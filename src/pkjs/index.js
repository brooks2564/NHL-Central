// ── NHL Scores - PebbleKit JS ─────────────────────────────────────────────
// Fetches data from the NHL's free web API and sends each screen to the
// watch as text rows: rows separated by "\n", fields separated by "|".
// See the row formats at the top of src/c/list_window.c.

// Must match the enum in src/c/app.h exactly
var REQ_GAMES = 1, REQ_GAME = 2, REQ_STANDINGS = 3, REQ_TEAM = 4,
    REQ_LEADERS = 5, REQ_PLAYOFFS = 6, REQ_SETTINGS = 7, REQ_SET_FAV = 8;

var API = "https://api-web.nhle.com/v1/";
var MAX_DATA = 3800;   // watch buffer is 4096 bytes

var TEAMS = {
  ANA: "Ducks", BOS: "Bruins", BUF: "Sabres", CAR: "Hurricanes",
  CBJ: "Blue Jackets", CGY: "Flames", CHI: "Blackhawks", COL: "Avalanche",
  DAL: "Stars", DET: "Red Wings", EDM: "Oilers", FLA: "Panthers",
  LAK: "Kings", MIN: "Wild", MTL: "Canadiens", NJD: "Devils",
  NSH: "Predators", NYI: "Islanders", NYR: "Rangers", OTT: "Senators",
  PHI: "Flyers", PIT: "Penguins", SEA: "Kraken", SJS: "Sharks",
  STL: "Blues", TBL: "Lightning", TOR: "Maple Leafs", UTA: "Mammoth",
  VAN: "Canucks", VGK: "Golden Knights", WSH: "Capitals", WPG: "Jets"
};
var DIVISIONS = { A: "Atlantic", M: "Metropolitan", C: "Central", P: "Pacific" };

// ── Settings ──────────────────────────────────────────────────────────────
var DEFAULT_SETTINGS = { fav: "TOR", follow: [], vibrate: true, autoOpen: true, wake: true };

function loadSettings() {
  var s = {};
  try { s = JSON.parse(localStorage.getItem("settings")) || {}; } catch (e) {}
  for (var k in DEFAULT_SETTINGS) {
    if (s[k] === undefined) s[k] = DEFAULT_SETTINGS[k];
  }
  if (!TEAMS[s.fav]) s.fav = DEFAULT_SETTINGS.fav;
  return s;
}

function saveSettings(s) {
  localStorage.setItem("settings", JSON.stringify(s));
}

var gSettings = loadSettings();

// ── Sending to the watch (one message at a time) ──────────────────────────
var gQueue = [];
var gSending = false;

function send(msg) {
  gQueue.push(msg);
  pump();
}

function pump() {
  if (gSending || gQueue.length === 0) return;
  gSending = true;
  var msg = gQueue.shift();
  Pebble.sendAppMessage(msg, function () {
    gSending = false;
    pump();
  }, function (e) {
    console.log("[NHL] send failed: " + JSON.stringify(e));
    gSending = false;
    pump();
  });
}

function clean(s) {
  return String(s === undefined || s === null ? "" : s).replace(/[|\n]/g, " ");
}

function row() {
  var parts = [];
  for (var i = 0; i < arguments.length; i++) parts.push(clean(arguments[i]));
  return parts.join("|");
}

function sendRows(type, arg, rows) {
  var data = rows.join("\n");
  // Drop rows from the end until it fits (counting UTF-8 bytes)
  while (unescape(encodeURIComponent(data)).length > MAX_DATA && rows.length > 1) {
    rows.pop();
    data = rows.join("\n");
  }
  send({ TYPE: type, ARG: arg, DATA: data });
}

function sendError(type, arg, text) {
  send({ TYPE: type, ARG: arg, ERROR: text });
}

// ── HTTP with a short cache ───────────────────────────────────────────────
var gCache = {};

