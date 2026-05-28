// ---------------------------------------------------------------------------
// Fuzzy Text Watchface
// ---------------------------------------------------------------------------
//
// Targets Pebble Time 2 (Emery, 200x228, colour). Other platforms work but
// HRM is Emery-only; on Flint the heart row will show 0 (no hardware).
//
// Settings: Dark / Light mode, configurable via the Pebble phone app (Clay).
// Defaults to Dark on first launch.
// ---------------------------------------------------------------------------

#include <pebble.h>
#include "icons.h"
#include "fuzzy_time.h"

// --- layout constants ----------------------------------

#define TIME_TEXT_Y_OFFSET   10
#define STAT_TEXT_X_OFFSET   (ICON_BOX_W + 2)
#define STAT_TEXT_Y_OFFSET   -6
#define STAT_ROW_GAP_PX      4
#define STAT_PEEK_MARGIN_PX  -10

// Sized for Bitham 30 (30*1.25=27) and Gothic 28 (28*1.25=35)
#define TIME_ROW_H           36
#define JOIN_ROW_H           34
#define DATE_H               34
#define STAT_TEXT_H          34

// Timeline Quick View ("Peek") obstructs the bottom of the screen.
#define PEEK_HEIGHT_PX       51

// Persistent storage keys
#define PERSIST_KEY_DARK_MODE  1

// --- palette -------------------------------------------------------------
// Two complete palettes; swap by reassigning s_palette pointer.

typedef struct {
  GColor bg;          // window background, icon punch-through, battery interior
  GColor text;        // time, date, "past" — also battery outline
  GColor heart;
  GColor steps;
  GColor flame;
  GColor batt_good;
  GColor batt_med;
  GColor batt_low;
} Palette;

static Palette s_palette;

static void apply_palette(bool dark_mode) {
  if (dark_mode) {
    s_palette.bg        = GColorBlack;
    s_palette.text      = GColorWhite;
    s_palette.heart     = GColorFromRGB(248,  60, 140); // Ping
    s_palette.steps     = GColorFromRGB( 20, 211, 245); // Cyan
    s_palette.flame     = GColorFromRGB(255, 159,  51); // Orange
    s_palette.batt_good = GColorFromRGB(  0, 166,  41); // Green
    s_palette.batt_med  = GColorFromRGB(255, 170,   0); // Yellow
    s_palette.batt_low  = GColorFromRGB(255,   0,   0); // Red
  } else {
    s_palette.bg        = GColorWhite;
    s_palette.text      = GColorBlack;
    s_palette.heart     = GColorFromRGB(170,   0,  85); // Magenta
    s_palette.steps     = GColorFromRGB(  0,  85, 170); // Blue
    s_palette.flame     = GColorFromRGB(255, 102,   0); // Orange
    s_palette.batt_good = GColorFromRGB(  0, 166,  41); // Green
    s_palette.batt_med  = GColorFromRGB(255, 102,   0); // Orange
    s_palette.batt_low  = GColorFromRGB(255,   0,   0); // Red
  }
}

static GColor battery_colour(int percent) {
  if (percent > 40) return s_palette.batt_good;
  if (percent > 20) return s_palette.batt_med;
  return s_palette.batt_low;
}

// --- module state ---------------------------------------------------------

static Window     *s_window;
static TextLayer  *s_minute_layer;
static TextLayer  *s_join_layer;
static TextLayer  *s_hour_layer;
static TextLayer  *s_date_layer;
static Layer      *s_stats_layer;

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
static int s_battery_percent = 0;
static bool s_charging = false;
static bool s_dark_mode = false;

// --- helpers --------------------------------------------------------------

static void format_stat(int value, char *out, size_t out_size) {
  if (value >= 10000) {
    snprintf(out, out_size, "%dK", value / 1000);
  } else {
    snprintf(out, out_size, "%d", value);
  }
}

// --- stats layer update proc ---------------------------------------------

