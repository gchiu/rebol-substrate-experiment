#!/usr/bin/env python3
"""tests/core-v2/lexaudit.py - GLON-VIEW-CORE-V2 lexical-binding audit (a static
approximation of the GLON-VOCAB-1 loader rules; the loader is authoritative).

Usage: lexaudit.py ROOT SRC1 SRC2 ...   (sources in session load order)

For every function set-word that is not a parameter/local of an enclosing
function, VOCAB-1 binds it to (1) a global defined at the top level of the
same source, or (2) a DATA global (not func/masm/does/lambda/raw) defined by an
earlier source; otherwise it is a NEW LOCAL.  The old runtime instead wrote any
binding existing at run time.  Reported divergences:
  A  set-word names a function/masm/native global of an earlier source
     (old: overwrote it; new: a local)
  B  set-word names a global first defined by a LATER source (old: wrote it once loaded)
  C  set-word names no global anywhere, but the name is READ in another function
     of the session where it is not a local there (old: run-time global creation)
  U  set-word names an earlier-source global whose kind is unknown (get-word value)
Qualified names (a/b:) are globals or members either way and are not audited.
Inside `vocabulary` bodies VOCAB-1 never assigns globals from functions; such
functions are reported only for A/B/C if they assign plain non-member names.
"""
import sys, os
NATIVES = set("+ - * / = < > <= >= print values either do invoke reduce make-error error? error-type error-id error-arg raise trap func masm raw lambda does".split())
DEF_HEADS = {"func", "masm", "raw", "does", "lambda"}
def toks(s):
    out=[];i=0;n=len(s)
    while i<n:
        c=s[i]
        if c==';':
            while i<n and s[i]!='\n': i+=1
            continue
        if c=='"':
            j=i+1
            while j<n and s[j]!='"': j+= 2 if s[j]=='^' else 1
            out.append('"S"'); i=j+1; continue
        if c in '[]': out.append(c); i+=1; continue
        if c.isspace(): i+=1; continue
        j=i
        while j<n and not s[j].isspace() and s[j] not in '[]': j+=1
        out.append(s[i:j]); i=j
    return out
def end(t,i):
    d=0
    for j in range(i,len(t)):
        if t[j]=='[': d+=1
        elif t[j]==']':
            d-=1
            if d==0: return j
    return len(t)-1
def skip_masm(t,i):
    j=i+1
    if j<len(t) and t[j].lstrip('-').isdigit(): j+=1
    return end(t,j)+1 if j<len(t) and t[j]=='[' else j
def is_set(x): return x.endswith(':') and len(x)>1 and not x.startswith(':') and not x.startswith("'")
def analyse(t):
    """top-level set-words (with kind) and functions (name, own set-words, reads, params, in_vocab)"""
    tops={}; fns=[]
    def walk(i,stop,depth_fn,outer,vocab):
        while i<stop:
            x=t[i]
            if x in ('masm','raw'): i=skip_masm(t,i); continue
            if x=='vocabulary' and i+2<len(t) and t[i+2]=='[':
                e=end(t,i+2); walk(i+3,e,depth_fn,outer,True); i=e+1; continue
            if x=='func' and i+1<len(t) and t[i+1]=='[':
                pe=end(t,i+1); params=set(w for w in t[i+2:pe] if w not in '[]')
                if pe+1<len(t) and t[pe+1]=='[':
                    be=end(t,pe+1); sets=set(); reads=set(); j=pe+2
                    while j<be:
                        y=t[j]
                        if y in ('masm','raw'): j=skip_masm(t,j); continue
                        if y=='func' and j+1<be and t[j+1]=='[':
                            e1=end(t,j+1); j=(end(t,e1+1)+1) if e1+1<len(t) and t[e1+1]=='[' else e1+1; continue
                        if is_set(y) and '/' not in y and y[:-1] not in params: sets.add(y[:-1])
                        elif not is_set(y) and y not in '[]' and not y.startswith((':',"'",'"')) and '/' not in y and not y.lstrip('-').isdigit(): reads.add(y)
                        j+=1
                    enc=set().union(*outer) if outer else set()
                    own=sets-enc
                    name=t[i-1][:-1] if i and is_set(t[i-1]) else '?'
                    fns.append((name,own,reads,params|enc|own,vocab))
                    walk(pe+2,be,depth_fn+1,outer+[params|own],vocab)
                    i=be+1; continue
                i=pe+1; continue
            if depth_fn==0 and not vocab and is_set(x) and '/' not in x:
                nxt=t[i+1] if i+1<len(t) else ''
                kind='fn' if nxt in DEF_HEADS else ('unknown' if nxt.startswith(':') else 'data')
                tops.setdefault(x[:-1],kind)
            i+=1
    walk(0,len(t),0,[],False)
    return tops,fns
def main():
    root=sys.argv[1]; srcs=sys.argv[2:]
    data=[]
    for f in srcs:
        t=toks(open(os.path.join(root,f),errors='replace').read()); data.append((f,)+analyse(t))
    all_tops={}
    for idx,(f,tops,fns) in enumerate(data):
        for n,k in tops.items(): all_tops.setdefault(n,(idx,k))
    all_reads=[(idx,fn) for idx,(f,tops,fns) in enumerate(data) for fn in fns]
    hits=[]
    for idx,(f,tops,fns) in enumerate(data):
        for name,own,reads,params,vocab in fns:
            for v in sorted(own):
                if v in tops: continue                      # same source top level: global, as before
                if v in all_tops:
                    j,k=all_tops[v]
                    if j<idx and k=='data': continue          # earlier data global: global, as before
                    cls='A' if (j<idx and k=='fn') else ('U' if j<idx else 'B')
                    hits.append((cls,f,name,v,data[j][0]))
                elif v in NATIVES:
                    hits.append(('A',f,name,v,'native'))
                else:
                    readers=[(data[i][0],fn[0]) for i,fn in all_reads if v in fn[2] and v not in fn[3] and fn[0]!=name]
                    if readers: hits.append(('C',f,name,v,readers[:3]))
    print("sources:",len(srcs),"functions:",sum(len(d[2]) for d in data),"divergence candidates:",len(hits))
    for h in hits: print("  %s %s: in %s, set-word %s -> %s"%h)
main()
