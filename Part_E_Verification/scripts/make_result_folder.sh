#!/bin/bash
# make_result_folder.sh <evidence_raw.txt>  - builds result/: explanation (md+pdf), evidence file (md+pdf), raw evidence, README.
set -e
RAW="${1:?raw evidence file}"; CB="${2:-}"; PRE="${3:-}"; PV="$(cd "$(dirname "$0")/.." && pwd)"; R="$PV/result"; T=$(mktemp -d)
CSS="$T/pdf.css"; cat > "$CSS" <<'C'
<style>
body{font-family:Helvetica,Arial,sans-serif;max-width:980px;margin:18px auto;font-size:11.5px;line-height:1.35;color:#111}
h1{font-size:22px;border-bottom:2px solid #333;padding-bottom:4px} h2{font-size:17px;margin-top:22px;border-bottom:1px solid #999} h3{font-size:14px;margin-top:18px}
table{border-collapse:collapse;width:100%;margin:8px 0} td,th{border:1px solid #888;padding:3px 5px;vertical-align:top} th{background:#e8e8e8}
pre{background:#f4f4f4;border:1px solid #ccc;padding:6px;font-size:8.2px;line-height:1.25;white-space:pre-wrap;word-break:break-all}
code{font-size:10.5px;background:#f0f0f0;padding:0 2px} pre code{background:none;font-size:8.2px}
</style>
C
mkdir -p "$R"
cp "$PV/PROJECT_EXPLANATION.md" "$R/1_PROJECT_EXPLANATION.md"
cp "$RAW" "$R/3_raw_evidence_output.txt"
if [ -n "$CB" ] || [ -n "$PRE" ]; then   # optional second raw file: the canbridge board test becomes section [C9]
  python3 - "$RAW" "$CB" "$PRE" "$R/3_raw_evidence_output.txt" <<'PY'
import sys,re
raw=open(sys.argv[1],errors="replace").read()
i=raw.index("\n"+"#"*64+"\n## [END]")
add=""
for path,title in ((sys.argv[2],"[C9] The packet path through the CAN controller (canbridge): can_sender -> can0 -> can_forwarder -> monitor"),
                   (sys.argv[3],"[C10] Packets made before Linux on the laptop (packet_gen.py), replayed through the CAN controller: can_replay -> can0 -> can_forwarder -> monitor")):
    if path:
        add+="\n"+"#"*64+"\n## "+title+"\n"+"#"*64+"\n\n"+open(path,errors="replace").read().strip("\n")+"\n"
open(sys.argv[4],"w").write(raw[:i]+add+raw[i:])
PY
fi
python3 "$PV/scripts/build_evidence_md.py" "$R/3_raw_evidence_output.txt" "$R/2_EVIDENCE_FILE.md"
for f in 1_PROJECT_EXPLANATION 2_EVIDENCE_FILE; do
  t=$(head -1 "$R/$f.md" | sed 's/^# //')
  pandoc "$R/$f.md" -s --metadata pagetitle="$t" -H "$CSS" -o "$T/$f.html"
  google-chrome --headless --no-sandbox --disable-gpu --no-pdf-header-footer --print-to-pdf="$R/$f.pdf" "$T/$f.html" >/dev/null 2>&1
done
rm -rf "$T"
ls -la "$R"
