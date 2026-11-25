#!/bin/bash
# demo_live_stream.sh - ONE command for the live demo: opens two big terminal windows at the same time,
#   TOP    = SENDER   (a new signed packet every 0.1 s, forever; keys t r d c w j a p + - q work in THIS window)
#   BOTTOM = RECEIVER (every packet and the Secure World's decision, coloured; status bar with the Secure World totals)
# It first prepares everything (key, counter, files), starts the receiver viewer, and 3 seconds later the sender stream.
#   bash scripts/demo_live_stream.sh            automatic attacks ON (one every 5 to 8 s, all six types in turn) AND you can press the keys yourself
#   bash scripts/demo_live_stream.sh --quiet    only valid packets (green) until you press an attack key
# Stop: press q in the sender window, Ctrl+C in the receiver window. Afterwards (optional): python3 scripts/demo_stream_setup.py --remove-key
# Environment: RX=<receiver ip> SN=<sender ip> COLS=170 ROWS=22 (window size in characters). TESTSEC=N runs a short logged test (windows close by themselves).
cd "$(dirname "$0")/.." || exit 1
# the addresses come from scripts/pi_addresses.env (edit that file); RX=... SN=... typed in front of the command wins
_RX0=$RX; _SN0=$SN; . "$(dirname "$0")/pi_addresses.env"; [ -n "$_RX0" ] && RX=$_RX0; [ -n "$_SN0" ] && SN=$_SN0
export RX SN
COLS=${COLS:-170}; ROWS=${ROWS:-22}
python3 scripts/demo_stream_setup.py || { echo "setup failed: both Pis must be on (see the message above)"; exit 1; }
ARGS="$*"; case "$ARGS" in *--quiet*) ARGS="${ARGS//--quiet/}";; *) ;; esac
if [ -n "$TESTSEC" ]; then
  RXCMD="ssh -t -o BatchMode=yes root@$RX '/tmp/packet_view' 2>&1 | tee /tmp/live_rx.log"
  TXCMD="ssh -t -o BatchMode=yes sender@$SN 'cd /tmp/demo && python3 stream.py --duration $TESTSEC --inject t:4,r:8,d:12,c:16,w:20 $ARGS' 2>&1 | tee /tmp/live_tx.log"
  END=""
else
  RXCMD="ssh -t -o BatchMode=yes root@$RX '/tmp/packet_view'"
  TXCMD="ssh -t -o BatchMode=yes sender@$SN 'cd /tmp/demo && python3 stream.py $ARGS'"
  END="; echo; echo '[stopped: press Enter to close this window]'; read"
fi
gnome-terminal --window --title="RECEIVER: the judge (Secure World), every packet and its colour" --geometry=${COLS}x${ROWS}+40+560 -- bash -c "$RXCMD $END" &
sleep 3
gnome-terminal --window --title="SENDER: a new packet every 0.1 s  (keys: t r d c w j a p + - q)" --geometry=${COLS}x${ROWS}+40+40 -- bash -c "$TXCMD $END" &
wait
