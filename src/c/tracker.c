// ── Live score tracker: polls today's games, fires goal alerts ────────────
// Game rows look like: M|away|awayScore|home|homeScore|status text|gameId|state
// where state is P (pre-game), L (live) or F (final).
#include "app.h"

#define POLL_MS    30000
#define MAX_GAMES  24

typedef struct { int id; int away, home; } Score;

static Table *s_table;
static Score s_scores[MAX_GAMES];
static int s_num_scores;
static int s_listener = -1;
static AppTimer *s_poll_timer;
static TrackerHandler s_on_change;

static int s_fav_game;
static bool s_fav_live;
static char s_fav_status[40];

int tracker_fav_game(const char **status, bool *live) {
  if (status) *status = s_fav_status;
  if (live) *live = s_fav_live;
  return s_fav_game;
}

static Score *find_score(int id) {
  for (int i = 0; i < s_num_scores; i++) {
    if (s_scores[i].id == id) return &s_scores[i];
  }
  return NULL;
}

// Followed teams: vibrate, and show the popup unless that game is on screen
// (the game screen does its own flashing then).
static void check_goal(int id, const char *scorer, const char *away, int as, const char *home, int hs) {
  if (!settings_is_followed(scorer)) return;
  if (g_settings.vibrate) {
    static const uint32_t pattern[] = {300, 100, 300, 100, 600};
    vibes_enqueue_custom_pattern((VibePattern) { .durations = pattern, .num_segments = 5 });
  }
  if (id == g_viewing_game) return;
  char line[40];
  snprintf(line, sizeof(line), "%s %d - %d %s", away, as, hs, home);
  goal_popup_show(scorer, line);
}

static void on_games(const char *data, const char *error, void *ctx) {
  if (error) return;
  table_parse(s_table, data);

  s_fav_game = 0;
  s_fav_live = false;
  s_fav_status[0] = '\0';

  for (int r = 0; r < s_table->nrows; r++) {
    if (table_get(s_table, r, 0)[0] != 'M') continue;
    const char *away = table_get(s_table, r, 1);
    const char *home = table_get(s_table, r, 3);
    int as = atoi(table_get(s_table, r, 2));
    int hs = atoi(table_get(s_table, r, 4));
    int id = atoi(table_get(s_table, r, 6));
    char state = table_get(s_table, r, 7)[0];
    if (id == 0) continue;

    Score *s = find_score(id);
    if (s) {
      if (state == 'L') {
        if (as > s->away) check_goal(id, away, away, as, home, hs);
        else if (hs > s->home) check_goal(id, home, away, as, home, hs);
      }
      s->away = as;
      s->home = hs;
    } else if (s_num_scores < MAX_GAMES) {
      s_scores[s_num_scores++] = (Score) { .id = id, .away = as, .home = hs };
    }

    bool fav = g_settings.fav[0] &&
               (strcmp(away, g_settings.fav) == 0 || strcmp(home, g_settings.fav) == 0);
    if (fav && (!s_fav_game || state == 'L')) {
      s_fav_game = id;
      s_fav_live = (state == 'L');
      if (state == 'P') {
        snprintf(s_fav_status, sizeof(s_fav_status), "%s @ %s %s", away, home, table_get(s_table, r, 5));
      } else {
        snprintf(s_fav_status, sizeof(s_fav_status), "%s %d-%d %s %s", away, as, hs, home,
                 table_get(s_table, r, 5));
      }
    }
  }
  if (s_on_change) s_on_change();
}

static void poll(void *ctx) {
  comm_request(REQ_GAMES, "");
  s_poll_timer = app_timer_register(POLL_MS, poll, NULL);
}

void tracker_init(TrackerHandler on_change) {
  s_on_change = on_change;
  s_table = malloc(sizeof(Table));
  s_table->nrows = 0;
  s_listener = comm_listen(REQ_GAMES, NULL, on_games, NULL);
  poll(NULL);
}

void tracker_deinit(void) {
  if (s_poll_timer) app_timer_cancel(s_poll_timer);
  comm_unlisten(s_listener);
  free(s_table);
}
