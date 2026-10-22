// ── List screen: every page in the app is one of these ────────────────────
// The phone (or the watch, for static menus) sends rows like:
//   !|secs                                  refresh every `secs` seconds
//   #|Title                                 section header
//   T|abbr|name|line1|line2                 team header with big logo
//   G|away|aScore|home|hScore|status|sub|state   scoreboard (state P/L/F)
//   M|away|aScore|home|hScore|status|gameId|state  game row (state P/L/F/S)
//   R|left|right|sub|abbr|gameId            text row (opens game or team)
//   L|title|sub|req|arg|abbr                link row (opens another list)
// Buttons and touch both work: menus are driven by MenuLayer, which the
// system touch navigation scrolls (swipe), selects (tap) and backs out of
// (swipe right).
#include "app.h"

#define MAX_SECTIONS   16
#define ARG_LEN        24
#define FLASH_FRAMES   40
#define FLASH_MS       90

struct ListWindow {
  Window *window;
  MenuLayer *menu;
  TextLayer *msg;
  Table *table;
  int req;
  char arg[ARG_LEN];
  int listener;
  bool visible;

  int items[TABLE_MAX_ROWS];
  int nitems;
  struct { int start, count; const char *title; } sec[MAX_SECTIONS];
  int nsec;

  int refresh_secs;
  AppTimer *refresh_timer;

  bool pick;               // the "choose your team" list: picking a row sets your team

#ifdef PBL_TOUCH
  AppTimer *hold_timer;    // touch-and-hold detection
  GPoint touch;
#endif

  // Game screen goal flash
  int game_id;
  bool has_score;
  int away, home;
  int flash_frame;
  AppTimer *flash_timer;
  char flash_abbr[4];
  char flash_line[40];
};

#define F(c) table_get(lw->table, row, (c))

// Side padding of the cell being drawn. On round watches the circle cuts into
// rows near the top and bottom, so those rows get more padding to stay visible.
static int s_pad = ROUND_PAD;

#ifdef PBL_ROUND
static int isqrt(int n) {
  int r = 0;
  while ((r + 1) * (r + 1) <= n) r++;
  return r;
}
#endif

static int cell_pad(const Layer *cell, GRect b) {
#ifdef PBL_ROUND
  const int r = PBL_DISPLAY_WIDTH / 2;
  int top = layer_convert_point_to_screen(cell, GPointZero).y;
  // Distance from the screen's middle to where the text sits: the cell's
  // middle plus most of the way to its outer edge.
  int far = abs(top + b.size.h / 2 - r) + b.size.h * 3 / 10;
  if (far >= r) far = r - 1;
  int inset = r - isqrt(r * r - far * far) + 6;
  return inset > ROUND_PAD ? inset : ROUND_PAD;
#else
  return ROUND_PAD;
#endif
}

static GFont font(const char *key) { return fonts_get_system_font(key); }

// ── Building sections from rows ───────────────────────────────────────────
static void rebuild(ListWindow *lw) {
  lw->nitems = 0;
  lw->nsec = 0;
  lw->refresh_secs = 0;
  for (int row = 0; row < lw->table->nrows; row++) {
    char type = F(0)[0];
    if (type == '!') {
      lw->refresh_secs = atoi(F(1));
    } else if (type == '#') {
      if (lw->nsec < MAX_SECTIONS) {
        lw->sec[lw->nsec].start = lw->nitems;
        lw->sec[lw->nsec].count = 0;
        lw->sec[lw->nsec].title = F(1);
        lw->nsec++;
      }
    } else if (type && strchr("TGMRL", type)) {
      if (lw->nsec == 0) {
        lw->sec[0].start = 0;
        lw->sec[0].count = 0;
        lw->sec[0].title = NULL;
        lw->nsec = 1;
      }
      lw->items[lw->nitems++] = row;
      lw->sec[lw->nsec - 1].count++;
    }
  }
}

static int item_row(ListWindow *lw, MenuIndex *index) {
  if (index->section >= lw->nsec) return -1;
  int i = lw->sec[index->section].start + index->row;
  return (i < lw->nitems) ? lw->items[i] : -1;
}