function getJSON(path, maxAgeSecs, cb) {
  var hit = gCache[path];
  if (hit && Date.now() - hit.time < maxAgeSecs * 1000) {
    cb(null, hit.data);
    return;
  }
  var xhr = new XMLHttpRequest();
  xhr.open("GET", API + path, true);
  xhr.setRequestHeader("Accept", "application/json");
  xhr.timeout = 15000;
  xhr.onload = function () {
    if (xhr.status !== 200) {
      cb("NHL data error (" + xhr.status + ")");
      return;
    }
    try {
      var data = JSON.parse(xhr.responseText);
      gCache[path] = { time: Date.now(), data: data };
      cb(null, data);
    } catch (e) {
      cb("Couldn't read NHL data");
    }
  };
  xhr.onerror = xhr.ontimeout = function () { cb("No connection"); };
  xhr.send();
}

// ── Formatting helpers ────────────────────────────────────────────────────
var DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
var MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

function pad2(n) { return n < 10 ? "0" + n : "" + n; }

function localDateStr(d) {
  return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate());
}

function formatTime(iso) {
  var d = new Date(iso);
  if (isNaN(d.getTime())) return "";
  var h = d.getHours(), m = d.getMinutes();
  var ampm = h >= 12 ? "PM" : "AM";
  h = h % 12 || 12;
  return h + ":" + pad2(m) + " " + ampm;
}

// "2026-10-24" -> "Sat Oct 24"
function formatDate(ymd) {
  var p = (ymd || "").split("-");
  if (p.length < 3) return ymd || "";
  var d = new Date(+p[0], +p[1] - 1, +p[2]);
  return DAYS[d.getDay()] + " " + MONTHS[d.getMonth()] + " " + d.getDate();
}

function ordinal(n) {
  n = +n || 0;
  var s = ["th", "st", "nd", "rd"], v = n % 100;
  return n + (s[(v - 20) % 10] || s[v] || s[0]);
}

function name(obj) {
  return (obj && (obj["default"] || obj)) || "";
}

function gameState(g) {
  var s = (g.gameState || "").toUpperCase();
  if (s === "LIVE" || s === "CRIT") return "L";
  if (s === "FINAL" || s === "OFF" || s === "OVER") return "F";
  return "P";
}

function periodLabel(pd) {
  pd = pd || {};
  var t = (pd.periodType || "").toUpperCase();
  if (t === "SO") return "SO";
  if (t === "OT") return (pd.number > 4 ? (pd.number - 3) : "") + "OT";
  return ["", "1st", "2nd", "3rd"][pd.number] || ("P" + (pd.number || ""));
}

function statusText(g) {
  var st = gameState(g);
  if (st === "P") {
    if ((g.gameScheduleState || "") === "PPD") return "Postponed";
    return formatTime(g.startTimeUTC);
  }
  if (st === "F") {
    var last = (g.gameOutcome && g.gameOutcome.lastPeriodType) ||
               (g.periodDescriptor && g.periodDescriptor.periodType) || "";
    return (last === "OT" || last === "SO") ? "Final/" + last : "Final";
  }
  var clock = g.clock || {};
  if (clock.inIntermission) return periodLabel(g.periodDescriptor) + " INT";
  return periodLabel(g.periodDescriptor) + " " + (clock.timeRemaining || "");
}

function matchupRow(g) {
  var a = g.awayTeam || {}, h = g.homeTeam || {};
  return row("M", a.abbrev, a.score || 0, h.abbrev, h.score || 0,
             statusText(g), g.id, gameState(g));
}

// Power play text from a situation code like "1451" (away goalie, away
// skaters, home skaters, home goalie).
function powerPlay(g) {
  var sit = g.situation;
  if (!sit || !sit.situationCode || sit.situationCode.length < 4) return "";
  var c = sit.situationCode;
  var as = +c[1], hs = +c[2];
  var time = sit.timeRemaining ? " " + sit.timeRemaining : "";
  var a = (g.awayTeam || {}).abbrev, h = (g.homeTeam || {}).abbrev;
  if (+c[0] === 0) return a + " empty net";
  if (+c[3] === 0) return h + " empty net";
  if (as > hs) return a + " PP " + as + "v" + hs + time;
  if (hs > as) return h + " PP " + hs + "v" + as + time;
  return "";
}

