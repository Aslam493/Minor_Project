#!/bin/bash
# Object-code inspection of the compiled monitor object (stage 1): the call targets of each function, and a check that
# nothing forbidden (memcpy, memcmp, memset, heap, printing, crypto) is called by the lock and health functions.
# usage: objcheck.sh <rpi3_can_monitor.o>
set -u
O="${1:?usage: $0 <rpi3_can_monitor.o>}"
OD="$(cd "$(dirname "$0")/../../.." && pwd)/optee_full/toolchains/aarch64/bin/aarch64-linux-gnu-objdump"
FORBID='^(memcpy|memcmp|memset|memmove|malloc|calloc|realloc|free|trace_printf|trace_printf_helper|trace_ext_puts|__stack_chk_fail_dummy|crypto_[a-z_0-9]+|mobj_[a-z_0-9]+|cpu_spin_lock|cpu_spin_lock_xsave|__cpu_spin_lock|expired)$'
fail=0
"$OD" -dr --no-show-raw-insn "$O" | awk '
  /^[0-9a-f]+ <[^>]+>:$/ { fn=$2; gsub(/[<>:]/,"",fn); next }
  /R_AARCH64_(CALL26|JUMP26)/ { sym=$NF; sub(/[+-]0x[0-9a-f]+$/,"",sym); print fn, sym }
' | sort -u > /tmp/objcheck_calls.$$
for f in rpi3_mon_acquire rpi3_mon_release rpi3_mon_health_fill rpi3_mon_health_zero rpi3_mon_wait_init rpi3_mon_init_ok rpi3_mon_init; do
  calls=$(awk -v f="$f" '$1==f{print $2}' /tmp/objcheck_calls.$$ | tr '\n' ' ')
  bad=$(awk -v f="$f" '$1==f{print $2}' /tmp/objcheck_calls.$$ | grep -E "$FORBID" | grep -v '^$' | tr '\n' ' ')
  # memset and the single EMSG trace call are expected ONLY in rpi3_mon_init
  if [ "$f" = rpi3_mon_init ]; then bad=$(echo "$bad" | sed -e 's/\bmemset\b//g' -e 's/\btrace_printf_helper\b//g' -e 's/\btrace_printf\b//g'); fi
  if [ -n "$(echo $bad)" ]; then printf "  FAIL  %-22s forbidden call(s): %s\n" "$f" "$bad"; fail=1; else printf "  ok    %-22s calls: %s\n" "$f" "${calls:-(none)}"; fi
done
rm -f /tmp/objcheck_calls.$$
exit $fail
