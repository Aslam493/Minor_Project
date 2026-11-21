#!/bin/bash
# watch_live.sh [prepare|attacks|print]       (no argument = prepare)
#   prepare : (1) copy the tools to the receiver's /tmp with one scp, (2) read last_counter from the monitor (read-only), (3) make the numbered packet files on
#             the LAPTOP with tools/monitor_client/watch_packets.py (the prototype key is read there and never written to a file), (4) copy them and
#             laptop_can_send.py to the sender's /tmp/watch/, (5) print the commands of WATCH_IT_YOURSELF.md.
#   attacks : same, but only files 2..5, counted from the monitor's CURRENT last_counter (use it after a stopped or repeated step).
#   print   : only print the commands of WATCH_IT_YOURSELF.md. Touches nothing, needs no network.
# Addresses (DHCP, they change): RX = receiver, default 10.237.27.212; SN = sender, default 10.237.27.144.  Example: RX=10.0.0.5 SN=10.0.0.6 bash scripts/watch_live.sh prepare
# Needs ssh key login as root@RX and sender@SN. Changes only RAM files in /tmp on the Pis; no card is written, nothing is rebooted.
set -u
RX=${RX:-10.237.27.212}; SN=${SN:-10.237.27.144}
HERE="$(cd "$(dirname "$0")" && pwd)"; PV="$(cd "$HERE/.." && pwd)"; DOC="$PV/WATCH_IT_YOURSELF.md"; MC="$PV/tools/monitor_client"
SSH="ssh -o BatchMode=yes -o ConnectTimeout=8"; SCP="scp -q -o BatchMode=yes -o ConnectTimeout=8"

# the commands = the headings and the ```sh blocks of WATCH_IT_YOURSELF.md (single source of truth), with the chosen addresses
print_cmds() {
  awk '/^## /||/^### /{h=$0; next} /^```sh/{f=1; next} /^```/{f=0; next} f{ if (h!="") {print "\n" h; h=""}; print "    " $0 }' "$DOC" \
    | sed "s/10\.237\.27\.77/$RX/g; s/10\.237\.27\.144/$SN/g"
}
copy_tools() {
  $SCP "$MC/mon_client" "$MC/can_forwarder" "$PV/tools/canmode/canmode" "$PV/tools/canutils/candump_aarch64" root@"$RX":/tmp/ \
    && $SSH root@"$RX" 'chmod +x /tmp/mon_client /tmp/can_forwarder /tmp/canmode /tmp/candump_aarch64'
}
last_counter() { $SSH root@"$RX" '/tmp/mon_client --retries 20 status 0' 2>/dev/null | grep -o 'last_counter [0-9]*' | head -1 | cut -d' ' -f2; }

case "${1:-prepare}" in
  print) print_cmds ;;
  prepare|attacks)
    mode="${1:-prepare}"
    copy_tools || { echo "could not copy the tools to root@$RX: is the receiver up, and is that still its address?" >&2; exit 2; }
    L=$(last_counter); case "$L" in ''|*[!0-9]*) echo "could not read last_counter from the monitor on $RX" >&2; exit 2;; esac
    T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
    if [ "$mode" = prepare ]; then python3 "$MC/watch_packets.py" "$T" --healthy-last "$L" || exit 2
    else python3 "$MC/watch_packets.py" "$T" --attacks-last "$L" || exit 2; fi
    $SSH sender@"$SN" 'mkdir -p /tmp/watch' && $SCP "$T"/*.txt "$MC/laptop_can_send.py" sender@"$SN":/tmp/watch/ \
      || { echo "could not copy the packet files to sender@$SN: is it up, and is that still its address?" >&2; exit 2; }
    echo "monitor last_counter was $L; tools copied to $RX:/tmp, packet files and laptop_can_send.py copied to $SN:/tmp/watch/"
    print_cmds ;;
  *) sed -n 2,9p "$0"; exit 1 ;;
esac