// ── Standings (shared by several screens) ─────────────────────────────────
function getStandings(cb) {
  getJSON("standings/now", 300, function (err, data) {
    if (err) return cb(err);
    var list = (data.standings || []).map(function (t) {
      var abbr = name(t.teamAbbrev);
      return {
        abbr: abbr,
        name: TEAMS[abbr] || name(t.teamCommonName),
        div: (t.divisionAbbrev || "").charAt(0),
        conf: (t.conferenceAbbrev || "").charAt(0),
        divRank: t.divisionSequence || 0,
        confRank: t.conferenceSequence || 0,
        leagueRank: t.leagueSequence || 0,
        w: t.wins || 0, l: t.losses || 0, otl: t.otLosses || 0,
        pts: t.points || 0, gp: t.gamesPlayed || 0,
        diff: t.goalDifferential || 0,
        streak: t.streakCode ? t.streakCode + (t.streakCount || "") : "",
        l10: (t.l10Wins || 0) + "-" + (t.l10Losses || 0) + "-" + (t.l10OtLosses || 0),
        clinch: t.clinchIndicator || ""
      };
    });
    cb(null, list);
  });
}

function record(t) {
  return t ? t.w + "-" + t.l + "-" + t.otl : "";
}

function findTeam(list, abbr) {
  for (var i = 0; i < list.length; i++) if (list[i].abbr === abbr) return list[i];
  return null;
}

function standingsRow(t, rankText) {
  var clinch = t.clinch ? " (" + t.clinch + ")" : "";
  return row("R", rankText + " " + t.name + clinch, t.pts + " pts",
             record(t) + "  GP " + t.gp + (t.streak ? "  " + t.streak : ""), t.abbr, "");
}

function byKey(key) {
  return function (a, b) { return a[key] - b[key]; };
}

// ── Screens ───────────────────────────────────────────────────────────────
function sendGames(arg) {
  getJSON("schedule/now", 20, function (err, data) {
    if (err) return sendError(REQ_GAMES, arg, err);
    var week = data.gameWeek || [];
    var today = localDateStr(new Date());
    var rows = ["!|30"];
    var live = [], todays = [], next = null;

    week.forEach(function (day) {
      (day.games || []).forEach(function (g) {
        if (day.date === today) todays.push(g);
        else if (gameState(g) === "L") live.push(g);   // late games past midnight
      });
      if (!next && day.date > today && (day.games || []).length) next = day;
    });

    if (live.length) {
      rows.push("#|Still Playing");
      live.forEach(function (g) { rows.push(matchupRow(g)); });
    }
    if (todays.length) {
      rows.push("#|Today - " + formatDate(today));
      todays.forEach(function (g) { rows.push(matchupRow(g)); });
    } else if (next) {
      rows.push("#|No games today");
      rows.push("#|Next - " + formatDate(next.date));
      next.games.forEach(function (g) { rows.push(matchupRow(g)); });
    } else {
      rows.push("#|No games scheduled");
    }
    sendRows(REQ_GAMES, arg, rows);
  });
}

function goalieRows(box, side) {
  var team = ((box.playerByGameStats || {})[side + "Team"]) || {};
  var abbr = ((box[side + "Team"]) || {}).abbrev || "";
  return (team.goalies || []).filter(function (g) {
    return g.toi && g.toi !== "00:00";
  }).map(function (g) {
    var pct = g.savePctg !== undefined ? (+g.savePctg).toFixed(3).replace(/^0/, "") + " SV%" : "";
    return row("R", name(g.name), g.saveShotsAgainst || "", pct + "  TOI " + g.toi, abbr, "");
  });
}

