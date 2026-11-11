#!/bin/bash
# F24 config mitigation: remove the stock 'test' account from group teeclnt on the Pi, so only root can open /dev/tee0.
# usage: harden_teeclnt.sh <pi-ip> [--restore]
#   (default)  check, back up /etc/group (on the Pi and next to this script's log), remove test from teeclnt, verify test is DENIED and root still works.
#   --restore  put test back into teeclnt from the saved copy.
# Idempotent. Touches only /etc/group on the Pi's rootfs. Does NOT change the image build (the stock optee_client_ext package creates the account).
PI="$1"; MODE="${2:-}"
S=(ssh -o BatchMode=yes -o ConnectTimeout=10 root@"$PI")
if [ "$MODE" = "--restore" ]; then
  "${S[@]}" 'cp /etc/group.pre-f24 /etc/group && echo restored && grep "^teeclnt" /etc/group'; exit $?
fi
"${S[@]}" '
echo "before: $(grep "^teeclnt" /etc/group)"; su test -c "head -c0 /dev/tee0" 2>/dev/null && echo "  user test: CAN open /dev/tee0" || echo "  user test: denied"
[ -f /etc/group.pre-f24 ] || cp -p /etc/group /etc/group.pre-f24
if grep -q "^teeclnt:.*[:,]test\(,\|$\)" /etc/group; then sed -i "s/^\(teeclnt:x:[0-9]*:\)test\(,\|$\)/\1/; s/^\(teeclnt:x:[0-9]*:.*\),test\(,\|$\)/\1\2/" /etc/group; sync; fi   # busybox delgroup cannot remove a single member
echo "after:  $(grep "^teeclnt" /etc/group)"
su test -c "head -c0 /dev/tee0" 2>/dev/null && echo "  user test: STILL CAN open /dev/tee0 (FAIL)" || echo "  user test: denied (PASS)"
head -c0 /dev/tee0 && echo "  root: can open /dev/tee0 (PASS)"
cat /etc/group.pre-f24 | grep "^teeclnt"
'
