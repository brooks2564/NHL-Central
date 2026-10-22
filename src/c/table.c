// ── Table: splits phone data into rows and fields, in place ───────────────
#include "app.h"

void table_parse(Table *t, const char *src) {
  strncpy(t->buf, src ? src : "", TABLE_BUF - 1);
  t->buf[TABLE_BUF - 1] = '\0';
  t->nrows = 0;

  char *p = t->buf;
  while (*p && t->nrows < TABLE_MAX_ROWS) {
    int r = t->nrows++;
    int c = 0;
    t->cell[r][c++] = p;
    while (*p && *p != '\n') {
      if (*p == '|') {
        *p = '\0';
        if (c < TABLE_MAX_COLS) t->cell[r][c++] = p + 1;
      }
      p++;
    }
    t->ncols[r] = c;
    if (*p == '\n') *p++ = '\0';
  }
}

const char *table_get(const Table *t, int row, int col) {
  if (row < 0 || row >= t->nrows || col < 0 || col >= t->ncols[row]) return "";
  return t->cell[row][col];
}