function sendGame(arg) {
  getJSON("gamecenter/" + arg + "/landing", 10, function (err, g) {
    if (err) return sendError(REQ_GAME, arg, err);
    var st = gameState(g);
    var a = g.awayTeam || {}, h = g.homeTeam || {};
    var status = st === "P" ? formatDate(localDateStr(new Date(g.startTimeUTC))) + " " +
                 formatTime(g.startTimeUTC) : statusText(g);
    var sub = st === "L" ? powerPlay(g) : "";
    if (!sub && st !== "P") sub = "Shots " + (a.sog || 0) + " - " + (h.sog || 0);

    getStandings(function (err2, standings) {
      standings = standings || [];
      var rows = ["!|" + (st === "L" ? 15 : st === "P" ? 120 : 0)];
      rows.push(row("G", a.abbrev, a.score || 0, h.abbrev, h.score || 0, status, sub, st));

      rows.push("#|Game");
      [a, h].forEach(function (t) {
        var s = findTeam(standings, t.abbrev);
        rows.push(row("R", (TEAMS[t.abbrev] || t.abbrev), s ? s.pts + " pts" : "",
                      s ? record(s) + "  " + ordinal(s.divRank) + " " + DIVISIONS[s.div] : "",
                      t.abbrev, ""));
      });
      if (st !== "P") rows.push(row("R", "Shots on goal", (a.sog || 0) + " - " + (h.sog || 0), "", "", ""));
      if (g.seriesStatus || g.seriesSummary) {
        var ss = g.seriesStatus || {};
        var txt = ss.seriesStatusShort || (g.seriesSummary || {}).seriesStatusShort ||
                  (ss.topSeedTeamAbbrev ? ss.topSeedTeamAbbrev + " " + ss.topSeedWins + " - " +
                   ss.bottomSeedWins + " " + ss.bottomSeedTeamAbbrev : "");
        if (txt) rows.push(row("R", "Series", txt, "", "", ""));
      }
      if (g.venue) rows.push(row("R", "Venue", "", name(g.venue), "", ""));

      // Goals by period
      var scoring = (g.summary && g.summary.scoring) || [];
      scoring.forEach(function (p) {
        var goals = p.goals || [];
        if (!goals.length) return;
        rows.push("#|" + periodLabel(p.periodDescriptor) + " period");
        goals.forEach(function (goal) {
          var who = name(goal.name) ||
                    (name(goal.firstName).charAt(0) + ". " + name(goal.lastName));
          var team = name(goal.teamAbbrev);
          var strength = (goal.strength || "").toUpperCase();
          var tag = strength === "PP" ? "PPG" : strength === "SH" ? "SHG" :
                    goal.goalModifier === "empty-net" ? "ENG" : "";
          var assists = (goal.assists || []).map(function (x) {
            return name(x.name) || name(x.lastName);
          }).join(", ");
          rows.push(row("R", who + (goal.goalsToDate ? " (" + goal.goalsToDate + ")" : ""),
                        goal.timeInPeriod || "",
                        (goal.awayScore !== undefined ? goal.awayScore + "-" + goal.homeScore + " " : "") +
                        (tag ? tag + " " : "") + (assists ? "A: " + assists : "Unassisted"),
                        team, ""));
        });
      });

      if (st === "P") {
        sendRows(REQ_GAME, arg, rows);
        return;
      }
      getJSON("gamecenter/" + arg + "/boxscore", 10, function (err3, box) {
        if (!err3 && box) {
          var goalies = goalieRows(box, "away").concat(goalieRows(box, "home"));
          if (goalies.length) {
            rows.push("#|Goalies");
            rows = rows.concat(goalies);
          }
        }
        sendRows(REQ_GAME, arg, rows);
      });
    });
  });
}

function sendStandings(arg) {
  getStandings(function (err, list) {
    if (err) return sendError(REQ_STANDINGS, arg, err);
    var rows = [];
    if (arg === "DIV") {
      ["A", "M", "C", "P"].forEach(function (d) {
        rows.push("#|" + DIVISIONS[d]);
        list.filter(function (t) { return t.div === d; }).sort(byKey("divRank"))
          .forEach(function (t) { rows.push(standingsRow(t, t.divRank + ".")); });
      });
    } else if (arg === "EWC" || arg === "WWC") {
      var conf = arg.charAt(0);
      var divs = conf === "E" ? ["A", "M"] : ["C", "P"];
      var leaders = [];
      divs.forEach(function (d) {
        rows.push("#|" + DIVISIONS[d]);
        list.filter(function (t) { return t.div === d && t.divRank <= 3; }).sort(byKey("divRank"))
          .forEach(function (t) { leaders.push(t.abbr); rows.push(standingsRow(t, d + t.divRank)); });
      });
      rows.push("#|Wild Card");
      list.filter(function (t) { return t.conf === conf && leaders.indexOf(t.abbr) < 0; })
        .sort(byKey("confRank"))
        .forEach(function (t, i) {
          rows.push(standingsRow(t, i < 2 ? "WC" + (i + 1) : "--"));
        });
    } else if (arg === "E" || arg === "W") {
      rows.push("#|" + (arg === "E" ? "Eastern" : "Western") + " Conference");
      list.filter(function (t) { return t.conf === arg; }).sort(byKey("confRank"))
        .forEach(function (t) { rows.push(standingsRow(t, t.confRank + ".")); });
    } else {
      rows.push("#|League");
      list.slice().sort(byKey("leagueRank"))
        .forEach(function (t) { rows.push(standingsRow(t, t.leagueRank + ".")); });
    }
    sendRows(REQ_STANDINGS, arg, rows);
  });
}

