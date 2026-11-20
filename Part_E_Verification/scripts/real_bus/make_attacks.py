import sys; sys.path.insert(0,"/home/aslam/Minor_Project/Part_E_Verification/tools/monitor_client")
import packet_gen as g
k=g.key_from_source(); out=["# step 6 attack run; DELAY_MS(before) ID DATA(48 bytes)\n"]
def P(c): return 0xC0DE0000|(c&0xFFFF)
for c in range(601,621): out.append(g.line(100,0x123,g.packet(k,0x123,c,P(c)),"valid %d"%c))
out.append(g.line(400,0x123,g.packet(k,0x123,621,P(621),True),"A: one tag bit flipped, counter 621 -> expect AUTH_FAIL"))
out.append(g.line(300,0x123,g.packet(k,0x123,615,P(615)),"B: old counter 615 (last accepted 620) -> expect REPLAY"))
out.append(g.line(300,0x123,g.packet(k,0x123,620,P(620)^0x00FF),"C: same counter 620, payload changed, valid tag -> expect CONFLICT"))
out.append(g.line(300,0x124,g.packet(k,0x124,621,P(621)),"D: wrong CAN id 0x124 -> expect MALFORMED (judged by the monitor)"))
out.append(g.line(300,0x123,g.packet(k,0x123,625,P(625)),"E: counter jump of 5 (620 -> 625) -> expect GAP"))
out.append(g.line(1000,0x123,g.packet(k,0x123,626,P(626)),"F: after 1 s of silence, valid 626 -> TRAFFIC timeout (detail 1) then TRAFFIC_RECOVERED"))
for c in range(627,637): out.append(g.line(100,0x123,g.packet(k,0x123,c,P(c)),"valid %d"%c))
open(sys.argv[1],"w").write("".join(out)); print("wrote",sys.argv[1],len(out)-1,"packets")
