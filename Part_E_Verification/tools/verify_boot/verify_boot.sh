#!/bin/bash
# verify_boot.sh - read-only boot verification of the Raspberry Pi after a flash or a rollback
# (Component 1, subcomponent 1.5.8, step R5).
#
#   usage: verify_boot.sh <pi-address> [--expect-core1-pinned | --expect-stock]
#
# --expect-core1-pinned (default): the image has the Core 1 divert (Sep-18 step2 image and later):
#                                  CPU mask 0,2-3, "CPU1: failed to come online", Core 1 heartbeat rises.
# --expect-stock:                  an older image without the divert: all four CPUs online.
#
# Everything here only READS (dmesg, /sys, /proc, ps, and the harmless PTA status commands 1, 2, 12).
# It never arms the FIQ, never writes a chip register, never changes CAN settings.
# Exit status: 0 = no FAIL, 1 = at least one FAIL, 2 = cannot reach the Pi.

PI="${1:?usage: $0 <pi-address> [--expect-core1-pinned|--expect-stock]}"
MODE="${2:---expect-core1-pinned}"
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=8 "root@$PI")
fail=0; pass=0; skip=0

ok()   { printf "  PASS  %s\n" "$1"; pass=$((pass+1)); }
bad()  { printf "  FAIL  %s\n" "$1"; fail=$((fail+1)); }
skp()  { printf "  SKIP  %s\n" "$1"; skip=$((skip+1)); }
chk()  { if [ "$2" = 1 ]; then ok "$1"; else bad "$1"; fi; }
r()    { "${SSH[@]}" "$1" 2>&1; }

echo "verify_boot: $PI  mode=$MODE"
if ! "${SSH[@]}" true 2>/dev/null; then echo "  FAIL  cannot reach $PI over ssh"; exit 2; fi
ok "ssh reachable"

up=$(r 'cut -d" " -f1 /proc/uptime')
echo "  info  uptime ${up}s, kernel $(r 'uname -r')"
chk "kernel is 6.7.0-rc6" $([ "$(r 'uname -r')" = "6.7.0-rc6-v8" ] && echo 1 || echo 0)

dm=$(r 'dmesg')
rev=$(echo "$dm" | grep -m1 "optee: revision" )
echo "  info  ${rev:-no optee revision line}"
chk "OP-TEE reports its revision"           $([ -n "$rev" ] && echo 1 || echo 0)
chk "OP-TEE driver initialised"             $(echo "$dm" | grep -q "optee: initialized driver" && echo 1 || echo 0)
chk "no OP-TEE error in dmesg"              $(echo "$dm" | grep -iE "optee.*(fail|error|panic)" >/dev/null && echo 0 || echo 1)
chk "/dev/tee0 and /dev/teepriv0 exist"     $(r 'ls /dev/tee0 /dev/teepriv0 >/dev/null 2>&1 && echo 1 || echo 0')
chk "tee-supplicant is running"             $(r 'ps | grep -v grep | grep -c tee-supplicant' | awk '{print ($1>0)?1:0}')

on=$(r 'cat /sys/devices/system/cpu/online'); off=$(r 'cat /sys/devices/system/cpu/offline')
echo "  info  CPU online=$on offline=${off:-none}"
if [ "$MODE" = "--expect-stock" ]; then
  chk "all four CPUs online (stock image)"  $([ "$on" = "0-3" ] && echo 1 || echo 0)
else
  chk "CPU mask is 0,2-3 (Core 1 diverted)" $([ "$on" = "0,2-3" ] && echo 1 || echo 0)
  chk "CPU1 reported as failing to come online" $(echo "$dm" | grep -q "CPU1: failed to come online" && echo 1 || echo 0)
fi

h1=$(r 'spi_poc_test --core1-heartbeat 2>/dev/null | sed -n "s/.*heartbeat: //p"'); sleep 2
h2=$(r 'spi_poc_test --core1-heartbeat 2>/dev/null | sed -n "s/.*heartbeat: //p"')
echo "  info  Core 1 heartbeat $h1 -> $h2"
if [ "$MODE" = "--expect-stock" ]; then
  skp "Core 1 heartbeat (not applicable to a stock image)"
else
  chk "Core 1 heartbeat is rising"  $([ -n "$h1" ] && [ -n "$h2" ] && [ "$h2" -gt "$h1" ] 2>/dev/null && echo 1 || echo 0)
fi

fc=$(r 'spi_poc_test --fiq-control 2>/dev/null | sed -n "s/.*(live value): //p"')
echo "  info  FIQ_CONTROL=$fc"
chk "PTA answers (fiq-count)"        $(r 'spi_poc_test --fiq-count 2>/dev/null | grep -c "FIQ count"' | awk '{print ($1>0)?1:0}')
chk "FIQ is NOT armed after boot (FIQ_CONTROL 0x00000000)" $([ "$fc" = "0x00000000" ] && echo 1 || echo 0)

chk "can0 exists"                    $(r '[ -d /sys/class/net/can0 ] && echo 1 || echo 0')
chk "can0 is up"                     $([ "$(r 'cat /sys/class/net/can0/operstate 2>/dev/null')" = "up" ] && echo 1 || echo 0)
chk "mcp251xfd found the chip on can0" $(echo "$dm" | grep -q "mcp251xfd spi0.0 can0: MCP2518FD.*successfully initialized" && echo 1 || echo 0)
# NB: the driver banner contains "+CRC_REG +CRC_RX +CRC_TX" (feature flags), so match real error messages only
chk "no SPI CRC errors from the CAN driver" $(echo "$dm" | grep -iE "CRC (read|write) error|mcp251xfd.*CRC error" >/dev/null && echo 0 || echo 1)
if r '[ -x /tmp/canmode ] && echo y' | grep -q y; then
  cm=$(r '/tmp/canmode can0 | grep -E "nominal|ctrlmode ACTIVE|controller state"')
  echo "$cm" | sed 's/^/  info  /'
  chk "can0 state ERROR-ACTIVE"      $(echo "$cm" | grep -q "ERROR-ACTIVE" && echo 1 || echo 0)
else
  skp "canmode not on the Pi (/tmp); bitrate and mode not read"
fi
if r '[ -x /tmp/ta_roundtrip ] && echo y' | grep -q y; then
  chk "TA and PTA round trip (ta_roundtrip)" $(r '/tmp/ta_roundtrip >/dev/null 2>&1 && echo 1 || echo 0')
else
  skp "ta_roundtrip not on the Pi (/tmp); TA/PTA round trip not run"
fi

echo "verify_boot: $pass pass, $fail FAIL, $skip skipped"
[ "$fail" -eq 0 ]