function sendTeam(abbr) {
  if (!TEAMS[abbr]) return sendError(REQ_TEAM, abbr, "Unknown team");
  getStandings(function (err, list) {
    var s = list ? findTeam(list, abbr) : null;
    getJSON("club-schedule-season/" + abbr + "/now", 120, function (err2, sched) {
      if (err && err2) return sendError(REQ_TEAM, abbr, err2);
      var rows = ["!|60"];
      rows.push(row("T", abbr, TEAMS[abbr],
                    s ? record(s) + ", " + s.pts + " pts" : "",
                    s ? ordinal(s.divRank) + " " + DIVISIONS[s.div] + (s.streak ? ", streak " + s.streak : "") : ""));

      var games = (sched && sched.games) || [];
      var today = localDateStr(new Date());
      var current = null, upcoming = [], recent = [];
      games.forEach(function (g) {
        var st = gameState(g);
        if (st === "L" || (g.gameDate === today)) current = g;
        else if (st === "P" && g.gameDate > today) upcoming.push(g);
        else if (st === "F") recent.push(g);
      });

      if (current) {
        rows.push("#|" + (gameState(current) === "L" ? "Live now" : "Today"));
        rows.push(matchupRow(current));
      }
      if (s) {
        rows.push("#|Season");
        rows.push(row("R", "Division", ordinal(s.divRank), DIVISIONS[s.div], "", ""));
        rows.push(row("R", "Conference", ordinal(s.confRank), "", "", ""));
        rows.push(row("R", "League", ordinal(s.leagueRank), "", "", ""));
        rows.push(row("R", "Last 10", s.l10, "", "", ""));
        if (s.streak) rows.push(row("R", "Streak", s.streak, "", "", ""));
        rows.push(row("R", "Goal diff", (s.diff > 0 ? "+" : "") + s.diff, "", "", ""));
      }
      if (upcoming.length) {
        rows.push("#|Next games");
        upcoming.slice(0, 5).forEach(function (g) {
          var home = (g.homeTeam || {}).abbrev === abbr;
          var opp = home ? (g.awayTeam || {}).abbrev : (g.homeTeam || {}).abbrev;
          rows.push(row("R", (home ? "vs " : "@ ") + opp, formatTime(g.startTimeUTC),
                        formatDate(g.gameDate), opp, g.id));
        });
      }
      if (recent.length) {
        rows.push("#|Recent");
        recent.slice(-5).reverse().forEach(function (g) {
          var home = (g.homeTeam || {}).abbrev === abbr;
          var me = home ? g.homeTeam : g.awayTeam, them = home ? g.awayTeam : g.homeTeam;
          var won = (me.score || 0) > (them.score || 0);
          var last = (g.gameOutcome && g.gameOutcome.lastPeriodType) || "";
          var suffix = (last === "OT" || last === "SO") ? " " + last : "";
          rows.push(row("R", (won ? "W " : "L ") + me.score + "-" + them.score + suffix,
                        (home ? "vs " : "@ ") + them.abbrev, formatDate(g.gameDate),
                        them.abbrev, g.id));
        });
      }
      if (gSettings.fav !== abbr) {
        rows.push("#|My Team");
        rows.push(row("L", "Make " + TEAMS[abbr] + " my team", "Or hold Select on any team",
                      REQ_SET_FAV, abbr, abbr));
      }
      sendRows(REQ_TEAM, abbr, rows);
    });
  });
}

var LEADER_TITLES = {
  points: "Points", goals: "Goals", assists: "Assists", plusMinus: "Plus/Minus",
  wins: "Goalie Wins", savePctg: "Save %", goalsAgainstAverage: "Goals Against Avg"
};

