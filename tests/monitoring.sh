#!/bin/sh
# Requires an AMD GPU, no running daemon, and an unprivileged user.
# Run: sh tests/monitoring.sh /path/to/target/radeon-profile
set -eu
[ "$(id -u)" != 0 ]
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
status=0
QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$tmp" timeout 8s "$1" > "$tmp/gui.log" 2>&1 || status=$?
[ "$status" = 124 ] || { grep '^' "$tmp/gui.log"; exit 1; }
grep -q 'Card detected:' "$tmp/gui.log"
grep -q 'Handling found device features' "$tmp/gui.log"
if grep -Eq 'No such signal (QComboBox|QButtonGroup)|No matching signal|No cards found|Daemon connected' "$tmp/gui.log"; then
    grep '^' "$tmp/gui.log"
    exit 1
fi
printf 'Daemon-free GPU initialization passed.\n'
