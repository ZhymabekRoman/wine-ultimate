set -u
PREFIX=/var/tmp/wa-appgate/prefix
INST=/var/tmp/wine-installs/wbappgate
export WINEPREFIX="$PREFIX" WINEDLLPATH="$INST/lib/wine" DISPLAY=:97
export WINEDEBUG="-all,+resources,+mwr"
unset WEBVIEW2_USER_DATA_FOLDER

LOCK=/tmp/wa/wine.lock
for i in $(seq 1 900); do
  mkdir -p "$LOCK" 2>/dev/null
  ( set -o noclobber; echo "pid=$$ who=mcg-probe117" > "$LOCK/owner" ) 2>/dev/null && break
  sleep 2
done
[ -f "$LOCK/owner" ] && grep -q "who=mcg" "$LOCK/owner" || { echo "LOCK_TIMEOUT"; exit 1; }

STASH=/var/tmp/res-runs/session_stash
mv "$STASH"/session.db* "$PREFIX/drive_c/users/olge/AppData/Local/" 2>/dev/null
while IFS= read -r f; do
  relp="${f#$PREFIX}"; [ -f "$STASH/ebw$relp" ] || continue
  mkdir -p "$PREFIX$(dirname "$relp")" 2>/dev/null; mv "$STASH/ebw$relp" "$PREFIX$relp" 2>/dev/null
done < "$STASH/ebw_list" 2>/dev/null

pkill -9 -x CalculatorApp.e 2>/dev/null
sleep 2
"$INST/bin/wine" /var/tmp/calculator/CalculatorApp.exe >/dev/null 2>/tmp/abi/probe117_winelog.txt &
GP=$!
PID=""
for i in $(seq 1 40000); do
  for P in $(pgrep -x CalculatorApp.e); do
    grep -q SharedLibrary.dll /proc/$P/maps 2>/dev/null && { PID=$P; break; }
  done
  [ -n "$PID" ] && break
  sleep 0.002
done
if [ -z "$PID" ]; then
  echo "NO PID"
  rm -f "$LOCK/owner"; rmdir "$LOCK" 2>/dev/null; exit 1
fi
MB=$(awk '/mrt100_app\.dll/{print $1; exit}' /proc/$PID/maps | cut -d- -f1)
SB=$(awk '/SharedLibrary\.dll/{print $1; exit}' /proc/$PID/maps | cut -d- -f1)
CB=$(awk '/combase\.dll/{print $1; exit}' /proc/$PID/maps | cut -d- -f1)
echo "PID=$PID MB=0x$MB SB=0x$SB CB=0x$CB"
cp /proc/$PID/maps /tmp/abi/probe117_maps.txt 2>/dev/null
sed -e "s/@MB@/0x$MB/g" -e "s/@SB@/0x$SB/g" -e "s/@CB@/0x$CB/g" /tmp/abi/probe117.gdb.tpl > /tmp/abi/probe117.gdb
( for i in $(seq 1 60); do
    sleep 1
    WID=$(xdotool search --name "Program Error" 2>/dev/null | head -1)
    [ -n "$WID" ] && xdotool windowactivate --sync $WID key Escape 2>/dev/null && echo "dismissed $i t=$(date +%s.%N)" >> /tmp/abi/probe117_dismiss.txt
  done ) &
DB=$!
timeout 60 gdb -p $PID -batch -x /tmp/abi/probe117.gdb > /tmp/abi/probe117.out 2>&1
echo "GDB-DONE rc=$?"
kill $DB 2>/dev/null
kill -9 $GP 2>/dev/null
pkill -9 -x CalculatorApp.e 2>/dev/null
rm -f "$LOCK/owner"; rmdir "$LOCK" 2>/dev/null
echo PROBE100-FINISHED
