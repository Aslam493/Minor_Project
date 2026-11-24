#!/bin/bash
# Cross-compiles the receiver's live viewer (tools/live_demo/packet_view.c) into build_scratch/live_demo/packet_view (aarch64, -Werror).
cd "$(dirname "$0")/.." || exit 1
D=build_scratch/live_demo; mkdir -p $D
TC=../optee_full/toolchains/aarch64/bin/aarch64-linux-gnu-
F="-std=gnu11 -Wall -Wextra -Werror -O2 -I../optee_full/optee_client/libteec/include -I../optee_full/optee_os/lib/libutee/include -Itools/monitor_client"
${TC}gcc $F -c tools/monitor_client/mon_client.c -o $D/mon_client.o && ${TC}gcc $F -o $D/packet_view tools/live_demo/packet_view.c $D/mon_client.o -L../optee_full/out-br/target/usr/lib -lteec -lpthread && echo "built $D/packet_view (0 warnings)"