static int find_row(ListWindow *lw, char type) {
  for (int row = 0; row < lw->table->nrows; row++) {
    if (F(0)[0] == type) return row;
  }
  return -1;
}

// ── Goal flash on the game screen ─────────────────────────────────────────
static void flash_tick(void *ctx) {
  ListWindow *lw = ctx;
  lw->flash_timer = NULL;
  if (++lw->flash_frame >= FLASH_FRAMES) {
    lw->flash_frame = 0;
  } else {
    lw->flash_timer = app_timer_register(FLASH_MS, flash_tick, lw);
  }
  layer_mark_dirty(menu_layer_get_layer(lw->menu));
}

static void start_flash(ListWindow *lw, const char *abbr, const char *line) {
  strncpy(lw->flash_abbr, abbr, sizeof(lw->flash_abbr) - 1);
  lw->flash_abbr[sizeof(lw->flash_abbr) - 1] = '\0';
  strncpy(lw->flash_line, line, sizeof(lw->flash_line) - 1);
  lw->flash_line[sizeof(lw->flash_line) - 1] = '\0';
  lw->flash_frame = 1;
  if (!lw->flash_timer) lw->flash_timer = app_timer_register(FLASH_MS, flash_tick, lw);
  // Followed teams already vibrate from the tracker
  if (g_settings.vibrate && !settings_is_followed(abbr)) vibes_long_pulse();
}

static void stop_flash(ListWindow *lw) {
  if (lw->flash_timer) app_timer_cancel(lw->flash_timer);
  lw->flash_timer = NULL;
  lw->flash_frame = 0;
}

static void check_score(ListWindow *lw) {
  int row = find_row(lw, 'G');
  if (row < 0) return;
  int as = atoi(F(2)), hs = atoi(F(4));
  if (lw->has_score && F(7)[0] == 'L' && (as > lw->away || hs > lw->home)) {
    char line[40];
    snprintf(line, sizeof(line), "%s %d - %d %s", F(1), as, hs, F(3));
    start_flash(lw, as > lw->away ? F(1) : F(3), line);
  }
  lw->has_score = true;
  lw->away = as;
  lw->home = hs;
}

// ── Data from the phone ───────────────────────────────────────────────────
static void refresh_cb(void *ctx) {
  ListWindow *lw = ctx;
  lw->refresh_timer = NULL;
  if (lw->visible) comm_request(lw->req, lw->arg);
}

static void schedule_refresh(ListWindow *lw, int secs) {
  if (lw->refresh_timer) app_timer_cancel(lw->refresh_timer);
  lw->refresh_timer = NULL;
  if (secs > 0 && lw->visible) {
    lw->refresh_timer = app_timer_register(secs * 1000, refresh_cb, lw);
  }
}

static void show_message(ListWindow *lw, const char *text) {
  if (!lw->msg) return;
  text_layer_set_text(lw->msg, text);
  layer_set_hidden(text_layer_get_layer(lw->msg), text == NULL);
}

static void update_view(ListWindow *lw) {
  if (!lw->menu) return;
  menu_layer_reload_data(lw->menu);
  show_message(lw, lw->nitems ? NULL : "Nothing to show right now");
}

static void on_data(const char *data, const char *error, void *ctx) {
  ListWindow *lw = ctx;
  if (error) {
    if (lw->nitems == 0) {
      static char s_err[64];
      strncpy(s_err, error, sizeof(s_err) - 1);
      show_message(lw, s_err);
    }
    schedule_refresh(lw, 30);
    return;
  }
  table_parse(lw->table, data);
  rebuild(lw);
  if (lw->req == REQ_GAME) check_score(lw);
  update_view(lw);
  schedule_refresh(lw, lw->refresh_secs);
}

// ── Drawing ───────────────────────────────────────────────────────────────
static void draw_logo(GContext *ctx, const char *abbr, bool large, GPoint at) {
  GBitmap *bmp = team_logo(abbr, large);
  if (!bmp) return;
  GSize sz = gbitmap_get_bounds(bmp).size;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(at.x, at.y, sz.w, sz.h));
}

