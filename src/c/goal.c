// ── Goal celebration: flashing goal light + popup ─────────────────────────
#include "app.h"

#define GOAL_FRAMES     12
#define GOAL_FRAME_MS   90
#define POPUP_MS        4000

static const uint32_t GOAL_IDS[GOAL_FRAMES] = {
  RESOURCE_ID_IMAGE_GOAL_0, RESOURCE_ID_IMAGE_GOAL_1, RESOURCE_ID_IMAGE_GOAL_2,
  RESOURCE_ID_IMAGE_GOAL_3, RESOURCE_ID_IMAGE_GOAL_4, RESOURCE_ID_IMAGE_GOAL_5,
  RESOURCE_ID_IMAGE_GOAL_6, RESOURCE_ID_IMAGE_GOAL_7, RESOURCE_ID_IMAGE_GOAL_8,
  RESOURCE_ID_IMAGE_GOAL_9, RESOURCE_ID_IMAGE_GOAL_10, RESOURCE_ID_IMAGE_GOAL_11,
};

// Draws one frame of the celebration filling `bounds`.
void goal_draw(GContext *ctx, GRect bounds, int frame, const char *abbr, const char *line2) {
  bool on = (frame / 2) % 2 == 0;
  graphics_context_set_fill_color(ctx, on ? GColorRed : GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  GBitmap *light = gbitmap_create_with_resource(GOAL_IDS[frame % GOAL_FRAMES]);
  int y = bounds.origin.y + 4;
  if (light) {
    GSize sz = gbitmap_get_bounds(light).size;
    GRect r = GRect(bounds.origin.x + (bounds.size.w - sz.w) / 2, y, sz.w, sz.h);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, light, r);
    gbitmap_destroy(light);
    y += sz.h;
  }

  char title[16];
  snprintf(title, sizeof(title), "GOAL %s!", abbr ? abbr : "");
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(bounds.origin.x, y - 4, bounds.size.w, 32),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  if (line2 && line2[0] && bounds.size.h > y - bounds.origin.y + 50) {
    graphics_draw_text(ctx, line2, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(bounds.origin.x + ROUND_PAD, y + 26, bounds.size.w - 2 * ROUND_PAD, 56),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

// ── Popup window ──────────────────────────────────────────────────────────
static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_frame_timer, *s_close_timer;
static int s_frame;
static char s_abbr[4];
static char s_line2[40];

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  // Leave room on top for the team logo on tall screens
  GRect area = GRect(0, b.size.h / 2 - 70, b.size.w, 150);
  goal_draw(ctx, area, s_frame, s_abbr, s_line2);
  GBitmap *logo = team_logo(s_abbr, true);
  if (logo) {
    GSize sz = gbitmap_get_bounds(logo).size;
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, logo, GRect((b.size.w - sz.w) / 2, area.origin.y - sz.h - 6,
                                                  sz.w, sz.h));
  }
}

static void frame_tick(void *ctx) {
  s_frame++;
  layer_mark_dirty(s_canvas);
  s_frame_timer = app_timer_register(GOAL_FRAME_MS, frame_tick, NULL);
}

static void close_popup(void *ctx) {
  s_close_timer = NULL;
  if (s_window) window_stack_remove(s_window, true);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
  s_frame = 0;
  s_frame_timer = app_timer_register(GOAL_FRAME_MS, frame_tick, NULL);
}

static void window_unload(Window *window) {
  if (s_frame_timer) app_timer_cancel(s_frame_timer);
  if (s_close_timer) app_timer_cancel(s_close_timer);
  s_frame_timer = s_close_timer = NULL;
  layer_destroy(s_canvas);
  window_destroy(s_window);
  s_window = NULL;
}

void goal_popup_show(const char *abbr, const char *line2) {
  strncpy(s_abbr, abbr ? abbr : "", sizeof(s_abbr) - 1);
  s_abbr[sizeof(s_abbr) - 1] = '\0';
  strncpy(s_line2, line2 ? line2 : "", sizeof(s_line2) - 1);
  s_line2[sizeof(s_line2) - 1] = '\0';

  if (s_window) {   // already showing: restart with the new goal
    s_frame = 0;
    if (s_close_timer) app_timer_cancel(s_close_timer);
    s_close_timer = app_timer_register(POPUP_MS, close_popup, NULL);
    layer_mark_dirty(s_canvas);
    return;
  }
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  s_close_timer = app_timer_register(POPUP_MS, close_popup, NULL);
}