static void stats_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Background fill — uses palette so the stat band matches the rest.
  graphics_context_set_fill_color(ctx, s_palette.bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int xLeft = bounds.size.w * 5 / 100;
  int xRight = bounds.size.w * 55 / 100;
  int yRow1 = 0;
  int yRow2 = ICON_BOX_H + STAT_ROW_GAP_PX;

  GColor battC = battery_colour(s_battery_percent);

  // Heart
  icons_draw_heart(ctx, xLeft, yRow1, s_palette.heart, s_palette.bg);
  format_stat(s_hr, s_hr_buf, sizeof(s_hr_buf));
  graphics_context_set_text_color(ctx, s_palette.heart);
  graphics_draw_text(ctx, s_hr_buf, s_regular_font,
    GRect(xLeft + STAT_TEXT_X_OFFSET, yRow1 + STAT_TEXT_Y_OFFSET, 80, STAT_TEXT_H),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Steps
  icons_draw_steps(ctx, xRight, yRow1, s_palette.steps);
  format_stat(s_steps, s_steps_buf, sizeof(s_steps_buf));
  graphics_context_set_text_color(ctx, s_palette.steps);
  graphics_draw_text(ctx, s_steps_buf, s_regular_font,
    GRect(xRight + STAT_TEXT_X_OFFSET, yRow1 + STAT_TEXT_Y_OFFSET, 80, STAT_TEXT_H),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Flame (calories)
  icons_draw_flame(ctx, xLeft, yRow2, s_palette.flame, s_palette.bg);
  format_stat(s_calories, s_cal_buf, sizeof(s_cal_buf));
  graphics_context_set_text_color(ctx, s_palette.flame);
  graphics_draw_text(ctx, s_cal_buf, s_regular_font,
    GRect(xLeft + STAT_TEXT_X_OFFSET, yRow2 + STAT_TEXT_Y_OFFSET, 80, STAT_TEXT_H),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Battery — outline + interior come from the palette so light mode inverts.
  icons_draw_battery(ctx, xRight, yRow2,
                     s_palette.text, s_palette.bg, battC,
                     s_battery_percent, s_charging);
  format_stat(s_battery_percent, s_batt_buf, sizeof(s_batt_buf));
  graphics_context_set_text_color(ctx, battC);
  graphics_draw_text(ctx, s_batt_buf, s_regular_font,
    GRect(xRight + STAT_TEXT_X_OFFSET, yRow2 + STAT_TEXT_Y_OFFSET, 80, STAT_TEXT_H),
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
    strncpy(s_minute_buf, hour, sizeof(s_minute_buf));
    strncpy(s_join_buf,   end,  sizeof(s_join_buf));
    s_hour_buf[0] = '\0';
  } else {
    strncpy(s_minute_buf, minute, sizeof(s_minute_buf));
    strncpy(s_join_buf,   join,   sizeof(s_join_buf));
    strncpy(s_hour_buf,   hour,   sizeof(s_hour_buf));
  }

  int y_offset = end[0] ? (TIME_ROW_H / 2) : 0;

  GRect mf = layer_get_frame(text_layer_get_layer(s_minute_layer));
  mf.origin.y = TIME_TEXT_Y_OFFSET + y_offset;
  layer_set_frame(text_layer_get_layer(s_minute_layer), mf);

  GRect jf = layer_get_frame(text_layer_get_layer(s_join_layer));
  jf.origin.y = TIME_TEXT_Y_OFFSET + y_offset + TIME_ROW_H;
  layer_set_frame(text_layer_get_layer(s_join_layer), jf);

  text_layer_set_text(s_minute_layer, s_minute_buf);
  text_layer_set_text(s_join_layer,   s_join_buf);
  text_layer_set_text(s_hour_layer,   s_hour_buf);

  strftime(s_date_buf, sizeof(s_date_buf), "%a, %b %e", tick_time);
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
    int active  = (int)health_service_sum_today(HealthMetricActiveKCalories);
    int resting = (int)health_service_sum_today(HealthMetricRestingKCalories);
    s_calories = active + resting;
  }

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

// --- mode application -----------------------------------------------------

static void repaint_for_mode(void) {
  window_set_background_color(s_window, s_palette.bg);
  text_layer_set_text_color(s_minute_layer, s_palette.text);
  text_layer_set_text_color(s_join_layer,   s_palette.text);
  text_layer_set_text_color(s_hour_layer,   s_palette.text);
  text_layer_set_text_color(s_date_layer,   s_palette.text);
  layer_mark_dirty(s_stats_layer);
  layer_mark_dirty(window_get_root_layer(s_window));
}

// --- AppMessage inbox -----------------------------------------------------

static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_DARK_MODE);
  if (t) {
    // Clay sends booleans as int32 (0 or 1).
    s_dark_mode = (t->value->int32 != 0);
    persist_write_bool(PERSIST_KEY_DARK_MODE, s_dark_mode);
    apply_palette(s_dark_mode);
    repaint_for_mode();
  }
}

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
  window_set_background_color(window, s_palette.bg);

  s_bold_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  s_regular_font = fonts_get_system_font(FONT_KEY_GOTHIC_28);

  int y0 = 10;
  s_minute_layer = make_text_layer(root,
    GRect(0, y0, bounds.size.w, TIME_ROW_H), s_bold_font, s_palette.text, GTextAlignmentCenter);
  s_join_layer = make_text_layer(root,
    GRect(0, y0 + TIME_ROW_H, bounds.size.w, JOIN_ROW_H), s_regular_font,
    s_palette.text, GTextAlignmentCenter);
  s_hour_layer = make_text_layer(root,
    GRect(0, y0 + TIME_ROW_H + JOIN_ROW_H, bounds.size.w, TIME_ROW_H), s_bold_font,
    s_palette.text, GTextAlignmentCenter);

  int stats_h = ICON_BOX_H + STAT_ROW_GAP_PX + STAT_TEXT_H;
  int stats_y = bounds.size.h - PEEK_HEIGHT_PX - stats_h - STAT_PEEK_MARGIN_PX;
  s_stats_layer = layer_create(GRect(0, stats_y, bounds.size.w, stats_h));
  layer_set_update_proc(s_stats_layer, stats_update_proc);
  layer_add_child(root, s_stats_layer);

  int date_y = bounds.size.h - PEEK_HEIGHT_PX + (PEEK_HEIGHT_PX - DATE_H)/2;
  s_date_layer = make_text_layer(root,
    GRect(0, date_y, bounds.size.w, DATE_H),
    s_regular_font, s_palette.text, GTextAlignmentCenter);
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
  // Load the persisted dark-mode preference (defaults to true on first run).
  s_dark_mode = persist_exists(PERSIST_KEY_DARK_MODE)
                ? persist_read_bool(PERSIST_KEY_DARK_MODE)
                : s_dark_mode;
  apply_palette(s_dark_mode);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_window, true);

  time_t now = time(NULL);
  update_time_text(localtime(&now));

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

  // AppMessage for settings updates from Clay (Pebble phone app).
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(64, 64); // small buffer — we only ever receive one bool
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
