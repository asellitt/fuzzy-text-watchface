#include "fuzzy_time.h"
#include <stddef.h>

static const char *HOUR_WORDS[] = {
  "one", "two", "three", "four", "five", "six",
  "seven", "eight", "nine", "ten", "eleven", "twelve",
};

static const char *MINUTES_WORDS[] = {
  "five", "ten", "quarter", "twenty", "twenty five", "half",
};

const char *fuzzy_minute_word(int minutes) {
  if (minutes <= 2 || minutes >= 58) return "";
  int index = ((minutes + 2) / 5) - 1;
  if (minutes <= 30) return MINUTES_WORDS[index];
  // Mirror around "half": index 5 -> 5 (half), 6 -> 4, 7 -> 3, ...
  int mirrored = index - (index - (int)(sizeof(MINUTES_WORDS) / sizeof(*MINUTES_WORDS)) + 1) * 2;
  return MINUTES_WORDS[mirrored];
}

const char *fuzzy_hour_word(int hours, int minutes) {
  int hour = hours % 12;
  if (hour == 0) hour = 12;
  if (minutes > 32) return HOUR_WORDS[hour % 12]; // next hour
  return HOUR_WORDS[hour - 1];
}

const char *fuzzy_join_word(int minutes) {
  if (minutes <= 2 || minutes >= 58) return "";
  if (minutes > 32) return "to";
  return "past";
}

const char *fuzzy_end_word(int minutes) {
  if (minutes <= 2 || minutes >= 58) return "o'clock";
  return "";
}