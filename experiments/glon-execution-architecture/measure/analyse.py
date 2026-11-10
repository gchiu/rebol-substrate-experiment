import sys, collections
S=sys.argv[1]; dump=sys.argv[2]; top=int(sys.argv[3]) if len(sys.argv)>3 else 15
syms={}
for l in open(S+'/syms.txt'):
    a,n=l.split(); syms[int(a,16)]=n
HELPERS={'emit_call_fwd','emit_call_indirect','emit_decode_site','emit_esc_ref','emit_word_lit','emit_raise_fields','emit_esc_store_check','emit_worklist_full_halt','emit_raise_escape','pf_incr'}
KIND={0:'other',1:'CALL',2:'EXIT',3:'BRANCH',4:'>R',5:'R>',6:'R@',7:'SWAP'}
CAT={}
def cat(fn):
    f=fn
    if f in ('assemble_raw','raw_emit_pending','parse_masm'): return 'masm library code'
    for k,v in [('mark','GC'),('trace','GC'),('collect','GC'),('scan','GC'),('sweep','GC'),('worklist','GC'),('emit_alloc','allocator'),
                ('esc','escape-law checks'),('raise','errors (SIN!)'),('trap','errors (SIN!)'),('mkerror','errors (SIN!)'),('error','errors (SIN!)'),
                ('ds_guard','stack guards'),('ds_top','stack guards'),('rs_guard','stack guards'),('ctx_full','stack guards'),
                ('lookup','evaluator: name lookup'),('hash','evaluator: name lookup'),('load_lex','evaluator: name lookup'),('binding_ctx','evaluator: name lookup'),
                ('invoke_closure','evaluator: closure call/return'),('emit_return','evaluator: closure call/return'),('mkctx','evaluator: closure call/return'),('mkclosure','evaluator: closure construction'),
                ('invoke_raw','evaluator: masm call'),('subexpr','evaluator: dispatch'),('block_eval','evaluator: dispatch'),('values','evaluator: dispatch'),('run_block','evaluator: dispatch'),('emit_main','evaluator: dispatch'),
                ('reduce','evaluator: dispatch'),('emit_set','evaluator: set-word'),('append','evaluator: set-word'),('native','natives'),('string','natives')]:
        if k in f: return v
    return 'other: '+f
rt_end=here=var_exec=0
stat=collections.Counter(); dyn=collections.Counter(); kstat=collections.Counter(); kdyn=collections.Counter()
fstat=collections.Counter(); fdyn=collections.Counter(); catdyn=collections.Counter(); catstat=collections.Counter()
for l in open(dump):
    if l.startswith('RTEND'):
        p=l.split(); rt_end=int(p[1]); here=int(p[3])
        if 'VAR' in p: var_exec=int(p[p.index('VAR')+1])
        continue
    p=l.split(); c=int(p[0]); k=int(p[1]); ex=int(p[2])
    chain=[syms.get(int(x,16),'?') for x in p[3:] if x!='(nil)']
    fn='(none)'
    for f in chain:
        if not (f.startswith('e_') or f.startswith('asm_') or f in HELPERS): fn=f; break
    if c>=rt_end and c<here: fn='assemble_raw'
    region='runtime' if c<rt_end else 'library'
    if c>=256 and c<here:
        stat[region]+=1; kstat[(region,KIND[k])]+=1; fstat[fn]+=1; catstat[cat(fn)]+=1
    dyn['all']+=ex; kdyn[KIND[k]]+=ex; fdyn[fn]+=ex; catdyn[cat(fn)]+=ex
tot=dyn['all']
print(f"code: runtime {stat['runtime']} cells, masm libraries {stat['library']} cells; executed {tot} S1 instructions")
print(f"memory-variable traffic (LIT state @/!): {var_exec} instructions ({100*var_exec/max(tot,1):.1f}%)")
print("by derived macro (static cells in runtime code | executed share):")
for k in ['CALL','EXIT','BRANCH','>R','R>','R@','SWAP','other']:
    print(f"  {k:7s} {kstat[('runtime',k)]:6d} cells  {kdyn[k]:12d} exec  {100*kdyn[k]/max(tot,1):5.1f}%")
mac=sum(kdyn[k] for k in ['CALL','EXIT','BRANCH','>R','R>','R@','SWAP'])
macs=sum(kstat[('runtime',k)] for k in ['CALL','EXIT','BRANCH','>R','R>','R@','SWAP'])
print(f"  derived control/return-stack macros: {macs} of {stat['runtime']} runtime cells ({100*macs/stat['runtime']:.1f}%), {100*mac/max(tot,1):.1f}% of executed")
print("by category (executed share | runtime cells | % of runtime code):")
rt_cells=stat['runtime']
shown=set()
for k,v in catdyn.most_common(top):
    shown.add(k)
    print(f"  {k:36s} {100*v/max(tot,1):5.1f}%  {catstat[k]:6d} cells  {100*catstat[k]/max(rt_cells,1):5.1f}%")
print(f"  category cell sum: {sum(catstat.values())} (runtime {rt_cells})")
scan=sum(v for f,v in fdyn.items() if 'mark_closure' in f or 'scan_closure' in f)
print(f"conservative return-stack scan (mark_closure*): {100*scan/max(tot,1):.1f}% of executed")
