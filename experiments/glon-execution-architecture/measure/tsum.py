import sys,collections
S=sys.argv[1]; dump=sys.argv[2]; tr=sys.argv[3]
syms={int(a,16):n for a,n in (l.split() for l in open(S+'/syms.txt'))}
HELP={'emit_call_fwd','emit_call_indirect','emit_decode_site','emit_esc_ref','emit_word_lit','emit_raise_fields','emit_esc_store_check','emit_worklist_full_halt','emit_raise_escape','pf_incr'}
KIND={0:'',1:'CALL',2:'EXIT',3:'BRANCH',4:'>R',5:'R>',6:'R@',7:'SWAP'}
own={}; kind={}; rtend=0
for l in open(dump):
    if l.startswith('RTEND'): rtend=int(l.split()[1]); continue
    p=l.split(); c=int(p[0]); kind[c]=int(p[1]); fn='?'
    for x in p[3:]:
        f=syms.get(int(x,16),'?')
        if not (f.startswith('e_') or f.startswith('asm_') or f in HELP): fn=f; break
    own[c]=fn if c<rtend else 'masm library word'
byr=collections.Counter(); byk=collections.Counter(); n=0
for l in open(tr):
    ip=int(l.split()[0]); n+=1
    byr[own.get(ip,'?').replace('emit_','')]+=1; byk[KIND[kind.get(ip,0)] or 'other']+=1
print(f"{n} instructions; by routine:", ', '.join(f"{k} {v}" for k,v in byr.most_common(9)))
print("   by macro kind:", ', '.join(f"{k} {v}" for k,v in byk.most_common()))
