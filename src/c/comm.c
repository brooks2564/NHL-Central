// ── Phone messaging: request queue + listeners ────────────────────────────
#include "app.h"

#define QUEUE_LEN      8
#define MAX_LISTENERS  10
#define ARG_LEN        24

typedef struct { int type; char arg[ARG_LEN]; } Request;
typedef struct {
  bool used, any_arg;
  int type;
  char arg[ARG_LEN];
  CommHandler cb;
  void *ctx;
} Listener;

static Request s_queue[QUEUE_LEN];
static int s_queue_len;
static bool s_busy;
static bool s_ready;          // phone JS has said hello
static int s_failures;
static AppTimer *s_retry_timer;
static Listener s_listeners[MAX_LISTENERS];
static SettingsHandler s_on_settings;

static void send_next(void);

static void retry_cb(void *ctx) {
  s_retry_timer = NULL;
  send_next();
}

static void schedule_retry(uint32_t ms) {
  if (!s_retry_timer) s_retry_timer = app_timer_register(ms, retry_cb, NULL);
}

static void pop_front(void) {
  if (s_queue_len == 0) return;
  memmove(&s_queue[0], &s_queue[1], sizeof(Request) * (s_queue_len - 1));
  s_queue_len--;
}

static void send_next(void) {
  if (s_busy || !s_ready || s_queue_len == 0) return;

  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    schedule_retry(500);
    return;
  }
  dict_write_int32(iter, MESSAGE_KEY_REQ, s_queue[0].type);
  dict_write_cstring(iter, MESSAGE_KEY_ARG, s_queue[0].arg);
  if (app_message_outbox_send() == APP_MSG_OK) {
    s_busy = true;
  } else {
    schedule_retry(500);
  }
}

void comm_request(int type, const char *arg) {
  if (!arg) arg = "";
  for (int i = 0; i < s_queue_len; i++) {
    if (s_queue[i].type == type && strcmp(s_queue[i].arg, arg) == 0) return;
  }
  if (s_queue_len == QUEUE_LEN) return;
  s_queue[s_queue_len].type = type;
  strncpy(s_queue[s_queue_len].arg, arg, ARG_LEN - 1);
  s_queue[s_queue_len].arg[ARG_LEN - 1] = '\0';
  s_queue_len++;
  send_next();
}

int comm_listen(int type, const char *arg, CommHandler cb, void *ctx) {
  for (int i = 0; i < MAX_LISTENERS; i++) {
    if (s_listeners[i].used) continue;
    Listener *l = &s_listeners[i];
    l->used = true;
    l->type = type;
    l->any_arg = (arg == NULL);
    strncpy(l->arg, arg ? arg : "", ARG_LEN - 1);
    l->arg[ARG_LEN - 1] = '\0';
    l->cb = cb;
    l->ctx = ctx;
    return i;
  }
  return -1;
}

void comm_unlisten(int id) {
  if (id >= 0 && id < MAX_LISTENERS) s_listeners[id].used = false;
}

// ── AppMessage handlers ───────────────────────────────────────────────────
static void inbox_received(DictionaryIterator *iter, void *ctx) {
  if (!s_ready) {
    s_ready = true;
    send_next();
  }

  if (dict_find(iter, MESSAGE_KEY_FAV_TEAM) && s_on_settings) {
    s_on_settings(iter);
  }

  Tuple *type = dict_find(iter, MESSAGE_KEY_TYPE);
  if (!type) return;
  Tuple *arg  = dict_find(iter, MESSAGE_KEY_ARG);
  Tuple *data = dict_find(iter, MESSAGE_KEY_DATA);
  Tuple *err  = dict_find(iter, MESSAGE_KEY_ERROR);
  const char *a = arg ? arg->value->cstring : "";
  int t = type->value->int32;

  for (int i = 0; i < MAX_LISTENERS; i++) {
    Listener *l = &s_listeners[i];
    if (!l->used || l->type != t) continue;
    if (!l->any_arg && strcmp(l->arg, a) != 0) continue;
    l->cb(data ? data->value->cstring : "", err ? err->value->cstring : NULL, l->ctx);
  }
}

static void outbox_sent(DictionaryIterator *iter, void *ctx) {
  s_busy = false;
  s_failures = 0;
  pop_front();
  send_next();
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *ctx) {
  s_busy = false;
  if (++s_failures >= 3) {   // give up on this one, move on
    s_failures = 0;
    pop_front();
  }
  schedule_retry(1000);
}

static void ready_timeout(void *ctx) {
  // If the phone never said hello, try anyway (failures are retried).
  if (!s_ready) {
    s_ready = true;
    send_next();
  }
}

void comm_init(SettingsHandler on_settings) {
  s_on_settings = on_settings;
  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);
  uint32_t inbox = app_message_inbox_size_maximum();
  if (inbox > TABLE_BUF + 512) inbox = TABLE_BUF + 512;
  app_message_open(inbox, 128);
  app_timer_register(3000, ready_timeout, NULL);
}
