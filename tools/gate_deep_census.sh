#!/bin/bash
# forensics gate: deploy patched ntdll + run the 90s Calculator gate + metrics.
# Lock protocol identical to /tmp/sr31_gate.sh (who=forensics-gate).
LOCK=/tmp/wa/wine.lock
INST=/var/tmp/wine-installs/wbappgate
BUILD=/var/tmp/wt-builds/merge-build
PREFIX=/var/tmp/wa-appgate/prefix
LOG=/var/tmp/res-runs/forensics_gate_deep.log
STASH=/var/tmp/res-runs/session_stash
RUNSECS=${1:-90}
SRC=$BUILD/dlls/ntdll/ntdll.so

acq() { for i in $(seq 1 1500); do mkdir "$LOCK" 2>/dev/null && { echo "pid=$$ who=forensics-gate" > "$LOCK/owner"; return 0; }; sleep 2; done; return 1; }
rel() { [ -f "$LOCK/owner" ] && grep -q "who=forensics" "$LOCK/owner" 2>/dev/null && { rm -f "$LOCK/owner"; rmdir "$LOCK" 2>/dev/null; }; }
restore() {
  mv "$STASH"/session.db* "$PREFIX/drive_c/users/olge/AppData/Local/" 2>/dev/null
  while IFS= read -r f; do
    relp="${f#$PREFIX}"; [ -f "$STASH/ebw$relp" ] || continue
    mkdir -p "$PREFIX$(dirname "$relp")" 2>/dev/null; mv "$STASH/ebw$relp" "$PREFIX$relp" 2>/dev/null
  done < "$STASH/ebw_list" 2>/dev/null
}
trap 'restore; rel' EXIT
acq || { echo "LOCK_TIMEOUT (owner: $(cat $LOCK/owner 2>/dev/null))"; exit 1; }

# ---- deploy (owner verified before copy) ----
if ! grep -q "who=forensics" "$LOCK/owner"; then echo "LOCK_OWNER_MISMATCH: $(cat $LOCK/owner)"; exit 1; fi
cp -a "$INST/lib/wine/x86_64-unix/ntdll.so" "$INST/lib/wine/x86_64-unix/ntdll.so.bak-p54" 2>/dev/null
cp -f "$SRC" "$INST/lib/wine/x86_64-unix/ntdll.so" || { echo "DEPLOY_FAILED"; exit 1; }
md5sum "$SRC" "$INST/lib/wine/x86_64-unix/ntdll.so"
echo "=== deployed ==="; /bin/ls -la "$INST/lib/wine/x86_64-unix/ntdll.so"

export WINEPREFIX="$PREFIX" WINEDLLPATH="$INST/lib/wine" DISPLAY=:97
export WINEDEBUG="-all,+seh,+win,+module,+resources,+mwr,+combase,+xaml,+virtual,+mrmcorer,+bcp47mrm,+loaddll"
unset WEBVIEW2_USER_DATA_FOLDER
mkdir -p "$STASH/ebw"
mv "$PREFIX/drive_c/users/olge/AppData/Local/"session.db* "$STASH"/ 2>/dev/null
: > "$STASH/ebw_list"
/usr/bin/find "$PREFIX/drive_c/users/olge/AppData/Local/EBWebView" \( -name "lockfile*" -o -name "*sadRecord*" \) -type f 2>/dev/null > "$STASH/ebw_list"
while IFS= read -r f; do
  relp="${f#$PREFIX}"; mkdir -p "$STASH/ebw$(dirname "$relp")"; mv "$f" "$STASH/ebw$relp" 2>/dev/null
done < "$STASH/ebw_list"
: > "$LOG"
timeout --foreground $RUNSECS "$INST/bin/wine" /var/tmp/calculator/CalculatorApp.exe </dev/null >/dev/null 2> >(cat > "$LOG") &
CPID=$!
( for t in 5 15 25 35 45 55 65 75 85 95 105 115 125 135 145 155 165 175 185 195; do [ $t -gt 5 ] && sleep 10; echo "$t $(/usr/bin/pidof -s wine 2>/dev/null) $(/usr/bin/pidof -s wine-preloader 2>/dev/null) $(/usr/bin/pidof -s CalculatorApp.exe 2>/dev/null) $(/usr/bin/pidof -s wine64 2>/dev/null)" >> /tmp/abi/deep_life.txt; done
  xwininfo -root -tree -display :97 > /tmp/abi/deep_xwin.txt 2>&1
  P=$(/usr/bin/pidof -s wine 2>/dev/null)
  echo "DBG PID=$P" >> /tmp/abi/deep_threads.txt
  if [ -n "$P" ]; then
    printf '+info threads\n+ex\nquit\n' > /tmp/abi/deep_cmds.txt
    timeout 90 "$INST/bin/winedbg" --no-gui $P < /tmp/abi/deep_cmds.txt >> /tmp/abi/deep_threads.txt 2>&1
  fi
  tail -c 4000000 "$LOG" > /tmp/abi/deep_logtail.txt 2>/dev/null ) &
sleep $RUNSECS
kill -9 "$CPID" 2>/dev/null || true
"$INST/bin/wineserver" -k 2>/dev/null || true
sleep 2

echo "=== log lines ==="; wc -l "$LOG"
echo "=== EXECUTE-AV loop count (info[0]=8) ==="; grep -ac "info\[0\]=0000000000000008" "$LOG"
echo "=== stack overflow lines ==="; grep -ac "stack overflow" "$LOG"
echo "=== protect 0x44 requests ==="; grep -ac "NtProtectVirtualMemory 0xffffffffffffffff .* 00000044" "$LOG"
echo "=== sample 0x44 request + following view dump ==="
N=$(grep -n "NtProtectVirtualMemory 0xffffffffffffffff .* 00000044" "$LOG" | head -1 | cut -d: -f1)
[ -n "$N" ] && sed -n "$N,$((N+6))p" "$LOG"
echo "=== AV histogram ==="; grep -oE "dispatch_exception code=c000000[0-9a-f]+" "$LOG" | sort | uniq -c | sort -rn | head
echo "=== c0000602 failfast ==="; grep -ac "c0000602" "$LOG"
echo "=== e0434352 managed ==="; grep -ac "e0434352" "$LOG"
echo "=== resources.core activations ==="; grep -acE "resources.core|ResourceContext" "$LOG"
echo "=== last 12 resources trace lines ==="; grep -a "trace:resources" "$LOG" | tail -12
echo "=== window present ==="; xwininfo -root -tree -display :97 2>/dev/null | grep -aci calculator
echo "=== toplevels ==="; xwininfo -root -tree -display :97 2>/dev/null | grep -E ": \('" | head -8
echo "=== alive at end? ==="; pgrep -fa CalculatorApp | head -2
echo "=== last 8 non-AV lines ==="; grep -a -v "dispatch_exception\|call_vectored\|call_seh" "$LOG" | tail -8
restore; rel; trap - EXIT
echo "GATE-FIX-DONE"
