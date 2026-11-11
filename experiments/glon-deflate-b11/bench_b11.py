#!/usr/bin/env python3
"""B11: compare seven-primitive and native-CALL/RET evaluator profiles on
the B9 DEFLATE workloads. Timings use normal builds; a separate count build
reports total operations and native CALL/RET counts."""
import hashlib, json, os, statistics, subprocess, sys, time, zlib
HERE=os.path.dirname(os.path.abspath(__file__)); ROOT=os.path.abspath(os.path.join(HERE,'..','..'))
BASE=os.path.join(HERE,'build','dzhost-seven-o2')
B11=os.path.join(HERE,'build','dzhost-b11')
COUNT=os.path.join(HERE,'build','dzhost-b11-count')
fixtures=json.load(open(os.path.join(HERE,'fixtures.json')))['fixtures']
reps=int(os.environ.get('B11_REPS','5'))
def run(binary,fx,out):
    cmd=[binary,'-L',ROOT,'-c',','.join(map(str,fx['chunks'])),'-r',','.join(map(str,fx['read'])),'-s','-o',out,os.path.join(HERE,fx['file'])]
    p=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=300)
    if p.returncode!=0: raise SystemExit('run failed: '+fx['name']+'\n'+p.stderr.decode()[-1000:])
    d={}
    for line in p.stderr.decode().splitlines():
        if line.startswith('STATS '): d['stats']={kv.split('=',1)[0]:kv.split('=',1)[1] for kv in line.split()[1:] if '=' in kv}
        elif line.startswith('B11_COUNT '): d['count']={kv.split('=',1)[0]:int(kv.split('=',1)[1]) for kv in line.split()[1:]}
    return d
res={'date_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'environment':{'host':'native x86_64 WSL2','compiler':'cc -O2','repetitions':reps},'workloads':[]}
for fx in fixtures:
    vals={}
    for label,binary in [('seven',BASE),('b11',B11)]:
        times=[]; stats=[]
        for i in range(reps):
            d=run(binary,fx,'/tmp/b11-'+label+'-'+fx['name']+'.out')
            times.append(float(d['stats']['run_ms'])); stats.append(d['stats'])
        out=open('/tmp/b11-'+label+'-'+fx['name']+'.out','rb').read()
        want=zlib.decompress(open(os.path.join(HERE,fx['file']),'rb').read(),-15)
        if out!=want: raise SystemExit('output mismatch '+fx['name']+' '+label)
        vals[label]={'run_ms_median':statistics.median(times),'run_ms_min':min(times),'run_ms_max':max(times),
                     'spread_pct':100*(max(times)-min(times))/statistics.median(times),
                     'decode_ms_median':statistics.median(float(s['read_ms'])+float(s['feed_ms']) for s in stats),
                     'code_end':int(stats[-1]['code_end']),'max_live':int(stats[-1]['max_live']),
                     'max_rs':int(stats[-1]['max_rs']),'gcs':int(stats[-1]['gcs'])}
    c=run(COUNT,fx,'/tmp/b11-count-'+fx['name']+'.out')
    vals['count']=c['count']
    res['workloads'].append({'name':fx['name'],'compressed_bytes':fx['compressed_bytes'],'decompressed_bytes':fx['decompressed_bytes'],'measurements':vals})
    print(fx['name'],'seven',round(vals['seven']['run_ms_median'],1),'b11',round(vals['b11']['run_ms_median'],1),'ratio',round(vals['seven']['run_ms_median']/vals['b11']['run_ms_median'],3),'count',vals['count'])
with open(os.path.join(HERE,'baseline','b11-results.json'),'w') as f: json.dump(res,f,indent=2); f.write('\n')
print('wrote baseline/b11-results.json')