function sendLeaders(arg) {
  var goalie = arg.indexOf("g:") === 0;
  var cat = goalie ? arg.substring(2) : arg;
  var path = (goalie ? "goalie" : "skater") + "-stats-leaders/current?categories=" + cat + "&limit=20";
  getJSON(path, 600, function (err, data) {
    if (err) return sendError(REQ_LEADERS, arg, err);
    var list = data[cat] || [];
    var rows = ["#|" + (LEADER_TITLES[cat] || cat)];
    list.forEach(function (p, i) {
      var v = p.value;
      if (cat === "savePctg") v = (+v).toFixed(3).replace(/^0/, "");
      else if (cat === "goalsAgainstAverage") v = (+v).toFixed(2);
      else if (cat === "plusMinus" && v > 0) v = "+" + v;
      var who = name(p.firstName).charAt(0) + ". " + name(p.lastName);
      rows.push(row("R", (i + 1) + ". " + who, v,
                    (TEAMS[p.teamAbbrev] || p.teamAbbrev || "") + (p.position ? " - " + p.position : ""),
                    p.teamAbbrev, ""));
    });
    if (!list.length) rows.push(row("R", "No stats yet", "", "", "", ""));
    sendRows(REQ_LEADERS, arg, rows);
  });
}

function currentSeason() {
  var d = new Date();
  var y = d.getFullYear();
  return d.getMonth() >= 8 ? "" + y + (y + 1) : "" + (y - 1) + y;   // season starts in the fall
}

function sendPlayoffs(arg) {
  getJSON("playoff-series/carousel/" + currentSeason() + "/", 60, function (err, data) {
    var rounds = (!err && data && data.rounds) || [];
    var rows = ["!|60"];
    rounds.forEach(function (r) {
      var series = r.series || [];
      if (!series.length) return;
      rows.push("#|" + (r.roundLabel || ("Round " + r.roundNumber)).replace(/-/g, " "));
      series.forEach(function (s) {
        var top = s.topSeed || {}, bot = s.bottomSeed || {};
        var need = s.neededToWin || 4;
        var done = top.wins >= need || bot.wins >= need;
        var text = done ? ((top.wins > bot.wins ? top.abbrev : bot.abbrev) + " wins") :
                   top.wins === bot.wins ? (top.wins ? "Tied " + top.wins + "-" + bot.wins : "Not started") :
                   (top.wins > bot.wins ? top.abbrev : bot.abbrev) + " leads";
        rows.push(row("M", top.abbrev || "TBD", top.wins || 0, bot.abbrev || "TBD",
                      bot.wins || 0, text, "", "S"));
      });
    });
    if (rows.length === 1) {
      rows.push("#|Playoffs");
      rows.push(row("R", "No playoff series yet", "", "Check back in April", "", ""));
    }
    sendRows(REQ_PLAYOFFS, arg, rows);
  });
}

// ── Settings to the watch ─────────────────────────────────────────────────
// Includes the start time of your team's next game, so the watch can wake
// up and open the app at puck drop.
function sendSettings() {
  var msg = {
    FAV_TEAM: gSettings.fav,
    FOLLOW: gSettings.follow.join(","),
    VIBRATE: gSettings.vibrate ? 1 : 0,
    AUTO_OPEN: gSettings.autoOpen ? 1 : 0,
    WAKE_TIME: 0
  };
  if (!gSettings.wake) {
    send(msg);
    return;
  }
  getJSON("club-schedule-season/" + gSettings.fav + "/now", 600, function (err, sched) {
    var now = Date.now();
    var games = (!err && sched && sched.games) || [];
    for (var i = 0; i < games.length; i++) {
      var t = new Date(games[i].startTimeUTC).getTime();
      if (gameState(games[i]) === "P" && t > now + 60000) {
        msg.WAKE_TIME = Math.floor(t / 1000);
        break;
      }
    }
    send(msg);
  });
}

// ── Events ────────────────────────────────────────────────────────────────
Pebble.addEventListener("ready", function () {
  console.log("[NHL] ready, favorite " + gSettings.fav);
  sendSettings();
});

