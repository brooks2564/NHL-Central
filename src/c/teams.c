// ── Teams, colors and logos ───────────────────────────────────────────────
#include "app.h"

const TeamInfo TEAMS[NUM_TEAMS] = {
  {"ANA", "Ducks",        'P'}, {"BOS", "Bruins",       'A'},
  {"BUF", "Sabres",       'A'}, {"CAR", "Hurricanes",   'M'},
  {"CBJ", "Blue Jackets", 'M'}, {"CGY", "Flames",       'P'},
  {"CHI", "Blackhawks",   'C'}, {"COL", "Avalanche",    'C'},
  {"DAL", "Stars",        'C'}, {"DET", "Red Wings",    'A'},
  {"EDM", "Oilers",       'P'}, {"FLA", "Panthers",     'A'},
  {"LAK", "Kings",        'P'}, {"MIN", "Wild",         'C'},
  {"MTL", "Canadiens",    'A'}, {"NJD", "Devils",       'M'},
  {"NSH", "Predators",    'C'}, {"NYI", "Islanders",    'M'},
  {"NYR", "Rangers",      'M'}, {"OTT", "Senators",     'A'},
  {"PHI", "Flyers",       'M'}, {"PIT", "Penguins",     'M'},
  {"SEA", "Kraken",       'P'}, {"SJS", "Sharks",       'P'},
  {"STL", "Blues",        'C'}, {"TBL", "Lightning",    'A'},
  {"TOR", "Maple Leafs",  'A'}, {"UTA", "Mammoth",      'C'},
  {"VAN", "Canucks",      'P'}, {"VGK", "Golden Knights", 'P'},
  {"WSH", "Capitals",     'M'}, {"WPG", "Jets",         'C'},
};

// Same order as TEAMS
#define LOGOS(a) {RESOURCE_ID_LOGO_##a##_LG, RESOURCE_ID_LOGO_##a##_SM}
static const uint32_t LOGO_IDS[NUM_TEAMS][2] = {
  LOGOS(ANA), LOGOS(BOS), LOGOS(BUF), LOGOS(CAR), LOGOS(CBJ), LOGOS(CGY),
  LOGOS(CHI), LOGOS(COL), LOGOS(DAL), LOGOS(DET), LOGOS(EDM), LOGOS(FLA),
  LOGOS(LAK), LOGOS(MIN), LOGOS(MTL), LOGOS(NJD), LOGOS(NSH), LOGOS(NYI),
  LOGOS(NYR), LOGOS(OTT), LOGOS(PHI), LOGOS(PIT), LOGOS(SEA), LOGOS(SJS),
  LOGOS(STL), LOGOS(TBL), LOGOS(TOR), LOGOS(UTA), LOGOS(VAN), LOGOS(VGK),
  LOGOS(WSH), LOGOS(WPG),
};

int team_index(const char *abbr) {
  if (!abbr || !abbr[0]) return -1;
  for (int i = 0; i < NUM_TEAMS; i++) {
    if (strcmp(TEAMS[i].abbr, abbr) == 0) return i;
  }
  return -1;
}

GColor team_color(const char *abbr) {
  static const char *ORANGE = "ANA EDM NYI PHI";
  static const char *YELLOW = "BOS NSH PIT VGK";
  static const char *RED    = "CAR CGY CHI DET FLA MTL NJD OTT WSH";
  static const char *GREEN  = "DAL MIN";
  static const char *TEAL   = "SEA SJS";
  if (!abbr || strlen(abbr) != 3) return GColorWhite;
  if (strstr(ORANGE, abbr)) return GColorOrange;
  if (strstr(YELLOW, abbr)) return GColorYellow;
  if (strstr(RED, abbr))    return GColorRed;
  if (strstr(GREEN, abbr))  return GColorIslamicGreen;
  if (strstr(TEAL, abbr))   return GColorTiffanyBlue;
  if (strcmp(abbr, "COL") == 0) return GColorImperialPurple;
  if (strcmp(abbr, "LAK") == 0) return GColorLightGray;
  return GColorVividCerulean;  // blue teams
}

// Small logos are cached for every team. Large logos use a few rotating
// slots, so callers must draw right away and never keep the pointer.
#define LG_SLOTS 4
static GBitmap *s_small[NUM_TEAMS];
static GBitmap *s_large[LG_SLOTS];
static int s_large_team[LG_SLOTS] = {-1, -1, -1, -1};
static int s_large_next;

GBitmap *team_logo(const char *abbr, bool large) {
  int i = team_index(abbr);
  if (i < 0) return NULL;
  if (!large) {
    if (!s_small[i]) s_small[i] = gbitmap_create_with_resource(LOGO_IDS[i][1]);
    return s_small[i];
  }
  for (int s = 0; s < LG_SLOTS; s++) {
    if (s_large_team[s] == i) return s_large[s];
  }
  int s = s_large_next;
  s_large_next = (s_large_next + 1) % LG_SLOTS;
  if (s_large[s]) gbitmap_destroy(s_large[s]);
  s_large[s] = gbitmap_create_with_resource(LOGO_IDS[i][0]);
  s_large_team[s] = i;
  return s_large[s];
}

void team_logos_deinit(void) {
  for (int i = 0; i < NUM_TEAMS; i++) {
    if (s_small[i]) gbitmap_destroy(s_small[i]);
    s_small[i] = NULL;
  }
  for (int s = 0; s < LG_SLOTS; s++) {
    if (s_large[s]) gbitmap_destroy(s_large[s]);
    s_large[s] = NULL;
    s_large_team[s] = -1;
  }
}
