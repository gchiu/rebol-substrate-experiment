#!/usr/bin/env python3
"""B10: measure operation categories on the B9 DEFLATE workloads.

Counts come from a non-profiled runtime + instrumented S1 dispatcher
(b10-host-ops); evaluator/GC counters come from a separate R0_S1_PROFILE build
(b10-host-pf). Neither binary is used for elapsed-time claims.
"""
import hashlib, json, os, subprocess, time, zlib
HERE=os.path.dirname(os.path.abspath(__file__)); ROOT=os.path.abspath(os.path.join(HERE,'..','..'))
fixtures=json.load(open(os.path.join(HERE,'fixtures.json')))['fixtures']
bin_ops=os.path.join(HERE,'build','b10-host-ops'); bin_pf=os.path.join(HERE,'build','b10-host-pf')
def run(binary, fx, out):
    cmd=[binary,'-L',ROOT,'-c',','.join(map(str,fx['chunks'])),'-r',','.join(map(str,fx['read'])),'-s','-o',out,os.path.join(HERE,fx['file'])]
    p=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=300)
    if p.returncode!=0: raise SystemExit('run failed '+fx['name']+'\n'+p.stderr.decode()[-1000:])
    d={}
    for line in p.stderr.decode().splitlines():
        if line.startswith('B10_OPS '): d['ops']={kv.split('=',1)[0]:int(kv.split('=',1)[1]) for kv in line.split()[1:]}
        elif line.startswith('B10_HOST '): d['host']={kv.split('=',1)[0]:int(kv.split('=',1)[1]) for kv in line.split()[1:]}
        elif line.startswith('B10_PROFILE '): d['pf']={kv.split('=',1)[0]:int(kv.split('=',1)[1]) for kv in line.split()[1:]}
        elif line.startswith('B9_PROFILE '): d['b9']={kv.split('=',1)[0]:int(kv.split('=',1)[1]) for kv in line.split()[1:]}
        elif line.startswith('STATS '): d['stats']={kv.split('=',1)[0]:kv.split('=',1)[1] for kv in line.split()[1:] if '=' in kv}
    return d
res={'date_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'workloads':[]}
for fx in fixtures:
    o=run(bin_ops,fx,'/tmp/b10-'+fx['name']+'.ops.out')
    p=run(bin_pf,fx,'/tmp/b10-'+fx['name']+'.pf.out')
    got = open('/tmp/b10-'+fx['name']+'.ops.out','rb').read()
    want = zlib.decompress(open(os.path.join(HERE,fx['file']),'rb').read(), -15)
    if got != want or hashlib.sha256(got).hexdigest() != fx['sha256']:
        raise SystemExit('output mismatch: ' + fx['name'])
    item={'name':fx['name'],'compressed_bytes':fx['compressed_bytes'],'decompressed_bytes':fx['decompressed_bytes'],
          'ops':o['ops'],'host':o['host'],'b9':o['b9'],'stats_ops':o['stats'],
          'pf':p['pf'],'pf_instrumented_ops':p['b9'],'pf_stats':p['stats']}
    res['workloads'].append(item)
    tot=sum(o['ops'].values())
    print(fx['name'],'ops',tot,'host',sum(o['host'].values()),'closure',p['pf']['closure'],'subexpr',p['pf']['subexpr'],'lookup',p['pf']['lookup'])
with open(os.path.join(HERE,'baseline','profile-results.json'),'w') as f: json.dump(res,f,indent=2); f.write('\n')
print('wrote baseline/profile-results.json')