Pebble.addEventListener("appmessage", function (e) {
  var p = e.payload;
  var req = p.REQ, arg = p.ARG || "";
  if (req === undefined) return;
  switch (req) {
    case REQ_GAMES:     sendGames(arg); break;
    case REQ_GAME:      sendGame(arg); break;
    case REQ_STANDINGS: sendStandings(arg); break;
    case REQ_TEAM:      sendTeam(arg); break;
    case REQ_LEADERS:   sendLeaders(arg); break;
    case REQ_PLAYOFFS:  sendPlayoffs(arg); break;
    case REQ_SETTINGS:  sendSettings(); break;
    case REQ_SET_FAV:
      if (TEAMS[arg]) {
        gSettings.fav = arg;
        saveSettings(gSettings);
        sendSettings();
      }
      break;
  }
});

// ── Settings page (built here, no website needed) ─────────────────────────
function settingsPage() {
  var abbrs = Object.keys(TEAMS).sort();
  var opts = abbrs.map(function (a) {
    return "<option value='" + a + "'" + (a === gSettings.fav ? " selected" : "") + ">" +
           a + " - " + TEAMS[a] + "</option>";
  }).join("");
  var checks = abbrs.map(function (a) {
    return "<label class='team'><input type='checkbox' name='follow' value='" + a + "'" +
           (gSettings.follow.indexOf(a) >= 0 ? " checked" : "") + "> " + a + "</label>";
  }).join("");
  function toggle(id, label, on) {
    return "<label class='row'><span>" + label + "</span><input type='checkbox' id='" + id + "'" +
           (on ? " checked" : "") + "></label>";
  }
  return "<!DOCTYPE html><html><head><meta charset='utf-8'>" +
    "<meta name='viewport' content='width=device-width,initial-scale=1'>" +
    "<title>NHL Scores</title><style>" +
    "body{font-family:-apple-system,sans-serif;background:#111;color:#eee;margin:0;padding:16px}" +
    "h1{font-size:22px}h2{font-size:16px;color:#9ab;margin-top:24px}" +
    "select,button{width:100%;font-size:17px;padding:10px;border-radius:8px}" +
    "button{background:#1e6fd9;color:#fff;border:0;margin-top:24px}" +
    ".row{display:flex;justify-content:space-between;padding:10px 0;border-bottom:1px solid #333}" +
    ".grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}.team{padding:6px 0}" +
    "p{color:#999;font-size:14px}" +
    "</style></head><body><h1>NHL Scores</h1>" +
    "<h2>My Team</h2><select id='fav'>" + opts + "</select>" +
    "<h2>Alerts</h2>" +
    toggle("vibrate", "Vibrate on goals", gSettings.vibrate) +
    toggle("autoOpen", "Open my team's live game at launch", gSettings.autoOpen) +
    toggle("wake", "Open the app at puck drop", gSettings.wake) +
    "<h2>Also follow</h2><p>Get goal alerts for these teams too, while the app is open.</p>" +
    "<div class='grid'>" + checks + "</div>" +
    "<button id='save'>Save</button>" +
    "<script>document.getElementById('save').onclick=function(){" +
    "var f=[];var c=document.getElementsByName('follow');" +
    "for(var i=0;i<c.length;i++)if(c[i].checked)f.push(c[i].value);" +
    "var s={fav:document.getElementById('fav').value,follow:f," +
    "vibrate:document.getElementById('vibrate').checked," +
    "autoOpen:document.getElementById('autoOpen').checked," +
    "wake:document.getElementById('wake').checked};" +
    "location.href='pebblejs://close#'+encodeURIComponent(JSON.stringify(s));};</script>" +
    "</body></html>";
}

Pebble.addEventListener("showConfiguration", function () {
  Pebble.openURL("data:text/html;charset=utf-8," + encodeURIComponent(settingsPage()));
});

Pebble.addEventListener("webviewclosed", function (e) {
  if (!e || !e.response || e.response === "CANCELLED") return;
  try {
    var s = JSON.parse(decodeURIComponent(e.response));
    if (TEAMS[s.fav]) gSettings.fav = s.fav;
    gSettings.follow = (s.follow || []).filter(function (a) { return !!TEAMS[a]; });
    gSettings.vibrate = !!s.vibrate;
    gSettings.autoOpen = !!s.autoOpen;
    gSettings.wake = !!s.wake;
    saveSettings(gSettings);
    sendSettings();
  } catch (ex) {
    console.log("[NHL] settings error: " + ex);
  }
});
