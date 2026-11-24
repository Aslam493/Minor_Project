#!/bin/bash
# demo_live_stream.sh - ONE command for the live demo: opens two big terminal windows at the same time,
#   TOP    = SENDER   (a new signed packet every 0.1 s, forever; keys t r d c w j a p + - q work in THIS window)
#   BOTTOM = RECEIVER (every packet and the Secure World's decision, coloured; status bar with the Secure World totals)
# It first prepares everything (key, counter, files), starts the receiver viewer, and 3 seconds later the sender stream.
#   bash scripts/demo_live_stream.sh            normal
#   bash scripts/demo_live_stream.sh --auto     the sender also attacks by itself every 6 to 10 s
# Stop: press q in the sender window, Ctrl+C in the receiver window. Afterwards (optional): python3 scripts/demo_stream_setup.py --remove-key
# Environment: RX=<receiver ip> SN=<sender ip> COLS=170 ROWS=22 (window size in characters). TESTSEC=N runs a short logged test (windows close by themselves).
cd "$(dirname "$0")/.." || exit 1
export RX=${RX:-10.237.27.212} SN=${SN:-10.237.27.144}
COLS=${COLS:-170}; ROWS=${ROWS:-22}
python3 scripts/demo_stream_setup.py || { echo "setup failed: both Pis must be on (see the message above)"; exit 1; }
if [ -n "$TESTSEC" ]; then
  RXCMD="ssh -t -o BatchMode=yes root@$RX '/tmp/packet_view' 2>&1 | tee /tmp/live_rx.log"
  TXCMD="ssh -t -o BatchMode=yes sender@$SN 'cd /tmp/demo && python3 stream.py --duration $TESTSEC --inject t:4,r:8,d:12,c:16,w:20 $*' 2>&1 | tee /tmp/live_tx.log"
  END=""
else
  RXCMD="ssh -t -o BatchMode=yes root@$RX '/tmp/packet_view'"
  TXCMD="ssh -t -o BatchMode=yes sender@$SN 'cd /tmp/demo && python3 stream.py $*'"
  END="; echo; echo '[stopped: press Enter to close this window]'; read"
fi
gnome-terminal --window --title="RECEIVER: the judge (Secure World), every packet and its colour" --geometry=${COLS}x${ROWS}+40+560 -- bash -c "$RXCMD $END" &
sleep 3
gnome-terminal --window --title="SENDER: a new packet every 0.1 s  (keys: t r d c w j a p + - q)" --geometry=${COLS}x${ROWS}+40+40 -- bash -c "$TXCMD $END" &
wait
