#!/bin/bash
set -u
LOCK=/tmp/wa/wine.lock
INST=/var/tmp/wine-installs/wbappgate
PREFIX=/var/tmp/wa-appgate/prefix
STASH=/var/tmp/res-runs/session_stash
for i in $(seq 1 1500); do mkdir "$LOCK" 2>/dev/null && { echo "pid=$$ who=mcg-probe129" > "$LOCK/owner"; break; }; sleep 2; done
[ -f "$LOCK/owner" ] && grep -q "who=mcg" "$LOCK/owner" || { echo "LOCK_TIMEOUT"; exit 1; }
mv "$STASH"/session.db* "$PREFIX/drive_c/users/olge/AppData/Local/" 2>/dev/null
while IFS= read -r f; do relp="${f#$PREFIX}"; [ -f "$STASH/ebw$relp" ] || continue; mkdir -p "$PREFIX$(dirname "$relp")" 2>/dev/null; mv "$STASH/ebw$relp" "$PREFIX$relp" 2>/dev/null; done < "$STASH/ebw_list" 2>/dev/null
pkill -9 -x CalculatorApp.e 2>/dev/null; sleep 1
export WINEPREFIX="$PREFIX" WINEDLLPATH="$INST/lib/wine" DISPLAY=:97
export WINEDEBUG="-all"
unset WEBVIEW2_USER_DATA_FOLDER
WPP=/tmp/abi/probe129_pp
rm -rf "$WPP"; mkdir -p "$WPP"
"$INST/bin/wineserver" -w0 &
WSPID=$!
sleep 1
"$INST/bin/wine" /var/tmp/calculator/CalculatorApp.exe >/dev/null 2>/tmp/abi/probe129_winelog.txt &
GP=$!
PID=""
for i in $(seq 1 40000); do
  PID=$(pgrep -x wine-preloader | head -1)
  [ -z "$PID" ] && PID=$(pgrep -x wine | head -1)
  [ -z "$PID" ] && PID=$(pgrep -x CalculatorApp.e | head -1)
  [ -n "$PID" ] && break
  sleep 0.002
done
[ -z "$PID" ] && { echo "NO PID"; kill $WSPID 2>/dev/null; rm -f "$LOCK/owner"; rmdir "$LOCK" 2>/dev/null; exit 1; }
echo "PID=$PID"
( for i in $(seq 1 60); do sleep 1; WID=$(xdotool search --name "Program Error" 2>/dev/null | head -1); [ -n "$WID" ] && xdotool windowactivate --sync $WID key Escape 2>/dev/null; done ) &
DB=$!
timeout 60 gdb -batch -nx -x /tmp/abi/probe129.gdb -p $PID > /tmp/abi/probe129.out 2>&1
kill $DB 2>/dev/null
kill -9 $GP 2>/dev/null
pkill -9 -x CalculatorApp.e 2>/dev/null
kill $WSPID 2>/dev/null
rm -f "$LOCK/owner"; rmdir "$LOCK" 2>/dev/null
echo PROBE122-FINISHED
