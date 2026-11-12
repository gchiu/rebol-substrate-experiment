#!/usr/bin/env python3
"""Small fixed-workload timings for B13; process startup is included and this is
not a substitute for the DEFLATE measurements."""
import json, os, statistics, subprocess, time
HERE=os.path.dirname(os.path.abspath(__file__)); ROOT=os.path.abspath(os.path.join(HERE,'..','..'))
FIZZ=os.environ.get('FIZZBUZZ_DIR','/tmp/opencode/glon-fizzbuzz/experiments/glon-fizzbuzz')
apps=[]
def measure(name, cwd, cmd, n=5):
    vals=[]; outs=[]
    for _ in range(n):
        t=time.perf_counter(); p=subprocess.run(cmd,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120); vals.append(time.perf_counter()-t)
        if p.returncode: raise SystemExit(name+' failed: '+p.stderr.decode()[-300:])
        outs.append(p.stdout)
    return {'name':name,'median_ms':statistics.median(vals)*1000,'min_ms':min(vals)*1000,'max_ms':max(vals)*1000,'runs':n,'stdout_bytes':len(outs[0])}
for p in ['s1-7','s1-12']:
    host=os.path.join(HERE,'build','glon-'+p)
    if os.path.exists(os.path.join(FIZZ,'fizzbuzz.glon')):
        apps.append(measure('fizzbuzz '+p,ROOT,[host,os.path.join(FIZZ,'fizzbuzz.glon')]))
    apps.append(measure('latex-tests '+p,os.path.join(ROOT,'experiments','glon-latex'),
                        [host,'latex.glon','test-helpers.glon','latex-tests.glon']))
    apps.append(measure('latex-v002-tests '+p,os.path.join(ROOT,'experiments','glon-latex'),
                        [host,'latex.glon','test-helpers.glon','chris-samples.glon','latex-v002-tests.glon']))
json.dump({'apps':apps},open(os.path.join(HERE,'baseline-apps.json'),'w'),indent=2)
for a in apps: print(a)
