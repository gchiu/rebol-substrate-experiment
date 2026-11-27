#!/usr/bin/env python3
"""tests/core-v2/defs.py - GLON-VIEW-CORE-V2: top-level definitions per source
(vocabulary members expanded to full names); reports names defined by more
than one source.  Usage: defs.py ROOT SRC..."""
import sys, os, importlib.util, collections
spec=importlib.util.spec_from_file_location("la", os.path.join(os.path.dirname(__file__),"lexaudit.py"))
src=open(spec.origin).read().replace("\nmain()\n","\n")
la={}; exec(compile(src,"lexaudit","exec"),la)
toks,end,skip_masm,is_set=la['toks'],la['end'],la['skip_masm'],la['is_set']
def defs(t):
    out=[]
    def walk(i,stop,prefix,depth):
        while i<stop:
            x=t[i]
            if x in ('masm','raw'): i=skip_masm(t,i); continue
            if x=='vocabulary' and i+2<len(t) and t[i+2]=='[':
                e=end(t,i+2); walk(i+3,e,prefix+t[i+1]+'/',depth); i=e+1; continue
            if x=='func' and i+1<len(t) and t[i+1]=='[':
                pe=end(t,i+1); i=(end(t,pe+1)+1) if pe+1<len(t) and t[pe+1]=='[' else pe+1; continue
            if x=='[': walk(i+1,end(t,i),prefix,depth+1); i=end(t,i)+1; continue
            if is_set(x) and depth<=1:
                n=x[:-1]; nxt=t[i+1] if i+1<len(t) else ''
                kind='def' if nxt in ('func','masm','raw','does','lambda') else ('alias' if nxt.startswith(':') else 'data')
                out.append(((n if '/' in n and prefix and n.split('/')[0]+'/'!=prefix.split('/')[-2]+'/' else prefix+n) if prefix and '/' not in n else n, kind))
            i+=1
    walk(0,len(t),'',0); return out
root=sys.argv[1]; where=collections.defaultdict(list)
for f in sys.argv[2:]:
    for n,k in defs(toks(open(os.path.join(root,f),errors='replace').read())): where[n].append((f,k))
multi={n:v for n,v in where.items() if len(set(f for f,_ in v))>1}
print("sources:",len(sys.argv)-2,"names:",len(where),"defined in more than one source:",len(multi))
for n,v in sorted(multi.items()): print("  %-28s %s"%(n,", ".join("%s(%s)"%(os.path.basename(f),k) for f,k in v)))