static void text(GContext *ctx, const char *s, const char *font_key, GRect r, GTextAlignment align) {
  graphics_draw_text(ctx, s, font(font_key), r, GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void draw_text_row(GContext *ctx, GRect b, ListWindow *lw, int row, bool hl) {
  const char *left = F(1), *right = F(2), *sub = F(3), *abbr = F(4);
  int x = s_pad, w = b.size.w - 2 * s_pad;
  if (team_index(abbr) >= 0) {
    draw_logo(ctx, abbr, false, GPoint(x, sub[0] ? 10 : 4));
    x += 26;
    w -= 26;
  }
  int rw = 0;
  if (right[0]) {
    GSize sz = graphics_text_layout_get_content_size(right, font(FONT_KEY_GOTHIC_18_BOLD),
        GRect(0, 0, w, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentRight);
    rw = sz.w + 6;
    graphics_context_set_text_color(ctx, GColorWhite);
    text(ctx, right, FONT_KEY_GOTHIC_18_BOLD, GRect(x + w - rw, 2, rw, 24), GTextAlignmentRight);
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  text(ctx, left, FONT_KEY_GOTHIC_18_BOLD, GRect(x, 2, w - rw, 24), GTextAlignmentLeft);
  if (sub[0]) {
    graphics_context_set_text_color(ctx, hl ? GColorWhite : GColorLightGray);
    text(ctx, sub, FONT_KEY_GOTHIC_14, GRect(x, 23, w, 18), GTextAlignmentLeft);
  }
}

static void draw_link_row(GContext *ctx, GRect b, ListWindow *lw, int row, bool hl) {
  int x = s_pad, w = b.size.w - 2 * s_pad;
  const char *abbr = F(5);
  if (team_index(abbr) >= 0) {
    draw_logo(ctx, abbr, false, GPoint(x, 6));
    x += 28;
    w -= 28;
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  text(ctx, F(1), FONT_KEY_GOTHIC_24_BOLD, GRect(x, -2, w, 28), GTextAlignmentLeft);
  if (F(2)[0]) {
    graphics_context_set_text_color(ctx, hl ? GColorWhite : GColorLightGray);
    text(ctx, F(2), FONT_KEY_GOTHIC_18, GRect(x, 24, w, 22), GTextAlignmentLeft);
  }
}

static void draw_matchup_row(GContext *ctx, GRect b, ListWindow *lw, int row, bool hl) {
  int x = s_pad, w = b.size.w - 2 * s_pad;
  char state = F(7)[0];
  int as = atoi(F(2)), hs = atoi(F(4));
  for (int i = 0; i < 2; i++) {
    const char *abbr = F(i == 0 ? 1 : 3);
    const char *score = F(i == 0 ? 2 : 4);
    bool lost = (state == 'F' && (i == 0 ? as < hs : hs < as));
    int y = 2 + i * 24;
    draw_logo(ctx, abbr, false, GPoint(x, y + 1));
    graphics_context_set_text_color(ctx, (lost && !hl) ? GColorLightGray : GColorWhite);
    text(ctx, abbr, FONT_KEY_GOTHIC_18_BOLD, GRect(x + 26, y - 2, 42, 24), GTextAlignmentLeft);
    if (state != 'P') {
      text(ctx, score, FONT_KEY_GOTHIC_18_BOLD, GRect(x + 66, y - 2, 28, 24), GTextAlignmentRight);
    }
  }
  GColor status_color = hl ? GColorWhite : (state == 'L' ? GColorGreen : GColorLightGray);
  graphics_context_set_text_color(ctx, status_color);
  graphics_draw_text(ctx, F(5), font(FONT_KEY_GOTHIC_18), GRect(x + 98, 2, w - 98, 46),
                     GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
}

static void draw_team_header(GContext *ctx, GRect b, ListWindow *lw, int row, bool hl) {
  int x = s_pad, w = b.size.w - 2 * s_pad;
  draw_logo(ctx, F(1), true, GPoint(x, 16));
  x += 42;
  w -= 42;
  graphics_context_set_text_color(ctx, GColorWhite);
  text(ctx, F(2), FONT_KEY_GOTHIC_24_BOLD, GRect(x, -2, w, 28), GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, hl ? GColorWhite : GColorLightGray);
  text(ctx, F(3), FONT_KEY_GOTHIC_18, GRect(x, 24, w, 22), GTextAlignmentLeft);
  text(ctx, F(4), FONT_KEY_GOTHIC_18, GRect(x, 44, w, 22), GTextAlignmentLeft);
}

static void draw_scoreboard(GContext *ctx, GRect b, ListWindow *lw, int row, bool hl) {
  if (lw->flash_frame > 0) {
    goal_draw(ctx, GRect(0, 0, b.size.w, b.size.h), lw->flash_frame, lw->flash_abbr, lw->flash_line);
    return;
  }
  int x = s_pad, w = b.size.w - 2 * s_pad, cx = b.size.w / 2;
  char state = F(7)[0];
  const char *away = F(1), *home = F(3);

  draw_logo(ctx, away, true, GPoint(x, 4));
  draw_logo(ctx, home, true, GPoint(x + w - 36, 4));
  graphics_context_set_text_color(ctx, GColorWhite);
  text(ctx, away, FONT_KEY_GOTHIC_14_BOLD, GRect(x - 4, 38, 44, 18), GTextAlignmentCenter);
  text(ctx, home, FONT_KEY_GOTHIC_14_BOLD, GRect(x + w - 40, 38, 44, 18), GTextAlignmentCenter);

  if (state == 'P') {
    text(ctx, "@", FONT_KEY_GOTHIC_28_BOLD, GRect(cx - 20, 4, 40, 32), GTextAlignmentCenter);
  } else {
    text(ctx, F(2), FONT_KEY_LECO_32_BOLD_NUMBERS, GRect(x + 38, 2, cx - x - 46, 40), GTextAlignmentCenter);
    text(ctx, F(4), FONT_KEY_LECO_32_BOLD_NUMBERS, GRect(cx + 8, 2, x + w - 46 - cx, 40), GTextAlignmentCenter);
    text(ctx, "-", FONT_KEY_GOTHIC_28_BOLD, GRect(cx - 8, 4, 16, 32), GTextAlignmentCenter);
  }

  graphics_context_set_text_color(ctx, hl ? GColorWhite : (state == 'L' ? GColorGreen : GColorWhite));
  text(ctx, F(5), FONT_KEY_GOTHIC_24_BOLD, GRect(x, 54, w, 28), GTextAlignmentCenter);
  graphics_context_set_text_color(ctx, hl ? GColorWhite : GColorLightGray);
  text(ctx, F(6), FONT_KEY_GOTHIC_18, GRect(x, 80, w, 22), GTextAlignmentCenter);
}

// ── MenuLayer callbacks ───────────────────────────────────────────────────
static uint16_t get_num_sections(MenuLayer *menu, void *ctx) {
  ListWindow *lw = ctx;
  return lw->nsec;
}

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
  ListWindow *lw = ctx;
  return section < lw->nsec ? lw->sec[section].count : 0;
}

static int16_t get_header_height(MenuLayer *menu, uint16_t section, void *ctx) {
  ListWindow *lw = ctx;
  return (section < lw->nsec && lw->sec[section].title) ? 22 : 0;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
  ListWindow *lw = data;
  if (section >= lw->nsec || !lw->sec[section].title) return;
  GRect b = layer_get_bounds(cell);
  s_pad = cell_pad(cell, b);
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  text(ctx, lw->sec[section].title, FONT_KEY_GOTHIC_14_BOLD,
       GRect(s_pad, 2, b.size.w - 2 * s_pad, 18),
       PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
}

static int16_t get_cell_height(MenuLayer *menu, MenuIndex *index, void *ctx) {
  ListWindow *lw = ctx;
  int row = item_row(lw, index);
  switch (F(0)[0]) {
    case 'T': return 70;
    case 'G': return 106;
    case 'M': return 52;
    case 'L': return F(2)[0] ? 48 : 34;
    case 'R': return F(3)[0] ? 42 : 30;
    default:  return 30;
  }
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *data) {
  ListWindow *lw = data;
  int row = item_row(lw, index);
  if (row < 0) return;
  GRect b = layer_get_bounds(cell);
  s_pad = cell_pad(cell, b);
  bool hl = menu_cell_layer_is_highlighted(cell);
  switch (F(0)[0]) {
    case 'T': draw_team_header(ctx, b, lw, row, hl); break;
    case 'G': draw_scoreboard(ctx, b, lw, row, hl); break;
    case 'M': draw_matchup_row(ctx, b, lw, row, hl); break;
    case 'L': draw_link_row(ctx, b, lw, row, hl); break;
    case 'R': draw_text_row(ctx, b, lw, row, hl); break;
  }
}

static void open_team(ListWindow *lw, const char *abbr) {
  if (team_index(abbr) < 0) return;
  if (lw->req == REQ_TEAM && strcmp(lw->arg, abbr) == 0) return;
  list_window_push(REQ_TEAM, abbr);
}

static void open_game(ListWindow *lw, const char *id) {
  if (!id[0] || atoi(id) == 0) return;
  if (lw->req == REQ_GAME && strcmp(lw->arg, id) == 0) return;
  list_window_push(REQ_GAME, id);
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *ctx) {
  ListWindow *lw = ctx;
  int row = item_row(lw, index);
  if (row < 0) return;
  switch (F(0)[0]) {
    case 'M':
      open_game(lw, F(6));
      break;
    case 'R':
      if (F(5)[0]) open_game(lw, F(5));
      else open_team(lw, F(4));
      break;
    case 'L': {
      int req = atoi(F(3));
      if (req == REQ_SET_FAV) {
        bool pick = lw->pick;
        settings_set_favorite(F(4));
        if (pick) window_stack_pop(true);   // back to where you were
      }
      else if (req == REQ_STATIC) list_window_push_static(static_content(F(4)));
      else list_window_push(req, F(4));
      break;
    }
  }
}

// Hold Select (or touch and hold) on a team. On a team that isn't yours it
// makes that team yours; on your own team it opens a list to pick another.
static void select_long_click(MenuLayer *menu, MenuIndex *index, void *ctx) {
  ListWindow *lw = ctx;
  if (lw->pick) {
    select_click(menu, index, ctx);
    return;
  }
  int row = item_row(lw, index);
  if (row < 0) return;
  const char *abbr = "";
  switch (F(0)[0]) {
    case 'T': abbr = F(1); break;
    case 'R': abbr = F(4); break;
    case 'L': abbr = F(5); break;
  }
  if (team_index(abbr) < 0) return;
  if (strcmp(abbr, g_settings.fav) == 0) list_window_push_team_chooser();
  else settings_set_favorite(abbr);
}

#ifdef PBL_TOUCH
// Touch and hold: the system only reports taps and swipes, so watch the raw
// touch stream for a finger that stays put for HOLD_MS.
#define HOLD_MS    600
#define HOLD_SLOP  14   // pixels of movement that turn a hold into a scroll

static ListWindow *s_touch_owner;   // the window currently getting touch events

static void stop_hold(ListWindow *lw) {
  if (lw->hold_timer) app_timer_cancel(lw->hold_timer);
  lw->hold_timer = NULL;
}

// Which row is at this y position, counting from the top of the list
static bool row_at(ListWindow *lw, int y, MenuIndex *out) {
  int top = 0;
  for (int s = 0; s < lw->nsec; s++) {
    top += get_header_height(lw->menu, s, lw);
    for (int r = 0; r < lw->sec[s].count; r++) {
      MenuIndex idx = { .section = s, .row = r };
      int bottom = top + get_cell_height(lw->menu, &idx, lw);
      if (y >= top && y < bottom) {
        *out = idx;
        return true;
      }
      top = bottom;
    }
  }
  return false;
}

static void hold_fired(void *ctx) {
  ListWindow *lw = ctx;
  lw->hold_timer = NULL;
  GPoint scrolled = scroll_layer_get_content_offset(menu_layer_get_scroll_layer(lw->menu));
  MenuIndex idx;
  if (!row_at(lw, lw->touch.y - scrolled.y, &idx)) return;
  vibes_short_pulse();   // there's nothing to feel on a touchscreen, so buzz
  select_long_click(lw->menu, &idx, lw);
}

static void touch_handler(const TouchEvent *event, void *ctx) {
  ListWindow *lw = ctx;
  switch (event->type) {
    case TouchEvent_Touchdown:
      stop_hold(lw);
      lw->touch = GPoint(event->x, event->y);
      lw->hold_timer = app_timer_register(HOLD_MS, hold_fired, lw);
      break;
    case TouchEvent_PositionUpdate:
      if (abs(event->x - lw->touch.x) > HOLD_SLOP || abs(event->y - lw->touch.y) > HOLD_SLOP) {
        stop_hold(lw);
      }
      break;
    case TouchEvent_Liftoff:
      stop_hold(lw);
      break;
  }
}

static void touch_start(ListWindow *lw) {
  s_touch_owner = lw;
  touch_service_subscribe(touch_handler, lw);
}

static void touch_stop(ListWindow *lw) {
  stop_hold(lw);
  if (s_touch_owner == lw) {   // a newer window may already have taken over
    touch_service_unsubscribe();
    s_touch_owner = NULL;
  }
}
#endif

// ── Window lifecycle ──────────────────────────────────────────────────────
// Start the "choose your team" list on your current team
static void select_current_team(ListWindow *lw) {
  for (int sec = 0; sec < lw->nsec; sec++) {
    for (int i = 0; i < lw->sec[sec].count; i++) {
      int row = lw->items[lw->sec[sec].start + i];
      if (strcmp(F(4), g_settings.fav) == 0) {
        menu_layer_set_selected_index(lw->menu, (MenuIndex) { .section = sec, .row = i },
                                      MenuRowAlignCenter, false);
        return;
      }
    }
  }
}

static void window_load(Window *window) {
  ListWindow *lw = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  lw->menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(lw->menu, lw, (MenuLayerCallbacks) {
    .get_num_sections = get_num_sections,
    .get_num_rows = get_num_rows,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
    .select_long_click = select_long_click,
  });
  menu_layer_set_normal_colors(lw->menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(lw->menu, GColorCobaltBlue, GColorWhite);
  menu_layer_set_click_config_onto_window(lw->menu, window);
  layer_add_child(root, menu_layer_get_layer(lw->menu));

  lw->msg = text_layer_create(GRect(ROUND_PAD + 6, bounds.size.h / 2 - 30, bounds.size.w - 2 * ROUND_PAD - 12, 60));
  text_layer_set_background_color(lw->msg, GColorClear);
  text_layer_set_text_color(lw->msg, GColorWhite);
  text_layer_set_font(lw->msg, font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(lw->msg, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(lw->msg));

  if (lw->req == REQ_STATIC) update_view(lw);
  else show_message(lw, "Loading...");
  if (lw->pick) select_current_team(lw);
}

static void window_appear(Window *window) {
  ListWindow *lw = window_get_user_data(window);
  lw->visible = true;
  if (lw->req == REQ_GAME) g_viewing_game = lw->game_id;
  if (lw->req != REQ_STATIC) comm_request(lw->req, lw->arg);
#ifdef PBL_TOUCH
  touch_start(lw);
#endif
}

static void window_disappear(Window *window) {
  ListWindow *lw = window_get_user_data(window);
  lw->visible = false;
  if (lw->req == REQ_GAME && g_viewing_game == lw->game_id) g_viewing_game = 0;
  schedule_refresh(lw, 0);
  stop_flash(lw);
#ifdef PBL_TOUCH
  touch_stop(lw);
#endif
}

static void window_unload(Window *window) {
  ListWindow *lw = window_get_user_data(window);
  comm_unlisten(lw->listener);
  stop_flash(lw);
#ifdef PBL_TOUCH
  touch_stop(lw);
#endif
  if (lw->refresh_timer) app_timer_cancel(lw->refresh_timer);
  text_layer_destroy(lw->msg);
  menu_layer_destroy(lw->menu);
  window_destroy(lw->window);
  free(lw->table);
  free(lw);
}

static ListWindow *create(int req, const char *arg) {
  ListWindow *lw = malloc(sizeof(ListWindow));
  if (!lw) return NULL;
  memset(lw, 0, sizeof(ListWindow));
  lw->table = malloc(sizeof(Table));
  if (!lw->table) {
    free(lw);
    return NULL;
  }
  lw->table->nrows = 0;
  lw->req = req;
  strncpy(lw->arg, arg ? arg : "", ARG_LEN - 1);
  lw->listener = -1;
  if (req == REQ_GAME) lw->game_id = atoi(lw->arg);

  lw->window = window_create();
  window_set_background_color(lw->window, GColorBlack);
  window_set_user_data(lw->window, lw);
  window_set_window_handlers(lw->window, (WindowHandlers) {
    .load = window_load,
    .appear = window_appear,
    .disappear = window_disappear,
    .unload = window_unload,
  });
  return lw;
}

ListWindow *list_window_push(int req, const char *arg) {
  ListWindow *lw = create(req, arg);
  if (!lw) return NULL;
  lw->listener = comm_listen(req, lw->arg, on_data, lw);
  window_stack_push(lw->window, true);
  return lw;
}

ListWindow *list_window_push_static(const char *data) {
  ListWindow *lw = create(REQ_STATIC, "");
  if (!lw) return NULL;
  table_parse(lw->table, data);
  rebuild(lw);
  window_stack_push(lw->window, true);
  return lw;
}

// "Choose your team": every team by division, picking one makes it your team
ListWindow *list_window_push_team_chooser(void) {
  static char s_data[1900];
  static const char DIVS[] = "AMCP";
  static const char *DIV_NAMES[] = {"Atlantic", "Metropolitan", "Central", "Pacific"};
  int n = 0;
  for (int d = 0; d < 4; d++) {
    n += snprintf(s_data + n, sizeof(s_data) - n, "#|%s\n", DIV_NAMES[d]);
    for (int i = 0; i < NUM_TEAMS && n < (int)sizeof(s_data); i++) {
      if (TEAMS[i].div != DIVS[d]) continue;
      n += snprintf(s_data + n, sizeof(s_data) - n, "L|%s|%s|%d|%s|%s\n", TEAMS[i].name,
                    strcmp(TEAMS[i].abbr, g_settings.fav) == 0 ? "Your team" : "",
                    REQ_SET_FAV, TEAMS[i].abbr, TEAMS[i].abbr);
    }
  }
  ListWindow *lw = create(REQ_STATIC, "");
  if (!lw) return NULL;
  lw->pick = true;
  table_parse(lw->table, s_data);
  rebuild(lw);
  window_stack_push(lw->window, true);
  return lw;
}

void list_window_set_static(ListWindow *lw, const char *data) {
  if (!lw) return;
  table_parse(lw->table, data);
  rebuild(lw);
  update_view(lw);
}

// ── Menus built on the watch ──────────────────────────────────────────────
static const char STANDINGS_MENU[] =
  "#|Standings\n"
  "L|By Division||3|DIV\n"
  "L|Eastern Wild Card||3|EWC\n"
  "L|Western Wild Card||3|WWC\n"
  "L|Eastern Conference||3|E\n"
  "L|Western Conference||3|W\n"
  "L|League||3|L";

static const char LEADERS_MENU[] =
  "#|League Leaders\n"
  "L|Points||5|points\n"
  "L|Goals||5|goals\n"
  "L|Assists||5|assists\n"
  "L|Plus/Minus||5|plusMinus\n"
  "L|Goalie Wins||5|g:wins\n"
  "L|Save %||5|g:savePctg\n"
  "L|Goals Against Avg||5|g:goalsAgainstAverage";

const char *static_content(const char *name) {
  if (strcmp(name, "standings") == 0) return STANDINGS_MENU;
  if (strcmp(name, "leaders") == 0) return LEADERS_MENU;

  // "teams": every team, grouped by division
  static char s_teams[1600];
  static const char DIVS[] = "AMCP";
  static const char *DIV_NAMES[] = {"Atlantic", "Metropolitan", "Central", "Pacific"};
  int n = 0;
  for (int d = 0; d < 4; d++) {
    n += snprintf(s_teams + n, sizeof(s_teams) - n, "#|%s\n", DIV_NAMES[d]);
    for (int i = 0; i < NUM_TEAMS && n < (int)sizeof(s_teams); i++) {
      if (TEAMS[i].div != DIVS[d]) continue;
      n += snprintf(s_teams + n, sizeof(s_teams) - n, "R|%s|||%s|\n",
                    TEAMS[i].name, TEAMS[i].abbr);
    }
  }
  return s_teams;
}
