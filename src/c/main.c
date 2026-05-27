// ---------------------------------------------------------------------------
// Fuzzy Text Clockface
// ---------------------------------------------------------------------------
//
// Targets Pebble Time 2 (Emery, 200x228, colour). Other platforms work but
// HRM is Emery-only; on Flint the heart row will show 0 (no hardware).
//
// Layout:
//   - Centred 3-line fuzzy time (e.g. "twenty five / past / five")
//   - 2x2 stat grid: heart | steps / flame | battery
//   - Date centred at the bottom
//   - Stat row 2 sits above the Timeline Quick View ("Peek") zone.
//
// State updates:
//   - tick_handler           — every minute → refresh time + date
//   - battery_handler        — battery level / charging changes
//   - health_handler         — steps, calories, HR updates
//
// Drawing:
//   - Time, join, and date text use TextLayers (cheap, pre-rendered text).
//   - Stat icons + their numeric values are drawn in one custom Layer's
//     update_proc so we don't have to manage four separate icon layers.
// ---------------------------------------------------------------------------

#include <pebble.h>
#include "icons.h"
#include "fuzzy_time.h"

// --- layout constants ----------------------------------

#define STAT_TEXT_X_OFFSET   (ICON_BOX_W + 2)
#define STAT_TEXT_Y_OFFSET   (-2)
#define STAT_ROW_GAP_PX      4
#define STAT_PEEK_MARGIN_PX  2

// Timeline Quick View ("Peek") obstructs the bottom of the screen when an
// event is imminent. See the JS version for context — confirm on hardware.
#define PEEK_HEIGHT_PX       51

// --- module state ---------------------------------------------------------

static Window     *s_window;
static TextLayer  *s_minute_layer;
static TextLayer  *s_join_layer;
static TextLayer  *s_hour_layer;
static TextLayer  *s_date_layer;
static Layer      *s_stats_layer; // owns icon + number drawing

static GFont s_bold_font;
static GFont s_regular_font;

static char s_minute_buf[16];
static char s_join_buf[8];
static char s_hour_buf[16];
static char s_date_buf[32];
static char s_hr_buf[8];
static char s_steps_buf[8];
static char s_cal_buf[8];
static char s_batt_buf[8];

static int s_hr = 0;
static int s_steps = 0;
static int s_calories = 0;
static int s_battery_percent = 100;
static bool s_charging = false;

// --- helpers --------------------------------------------------------------

// "12345" -> "12K" for values >= 10000. Same logic as utils.js formatStat.
static void format_stat(int value, char *out, size_t out_size) {
  if (value >= 10000) {
    snprintf(out, out_size, "%dK", value / 1000);
  } else {
    snprintf(out, out_size, "%d", value);
  }
}

// --- brand palette --------------------------------------------------------
// Pebble colour displays use a 64-colour palette (2 bits per channel).
// GColorFromRGB() quantizes any RGB input to the nearest palette entry.
// Values mirror the JS palette (fb-magenta / fb-peach / fb-cyan).
#define COLOUR_MAGENTA  GColorFromRGB(248,  60, 140)  // -> GColorShockingPink
#define COLOUR_PEACH    GColorFromRGB(255, 159,  51)  // -> GColorRajah
#define COLOUR_CYAN     GColorFromRGB( 20, 211, 245)  // -> GColorCyan
#define COLOUR_GREEN    GColorFromRGB(  0, 166,  41)  // -> GColorIslamicGreen
#define COLOUR_YELLOW   GColorFromRGB(255, 170,   0)  // -> GColorChromeYellow
#define COLOUR_RED      GColorFromRGB(255,   0,   0)  // -> GColorRed
#define COLOUR_DIM      GColorLightGray

static GColor battery_colour(int percent) {
  if (percent > 40) return COLOUR_GREEN;
  if (percent > 20) return COLOUR_YELLOW;
  return COLOUR_RED;
}

// --- stats layer update proc ---------------------------------------------

