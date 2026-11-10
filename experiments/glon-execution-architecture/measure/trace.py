import sys
S=sys.argv[1]; dump=sys.argv[2]; tr=sys.argv[3]
syms={int(a,16):n for a,n in (l.split() for l in open(S+'/syms.txt'))}
HELP={'emit_call_fwd','emit_call_indirect','emit_decode_site','emit_esc_ref','emit_word_lit','emit_raise_fields','emit_esc_store_check','emit_worklist_full_halt','emit_raise_escape','pf_incr'}
KIND={0:'',1:'CALL',2:'EXIT',3:'BRANCH',4:'>R',5:'R>',6:'R@',7:'SWAP'}
own={}; kind={}
for l in open(dump):
    if l.startswith('RTEND'): continue
    p=l.split(); c=int(p[0]); kind[c]=int(p[1])
    fn='?'
    for x in p[3:]:
        f=syms.get(int(x,16),'?')
        if not (f.startswith('e_') or f.startswith('asm_') or f in HELP): fn=f; break
    own[c]=fn
OPS={0:'LIT',1:'DUP',2:'DROP',3:'@',4:'!',5:'ZBRANCH',6:'HOST',7:'HALT'}
segs=[]; n=0
for l in open(tr):
    ip,op,sp,rp=map(int,l.split()); n+=1
    r=own.get(ip,'?').replace('emit_','')
    k=KIND[kind.get(ip,0)]
    key=(r,k)
    if segs and segs[-1][0]==key: segs[-1][1]+=1
    else: segs.append([key,1,sp,rp])
print(f"{n} S1 instructions in {len(segs)} segments")
for (r,k),cnt,sp,rp in segs: print(f"  {r:18s} {k:7s} {cnt:4d}   SP={sp} RP={rp}")
