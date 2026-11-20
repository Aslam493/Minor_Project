#!/usr/bin/env python3
# 1000 CAN-FD frames, 48 bytes, BRS, id 0x123, 100 frames/s, 4-byte big-endian counter in bytes 0..3 (rest = pattern).
import socket, struct, time, sys
N=int(sys.argv[1]) if len(sys.argv)>1 else 1000; RATE=float(sys.argv[2]) if len(sys.argv)>2 else 100.0
s=socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
s.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FD_FRAMES, 1)
s.bind(("can0",))
t0=time.perf_counter(); dt=1.0/RATE; late=0
for i in range(N):
    data=struct.pack(">I",i)+bytes((i+j)&0xFF for j in range(44))
    frame=struct.pack("=IBB2x",0x123,48,1)+data+bytes(16)   # struct canfd_frame is 72 bytes: id,len,flags,2 pad,64 data
    due=t0+i*dt
    while time.perf_counter()<due: pass
    if time.perf_counter()-due>dt: late+=1
    s.send(frame)
print("sent",N,"in %.3f s"%(time.perf_counter()-t0),"late(>1 period):",late)
