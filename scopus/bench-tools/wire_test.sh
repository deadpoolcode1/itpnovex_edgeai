#!/usr/bin/env bash
# Decide, once and for all, whether the N6's bytes physically reach the modem.
# Stops sdvrApp so the UART is free, dumps raw bytes off /dev/ttyHS0 while the
# camera transmits, then restarts the app. Restart happens even on failure.
set -u
# ScopusQA #11: the modem login is site data, not source. It comes from
# scopus/bench.ini (untracked) via settings.py — bench.ini.template is the
# committed copy and carries placeholders only.
_SC="$(cd "$(dirname "$0")/.." && pwd)"
MODEM_USER=$(python3 -c "import sys;sys.path.insert(0,'$_SC/lib');from settings import S;print(S.get('modem','user'))")
MODEM_PASS=$(python3 -c "import sys;sys.path.insert(0,'$_SC/lib');from settings import S;print(S.require('modem','password'))")
MODEM_IP=$(python3   -c "import sys;sys.path.insert(0,'$_SC/lib');from settings import S;print(S.get('modem','ip'))")
M="sshpass -p $MODEM_PASS ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR $MODEM_USER@$MODEM_IP"
SP="$(cd "$(dirname "$0")" && pwd)"

APP=/legato/systems/current/bin/app
echo "=== stopping sdvrApp ==="
$M "$APP stop sdvrApp 2>&1 || true; sleep 1; fuser /dev/ttyHS0 2>&1 || echo 'ttyHS0 now free'"

echo "=== arming raw reader on /dev/ttyHS0 (8s) ==="
$M "stty -F /dev/ttyHS0 115200 raw -echo 2>/dev/null; timeout 8 cat /dev/ttyHS0 > /tmp/wire.bin 2>/dev/null; echo reader-done" &
READER=$!
sleep 2

echo "=== N6 transmits 4 frames ==="
timeout 45 python3 "$SP/n6.py" "mdm AT" "mdm AT" 2>&1 | grep -E "mdm:|OK" || true

wait $READER 2>/dev/null
echo "=== what landed on the modem's UART ==="
$M "ls -l /tmp/wire.bin; echo '--- hexdump ---'; od -An -tx1 /tmp/wire.bin 2>/dev/null | head -6; echo '--- size ---'; wc -c < /tmp/wire.bin"

echo "=== restarting sdvrApp ==="
$M "$APP start sdvrApp 2>&1 | tail -2; sleep 2; fuser /dev/ttyHS0 2>&1"
