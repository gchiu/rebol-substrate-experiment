#!/usr/bin/env python3
"""B12: seven vs nine vs twelve primitive profiles on the full DEFLATE decoder.
All builds are O2 and use the B9 fixtures. Native counts are a separate build.
"""
import hashlib, json, os, statistics, subprocess, time, zlib
HERE=os.path.dirname(os.path.abspath(__file__)); ROOT=os.path.abspath(os.path.join(HERE,'..','..'))
HOSTS={'seven':os.path.join(HERE,'build','dzhost-seven-o2'),
       'nine':os.path.join(HERE,'build','dzhost-nine'),
       'twelve':os.path.join(HERE,'build','dzhost-twelve')}
COUNTS={'nine':os.path.join(HERE,'build','dzhost-nine-count'),
        'twelve':os.path.join(HERE,'build','dzhost-twelve-count')}
fixtures=json.load(open(os.path.join(HERE,'fixtures.json')))['fixtures']
reps=int(os.environ.get('B12_REPS','5'))
def run(binary,fx,out):
    cmd=[binary,'-L',ROOT,'-c',','.join(map(str,fx['chunks'])),'-r',','.join(map(str,fx['read'])),'-s','-o',out,os.path.join(HERE,fx['file'])]
    p=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=300)
    if p.returncode!=0: raise SystemExit('run failed: '+fx['name']+' '+binary+'\n'+p.stderr.decode()[-1000:])
    d={}
    for line in p.stderr.decode().splitlines():
        if line.startswith('STATS '): d['stats']={kv.split('=',1)[0]:kv.split('=',1)[1] for kv in line.split()[1:] if '=' in kv}
        elif line.startswith('B12_COUNT '): d['count']={kv.split('=',1)[0]:int(kv.split('=',1)[1]) for kv in line.split()[1:]}
    return d
res={'date_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'environment':{'host':'native x86_64 WSL2','compiler':'cc -O2','repetitions':reps},'workloads':[]}
for fx in fixtures:
    m={}
    for profile,host in HOSTS.items():
        times=[]; stats=[]
        for i in range(reps):
            d=run(host,fx,'/tmp/b12-'+profile+'-'+fx['name']+'.out'); times.append(float(d['stats']['run_ms'])); stats.append(d['stats'])
        out=open('/tmp/b12-'+profile+'-'+fx['name']+'.out','rb').read(); want=zlib.decompress(open(os.path.join(HERE,fx['file']),'rb').read(),-15)
        if out!=want: raise SystemExit('output mismatch '+fx['name']+' '+profile)
        m[profile]={'run_ms_median':statistics.median(times),'run_ms_min':min(times),'run_ms_max':max(times),
                    'spread_pct':100*(max(times)-min(times))/statistics.median(times),
                    'decode_ms_median':statistics.median(float(s['read_ms'])+float(s['feed_ms']) for s in stats),
                    'code_end':int(stats[-1]['code_end']),'max_live':int(stats[-1]['max_live']),
                    'max_rs':int(stats[-1]['max_rs']),'gcs':int(stats[-1]['gcs'])}
    for profile,host in COUNTS.items():
        d=run(host,fx,'/tmp/b12-count-'+profile+'-'+fx['name']+'.out'); m[profile]['count']=d['count']
    res['workloads'].append({'name':fx['name'],'compressed_bytes':fx['compressed_bytes'],'decompressed_bytes':fx['decompressed_bytes'],'profiles':m})
    print(fx['name'],'seven',round(m['seven']['run_ms_median'],1),'nine',round(m['nine']['run_ms_median'],1),'twelve',round(m['twelve']['run_ms_median'],1),'counts',m['nine'].get('count'),m['twelve'].get('count'))
with open(os.path.join(HERE,'baseline','b12-results.json'),'w') as f: json.dump(res,f,indent=2); f.write('\n')
print('wrote baseline/b12-results.json')
