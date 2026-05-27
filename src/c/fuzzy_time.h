#pragma once

// Fuzzy-time word logic
// All functions are pure: input is a tm-style hours/minutes pair, output is
// a const char * pointing at a static string literal.

// Returns "" when no minute word applies (top of the hour).
const char *fuzzy_minute_word(int minutes);

// Returns the hour word (e.g. "five"), respecting the "ten to {next hour}" rule.
const char *fuzzy_hour_word(int hours, int minutes);

// "past", "to", or "" near the top of the hour.
const char *fuzzy_join_word(int minutes);

// "o'clock" near the top of the hour, otherwise "".
const char *fuzzy_end_word(int minutes);