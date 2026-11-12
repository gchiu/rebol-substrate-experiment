#!/usr/bin/env python3
"""B13 memory characterisation using existing runtime and fixtures.

Builds a temporary, read-only host probe from experiments/glon-deflate/dzhost.c
in /tmp. It only scans the existing heap headers after a run; no production or
committed runtime source is changed. Native process RSS is measured with
/usr/bin/time -v. GC pause and per-allocation latency are not measured because
they require runtime timestamp instrumentation.
"""
import json, os, re, statistics, subprocess, sys, tempfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HERE = os.path.dirname(os.path.abspath(__file__))
TMP = '/tmp/glon-memory-audit'
os.makedirs(TMP, exist_ok=True)
fixtures = json.load(open(os.path.join(ROOT, 'experiments/glon-deflate-b9/fixtures.json')))['fixtures']

src = open(os.path.join(ROOT, 'experiments/glon-deflate/dzhost.c')).read()
probe_fn = r'''
static void probe_heap(void) {
    long hp = (long)M[REG_HP];
    long live = 0, live_objs = 0, free_cells = 0, free_blocks = 0, largest = 0;
    for (long a = GC_HEAP_BASE; a < hp;) {
        long size = (long)M[a];
        if (size <= 0 || a + size > hp) break;
        long flags = (long)M[a + GC_HDR_FLAGS];
        if (flags & GC_FLAG_ALLOC) { live += size; live_objs++; }
        else { free_cells += size; free_blocks++; if (size > largest) largest = size; }
        a += size;
    }
    fprintf(stderr, "B13_HEAP hp=%ld used_high=%ld scan_live=%ld scan_live_objs=%ld scan_free=%ld scan_free_blocks=%ld largest_free=%ld gc_free=%ld gc_blocks=%ld gcs=%ld live_counter=%ld\\n",
            hp, hp - GC_HEAP_BASE, live, live_objs, free_cells, free_blocks, largest,
            (long)M[GC_FREE_CELLS], (long)M[GC_FREE_BLOCKS],
            (long)M[GC_COLLECT_CNT], r0_s1_gc_live_cells());
}
'''
assert 'int main(' in src
src = src.replace('int main(', probe_fn + '\nint main(', 1)
src = src.replace('    double t_run = now() - t_run0;\n', '    double t_run = now() - t_run0;\n    probe_heap();\n', 1)
probe_src = os.path.join(TMP, 'dzhost_probe.c')
open(probe_src, 'w').write(src)
probe_bin = os.path.join(TMP, 'dzhost-probe')
cmd = ['cc','-std=c17','-O2','-Wall','-Wextra','-D_POSIX_C_SOURCE=200809L','-DGLON_LIVE','-I'+ROOT,'-o',probe_bin,probe_src,
       os.path.join(ROOT,'r0_s1_g1a_live.c'),os.path.join(ROOT,'r0_s1_g1a.c'),os.path.join(ROOT,'r0_s1_runtime.c'),os.path.join(ROOT,'s1.c')]
subprocess.run(cmd, check=True)

def run(binary, args, env=None):
    e=dict(os.environ)
    if env: e.update(env)
    p=subprocess.run(binary+args, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=e, timeout=300)
    return p.returncode, p.stderr.decode()

def parse_mem(text):
    out={}
    for line in text.splitlines():
        if line.startswith('STATS '): out['stats']={kv.split('=',1)[0]:kv.split('=',1)[1] for kv in line.split()[1:] if '=' in kv}
        if line.startswith('B13_HEAP '): out['heap']={kv.split('=',1)[0]:kv.split('=',1)[1] for kv in line.split()[1:] if '=' in kv}
        if 'request_extent=' in line: out['failure']={kv.split('=',1)[0]:kv.split('=',1)[1] for kv in line.split()[1:] if '=' in kv}
    return out
res={'fixtures':[]}
for fx in fixtures:
    out='/tmp/'+fx['name']+'.b13.out'
    args=['-L',ROOT,'-c',','.join(map(str,fx['chunks'])),'-r',','.join(map(str,fx['read'])),'-s','-o',out,os.path.join(ROOT,'experiments/glon-deflate-b9',fx['file'])]
    rc,text=run([probe_bin],args)
    d=parse_mem(text); d['name']=fx['name']; d['rc']=rc
    # process RSS is a separate normal run, using /usr/bin/time
    t=subprocess.run(['/usr/bin/time','-v',probe_bin]+args,cwd=ROOT,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=300)
    m=re.search(r'Maximum resident set size \(kbytes\): (\d+)',t.stderr.decode())
    d['max_rss_kb']=int(m.group(1)) if m else None
    res['fixtures'].append(d)
    print(fx['name'],'rc',rc,'heap',d.get('heap'),'maxrss',d['max_rss_kb'])
# LaTeX/FizzBuzz process RSS and timing via existing B13 binaries; managed heap stats unavailable there.
apps=[]
b13=os.path.join(ROOT,'experiments/glon-profiles-b13/build')
for profile in ['s1-7','s1-12']:
    host=os.path.join(b13,'glon-'+profile)
    if not os.path.exists(host): continue
    jobs=[]
    fizz=os.path.expanduser('/tmp/opencode/glon-fizzbuzz/experiments/glon-fizzbuzz/fizzbuzz.glon')
    if os.path.exists(fizz): jobs.append(('fizzbuzz', [host,fizz], ROOT))
    jobs.append(('latex-tests',[host,'latex.glon','test-helpers.glon','latex-tests.glon'],os.path.join(ROOT,'experiments/glon-latex')))
    jobs.append(('latex-v002-tests',[host,'latex.glon','test-helpers.glon','chris-samples.glon','latex-v002-tests.glon'],os.path.join(ROOT,'experiments/glon-latex')))
    for name,cmd,cwd in jobs:
        t=subprocess.run(['/usr/bin/time','-v']+cmd,cwd=cwd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=120)
        m=re.search(r'Maximum resident set size \(kbytes\): (\d+)',t.stderr.decode())
        apps.append({'profile':profile,'app':name,'rc':t.returncode,'max_rss_kb':int(m.group(1)) if m else None})
res['apps']=apps
print('apps',apps)
json.dump(res,open(os.path.join(HERE,'measurements.json'),'w'),indent=2)
print('wrote measurements.json')
