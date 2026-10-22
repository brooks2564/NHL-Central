#!/usr/bin/env bash
# Build the app, install it in the Pebble emulator and open it.
#
#   ./scripts/run_emulator.sh [emery|gabbro]     (default: emery)
#
# `pebble install --emulator` installs the app but doesn't open it, so this
# also tells the emulated watch to start it.
set -euo pipefail

PLATFORM="${1:-emery}"
cd "$(dirname "$0")/.."

UUID="$(python3 -c "import json; print(json.load(open('package.json'))['pebble']['uuid'])")"

pebble build
pebble install --emulator "$PLATFORM"
sleep 2

pebble repl --emulator "$PLATFORM" >/dev/null <<EOF
from uuid import UUID
from libpebble2.protocol.apps import AppRunState, AppRunStateStart
pebble.send_packet(AppRunState(data=AppRunStateStart(uuid=UUID('$UUID'))))
EOF

echo "Opened NHL Central on the $PLATFORM emulator."
