#!/bin/bash
# series.sh TAG "kind kind ..."   Runs a series of bursts through the harness (burst_run.py), one run per kind, with a random 0-1 s wait before each run
# (random phase of the forwarder start), the forwarder started by the harness with --trace and --hblog, and fetches both files per run into baseline/.
# Example (the 30 x N=500 series of the final campaign):  K=""; for n in $(seq 30); do K="$K 500:retry"; done; QUIET=1 scripts/real_bus/series.sh s3_ "$K"
# Kinds: N:retry (burst with 4 planted frames), plain:N:retry (valid frames only), rate:N:DELAYS (paced), normal. See burst_run.py.
# Needs: RX (receiver ip, default below), SN; the tools in /tmp of the receiver (/tmp/can_forwarder_5 = the forwarder build, mon_client, canmode) and the
# sender scripts in /tmp/burst; the boot service S95can_forwarder must be STOPPED while this runs (two forwarders would answer the Secure World).
# Then report with:  python3 scripts/real_bus/series_report.py TAG COUNT
cd "$(dirname "$0")/../.." || exit 1
RX=${RX:-10.237.27.212}; export RX
S=${SCRATCH:-/tmp}
TAG=$1; i=0
for k in $2; do i=$((i+1)); j=$(python3 -c "import random;print(round(random.uniform(0,1),3))"); sleep $j
  STEP=$TAG$i NOCD=1 SUMMARYTAG=_$TAG$i FWD_SECONDS=${FWD_SECONDS:-36} FWD_BIN=/tmp/can_forwarder_5 FWD_ARGS="--rcvbuf 4194304 --trace /tmp/tr_$i.csv --hblog /tmp/hb_$i.csv" python3 scripts/real_bus/burst_run.py --runs $k --date ${DATE:-$(date +%Y%m%d)} > $S/$TAG$i.log 2>&1
  scp -q -o BatchMode=yes root@$RX:/tmp/tr_$i.csv baseline/trace_${DATE:-$(date +%Y%m%d)}_$TAG$i.csv; scp -q -o BatchMode=yes root@$RX:/tmp/hb_$i.csv baseline/hblog_${DATE:-$(date +%Y%m%d)}_$TAG$i.csv
  echo "$i $k jitter=$j done"
done
echo ALLDONE
