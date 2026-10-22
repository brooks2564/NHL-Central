# NHL Scores 🏒

A Pebble watch **app** for live NHL scores, standings and stats. It's the
companion to the NHL Live watchface: the watchface shows your team at a
glance, and this app lets you browse the whole league.

## Screens

- **My Team**: your team's page, with today's game, record, division place,
  next games and recent results
- **Today's Games**: every game today with live scores. Pick one for details.
- **Teams**: all 32 teams by division
- **Standings**: by division, wild card (East and West), conference or league
- **Playoffs**: series scores for each round
- **League Leaders**: points, goals, assists, plus/minus and goalie stats

The game screen shows the score, period and clock, power plays, shots, goals
by period with assists, and the goalies.

## Controls

Use the buttons or the touchscreen:

| Action        | Buttons      | Touch        |
|---------------|--------------|--------------|
| Scroll        | Up / Down    | Swipe        |
| Open          | Select       | Tap          |
| Go back       | Back         | Swipe right  |
| Make my team  | Hold Select  | (use the "Make ... my team" row) |

Touch uses Pebble's built-in touch navigation. If touch doesn't respond, check
that touch navigation is turned on in the watch's settings.

## Goal alerts

While the app is open, the watch vibrates and flashes a goal light when your
team or any team you follow scores. You can also have the app open itself at
puck drop for your team's next game.

## Settings (in the Pebble phone app)

My team, teams to follow, vibrate on goals, open the live game at launch, and
open the app at puck drop.

## Platforms

Emery (Pebble Time 2) and Gabbro (Pebble Round 2)

## Building

Needs a recent Pebble SDK (4.33 or newer, for touch navigation):

```
pebble build
pebble install --emulator emery
```

## How it works

- `src/pkjs/index.js` runs on the phone. It fetches data from the NHL's free
  web API (`api-web.nhle.com`) and sends each screen to the watch as simple
  text rows.
- `src/c/` is the watch side. Every screen is the same list screen
  (`list_window.c`), which draws the rows it receives.

## Author

Brooman Inks
