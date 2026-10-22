// ── NHL Central · watch app ────────────────────────────────────────────────
#include "app.h"

#define PERSIST_SETTINGS  1
#define WAKEUP_COOKIE     1

Settings g_settings = { .fav = "TOR", .follow = "", .vibrate = true, .auto_open = true };
int g_viewing_game;

static ListWindow *s_home;
static bool s_open_fav_game;   // open your team's game once data arrives

// ── Settings ──────────────────────────────────────────────────────────────
bool settings_is_followed(const char *abbr) {
  if (!abbr || strlen(abbr) != 3) return false;
  if (strcmp(abbr, g_settings.fav) == 0) return true;
  return strstr(g_settings.follow, abbr) != NULL;
}

static void build_home(void) {
  static char s_data[512];
  const char *status = "";
  bool live = false;
  tracker_fav_game(&status, &live);
  int i = team_index(g_settings.fav);
  snprintf(s_data, sizeof(s_data),
    "#|NHL Central\n"
    "L|%s|%s|4|%s|%s\n"
    "L|Today's Games||1||\n"
    "L|Teams||0|teams|\n"
    "L|Standings||0|standings|\n"
    "L|Playoffs||6||\n"
    "L|League Leaders||0|leaders|",
    i >= 0 ? TEAMS[i].name : "My Team",
    status[0] ? status : "Your team",
    g_settings.fav, g_settings.fav);
  if (s_home) list_window_set_static(s_home, s_data);
  else s_home = list_window_push_static(s_data);
}

void settings_set_favorite(const char *abbr) {
  if (team_index(abbr) < 0) return;
  strncpy(g_settings.fav, abbr, sizeof(g_settings.fav) - 1);
  g_settings.fav[sizeof(g_settings.fav) - 1] = '\0';
  persist_write_data(PERSIST_SETTINGS, &g_settings, sizeof(g_settings));
  comm_request(REQ_SET_FAV, abbr);
  vibes_short_pulse();
  build_home();
}

static void schedule_wakeup(time_t when) {
  wakeup_cancel_all();
  if (when > time(NULL) + 60) {
    WakeupId id = wakeup_schedule(when, WAKEUP_COOKIE, true);
    if (id < 0) APP_LOG(APP_LOG_LEVEL_WARNING, "wakeup_schedule failed: %d", (int)id);
  }
}

static void on_settings(DictionaryIterator *iter) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_FAV_TEAM)) && team_index(t->value->cstring) >= 0) {
    strncpy(g_settings.fav, t->value->cstring, sizeof(g_settings.fav) - 1);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_FOLLOW))) {
    strncpy(g_settings.follow, t->value->cstring, sizeof(g_settings.follow) - 1);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_VIBRATE)))   g_settings.vibrate = t->value->int32 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_AUTO_OPEN))) g_settings.auto_open = t->value->int32 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_WAKE_TIME))) schedule_wakeup((time_t)t->value->int32);
  persist_write_data(PERSIST_SETTINGS, &g_settings, sizeof(g_settings));
  build_home();
}

// ── Live tracker updates ──────────────────────────────────────────────────
static void on_tracker_change(void) {
  bool live = false;
  int game = tracker_fav_game(NULL, &live);
  if (s_open_fav_game && game) {
    if (live || launch_reason() == APP_LAUNCH_WAKEUP) {
      char id[16];
      snprintf(id, sizeof(id), "%d", game);
      list_window_push(REQ_GAME, id);
    }
    s_open_fav_game = false;
  }
  build_home();
}

// ── App lifecycle ─────────────────────────────────────────────────────────
static void init(void) {
  if (persist_exists(PERSIST_SETTINGS)) {
    persist_read_data(PERSIST_SETTINGS, &g_settings, sizeof(g_settings));
  }
#if defined(PBL_TOUCH)
  // Swipe to scroll, tap to select, swipe right to go back
  app_touch_navigation_enable(true);
#endif
  s_open_fav_game = g_settings.auto_open || launch_reason() == APP_LAUNCH_WAKEUP;
  comm_init(on_settings);
  build_home();
  tracker_init(on_tracker_change);
}

static void deinit(void) {
  tracker_deinit();
  team_logos_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
