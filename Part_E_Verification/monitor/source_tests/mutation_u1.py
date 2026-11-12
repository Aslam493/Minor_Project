import subprocess, shutil, sys, re, os
OS='/home/aslam/Minor_Project/optee_full/optee_os'; PL=OS+'/core/arch/arm/plat-rpi3'; SC=sys.argv[1]; HERE=os.getcwd()
src=open(PL+'/rpi3_can_monitor.c').read()
def sub(a,b,s=src,count=1):
    assert s.count(a)>=1, "pattern missing: "+a; return s.replace(a,b,count)
def cmd23_skip_eval(s):
    i=s.index("enum rpi3_mon_res rpi3_mon_cmd23"); j=s.index("rpi3_mon_evaluate(l, now);", i); return s[:j]+"/* MUTANT: no evaluate */"+s[j+len("rpi3_mon_evaluate(l, now);"):]
muts={
 "M1 expired() uses >= (boundary off by one, early)": lambda s: sub("return now >= t0 && now - t0 > limit;","return now >= t0 && now - t0 >= limit;",s),
 "M2 expired() uses > limit+1 (boundary late by one tick)": lambda s: sub("return now >= t0 && now - t0 > limit;","return now >= t0 && now - t0 > limit + 1;",s),
 "M3 heartbeat TIMEOUT only when miss_count == k": lambda s: sub("l->hb_miss_count >= RPI3_MON_K_MISS","l->hb_miss_count == RPI3_MON_K_MISS",s),
 "M4 command 23 does not evaluate the deadline itself": cmd23_skip_eval,
 "M5 a missed request is counted again by every Core 1 pass": lambda s: sub("l->hb_state == RPI3_MON_HB_OPEN && rpi3_mon_expired(now, l->hb_issue_ticks, RPI3_MON_D_REQ_TICKS)","l->hb_state != RPI3_MON_HB_ANSWERED && rpi3_mon_expired(now, l->hb_issue_ticks, RPI3_MON_D_REQ_TICKS)",s),
}
for k,(name,f) in enumerate(muts.items(),1):
    d=f"{SC}/m{k}"; shutil.rmtree(d,ignore_errors=True); shutil.copytree(PL,d)
    open(d+'/rpi3_can_monitor.c','w').write(f(src))
    exe=f"{SC}/h{k}"
    cc=["cc","-std=gnu11","-Wall","-Wextra","-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer","-pthread","-I","mock","-I",d,"-I",OS+"/lib/libutee/include","-I",".","-Wno-misleading-indentation","harness.c","mock_platform.c",OS+"/core/pta/rpi3_can_monitor_pta.c","-o",exe]
    r=subprocess.run(cc,capture_output=True,text=True)
    if r.returncode: print(name,"-> BUILD FAILED",r.stderr[:200]); continue
    out=subprocess.run([exe],capture_output=True,text=True).stdout
    failed=[l for l in out.splitlines() if l.startswith("FAIL")]
    new=[l.split()[1] for l in failed if re.match(r'FAIL (T11b|T11a|T10b)',l)]
    tot=[l for l in out.splitlines() if l.startswith("total:")]
    print(f"{name}\n    {tot[0] if tot else '?'} | failing tests: {len(failed)} | caught by the NEW tests: {new if new else 'NO'} | all failing: {[l[:40] for l in failed][:4]}")
