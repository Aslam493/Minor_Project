global-incdirs-y += .
srcs-y += main.c
srcs-y += rpi3_can_fiq.c
srcs-y += rpi3_can_server.c
srcs-$(CFG_RPI3_CAN_MONITOR) += rpi3_can_monitor.c