static void stats_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, GColorWhite); // overridden per-stat

  // Background fill (stats layer is the stat band only, not the whole screen)
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Column origins inside the layer's own coordinate space.
  int xLeft = bounds.size.w * 5 / 100;
  int xRight = bounds.size.w * 55 / 100;
  int yRow1 = 0;
  int yRow2 = ICON_BOX_H + STAT_ROW_GAP_PX;

  GColor magenta = COLOUR_MAGENTA;
  GColor cyan    = COLOUR_CYAN;
  GColor peach   = COLOUR_PEACH;
  GColor battC   = battery_colour(s_battery_percent);

  // Heart
  icons_draw_heart(ctx, xLeft, yRow1, magenta, GColorBlack);
  format_stat(s_hr, s_hr_buf, sizeof(s_hr_buf));
  graphics_context_set_text_color(ctx, magenta);
  graphics_draw_text(ctx, s_hr_buf, s_regular_font,
    GRect(xLeft + STAT_TEXT_X_OFFSET, yRow1 + STAT_TEXT_Y_OFFSET, 80, 28),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Steps
  icons_draw_steps(ctx, xRight, yRow1, cyan);
  format_stat(s_steps, s_steps_buf, sizeof(s_steps_buf));
  graphics_context_set_text_color(ctx, cyan);
  graphics_draw_text(ctx, s_steps_buf, s_regular_font,
    GRect(xRight + STAT_TEXT_X_OFFSET, yRow1 + STAT_TEXT_Y_OFFSET, 80, 28),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Flame (calories)
  icons_draw_flame(ctx, xLeft, yRow2, peach, GColorBlack);
  format_stat(s_calories, s_cal_buf, sizeof(s_cal_buf));
  graphics_context_set_text_color(ctx, peach);
  graphics_draw_text(ctx, s_cal_buf, s_regular_font,
    GRect(xLeft + STAT_TEXT_X_OFFSET, yRow2 + STAT_TEXT_Y_OFFSET, 80, 28),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Battery
  icons_draw_battery(ctx, xRight, yRow2, battC, s_battery_percent, s_charging);
  format_stat(s_battery_percent, s_batt_buf, sizeof(s_batt_buf));
  graphics_context_set_text_color(ctx, battC);
  graphics_draw_text(ctx, s_batt_buf, s_regular_font,
    GRect(xRight + STAT_TEXT_X_OFFSET, yRow2 + STAT_TEXT_Y_OFFSET, 80, 28),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

// --- time / date update ---------------------------------------------------

static void update_time_text(struct tm *tick_time) {
  int h = tick_time->tm_hour;
  int m = tick_time->tm_min;

  const char *minute = fuzzy_minute_word(m);
  const char *join   = fuzzy_join_word(m);
  const char *hour   = fuzzy_hour_word(h, m);
  const char *end    = fuzzy_end_word(m);

  if (end[0]) {
    // Two-line layout: HOUR / o'clock
    strncpy(s_minute_buf, hour, sizeof(s_minute_buf));
    strncpy(s_join_buf,   end,  sizeof(s_join_buf));
    s_hour_buf[0] = '\0';
  } else {
    strncpy(s_minute_buf, minute, sizeof(s_minute_buf));
    strncpy(s_join_buf,   join,   sizeof(s_join_buf));
    strncpy(s_hour_buf,   hour,   sizeof(s_hour_buf));
  }

  text_layer_set_text(s_minute_layer, s_minute_buf);
  text_layer_set_text(s_join_layer,   s_join_buf);
  text_layer_set_text(s_hour_layer,   s_hour_buf);

  // Date: "Tuesday, March 10"
  strftime(s_date_buf, sizeof(s_date_buf), "%A, %B %e", tick_time);
  text_layer_set_text(s_date_layer, s_date_buf);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time_text(tick_time);
}

// --- battery handler ------------------------------------------------------

static void battery_handler(BatteryChargeState state) {
  s_battery_percent = state.charge_percent;
  s_charging = state.is_charging;
  layer_mark_dirty(s_stats_layer);
}

// --- health handler -------------------------------------------------------

#if defined(PBL_HEALTH)
static void refresh_health(void) {
  time_t start = time_start_of_today();
  time_t end = time(NULL);

  HealthServiceAccessibilityMask steps_mask =
    health_service_metric_accessible(HealthMetricStepCount, start, end);
  if (steps_mask & HealthServiceAccessibilityMaskAvailable) {
    s_steps = (int)health_service_sum_today(HealthMetricStepCount);
  }

  HealthServiceAccessibilityMask cal_mask =
    health_service_metric_accessible(HealthMetricActiveKCalories, start, end);
  if (cal_mask & HealthServiceAccessibilityMaskAvailable) {
    int active   = (int)health_service_sum_today(HealthMetricActiveKCalories);
    int resting  = (int)health_service_sum_today(HealthMetricRestingKCalories);
    s_calories = active + resting; // total daily kcal, like the Fitbit original
  }

  // Heart rate is a peek, not a sum.
  HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
  if (bpm > 0) s_hr = (int)bpm;

  layer_mark_dirty(s_stats_layer);
}

static void health_handler(HealthEventType event, void *context) {
  switch (event) {
    case HealthEventSignificantUpdate:
    case HealthEventMovementUpdate:
    case HealthEventHeartRateUpdate:
      refresh_health();
      break;
    case HealthEventSleepUpdate:
    default:
      break;
  }
}
#endif

// --- window load/unload ---------------------------------------------------

static TextLayer *make_text_layer(Layer *parent, GRect frame, GFont font,
                                  GColor color, GTextAlignment align) {
  TextLayer *t = text_layer_create(frame);
  text_layer_set_background_color(t, GColorClear);
  text_layer_set_text_color(t, color);
  text_layer_set_font(t, font);
  text_layer_set_text_alignment(t, align);
  layer_add_child(parent, text_layer_get_layer(t));
  return t;
}

static void main_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  window_set_background_color(window, GColorBlack);

  s_bold_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  s_regular_font = fonts_get_system_font(FONT_KEY_GOTHIC_24);

  // Time block — three stacked text layers, centred.
  int y0 = 10;
  int row_h = 32;
  s_minute_layer = make_text_layer(root,
    GRect(0, y0, bounds.size.w, row_h), s_bold_font, GColorWhite, GTextAlignmentCenter);
  s_join_layer = make_text_layer(root,
    GRect(0, y0 + row_h, bounds.size.w, 28), s_regular_font,
    GColorLightGray, GTextAlignmentCenter);
  s_hour_layer = make_text_layer(root,
    GRect(0, y0 + row_h + 28, bounds.size.w, row_h), s_bold_font,
    GColorWhite, GTextAlignmentCenter);

  // Stats layer occupies the row band above the Peek zone.
  int stats_h = 2 * ICON_BOX_H + STAT_ROW_GAP_PX;
  int stats_y = bounds.size.h - PEEK_HEIGHT_PX - stats_h - STAT_PEEK_MARGIN_PX;
  s_stats_layer = layer_create(GRect(0, stats_y, bounds.size.w, stats_h));
  layer_set_update_proc(s_stats_layer, stats_update_proc);
  layer_add_child(root, s_stats_layer);

  // Date at the bottom (will be covered by Peek when active — fine).
  int date_h = 28;
  s_date_layer = make_text_layer(root,
    GRect(0, bounds.size.h - date_h - 4, bounds.size.w, date_h),
    s_regular_font, GColorLightGray, GTextAlignmentCenter);
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_minute_layer);
  text_layer_destroy(s_join_layer);
  text_layer_destroy(s_hour_layer);
  text_layer_destroy(s_date_layer);
  layer_destroy(s_stats_layer);
}

// --- init / deinit / main -------------------------------------------------

static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_window, true);

  // Initial paint
  time_t now = time(NULL);
  update_time_text(localtime(&now));

  // Subscriptions
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  BatteryChargeState st = battery_state_service_peek();
  s_battery_percent = st.charge_percent;
  s_charging = st.is_charging;
  battery_state_service_subscribe(battery_handler);

#if defined(PBL_HEALTH)
  if (health_service_events_subscribe(health_handler, NULL)) {
    refresh_health();
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Health service unavailable");
  }
#endif
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}