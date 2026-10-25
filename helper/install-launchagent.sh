#!/bin/bash
# Installs (or removes) the Desk Buddy helper as a macOS LaunchAgent: it starts when you log in
# and is restarted if it ever stops. Nothing here needs sudo.
#
#   ./install-launchagent.sh              install / reinstall
#   ./install-launchagent.sh --uninstall  remove
set -euo pipefail

LABEL="com.deskbuddy.helper"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/DeskBuddyHelper.log"
DOMAIN="gui/$(id -u)"

if [[ "${1:-}" == "--uninstall" ]]; then
  launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
  rm -f "$PLIST"
  echo "Removed $LABEL. (Your config.json and actions.json were left alone.)"
  exit 0
fi
if [[ -n "${1:-}" ]]; then
  echo "usage: $0 [--uninstall]" >&2
  exit 2
fi

if [[ "$(uname)" != "Darwin" ]]; then
  echo "This installs a macOS LaunchAgent - run it on your Mac." >&2
  exit 1
fi

NODE_BIN="$(command -v node || true)"
if [[ -z "$NODE_BIN" || ! -x "$NODE_BIN" ]]; then
  echo "Node.js was not found. Install it first (for example: brew install node) and run this again." >&2
  exit 1
fi
NODE_MAJOR="$("$NODE_BIN" -p 'process.versions.node.split(".")[0]')"
if (( NODE_MAJOR < 18 )); then
  echo "Node 18 or newer is needed (found $("$NODE_BIN" -v))." >&2
  exit 1
fi

if [[ ! -f "$HERE/config.json" ]]; then
  echo "No config.json yet. First run:  node \"$HERE/buddy-helper.js\" --init" >&2
  exit 1
fi
# Refuse to install a helper that would crash on start (bad token, broken actions.json, ...).
"$NODE_BIN" "$HERE/buddy-helper.js" --check >/dev/null

mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"

# perl ships with macOS; it keeps unusual characters in paths from breaking the XML.
NODE="$NODE_BIN" HELPER="$HERE" LOGFILE="$LOG" perl -pe '
  sub x { my $s = shift; $s =~ s/&/&amp;/g; $s =~ s/</&lt;/g; $s =~ s/>/&gt;/g; return $s; }
  s/__NODE__/x($ENV{NODE})/ge; s/__HELPER__/x($ENV{HELPER})/ge; s/__LOG__/x($ENV{LOGFILE})/ge;
' "$HERE/$LABEL.plist.template" > "$PLIST.tmp"
plutil -lint "$PLIST.tmp" >/dev/null
mv "$PLIST.tmp" "$PLIST"

launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
launchctl bootstrap "$DOMAIN" "$PLIST"
launchctl kickstart -k "$DOMAIN/$LABEL"

echo "Installed. The helper now starts at login."
echo "  log:      tail -f \"$LOG\""
echo "  status:   launchctl print $DOMAIN/$LABEL | head"
echo "  remove:   $0 --uninstall"
echo
echo "macOS may ask whether 'node' may accept incoming connections and find devices on your local network."
echo "Click Allow - the buddy could not reach the helper otherwise."
