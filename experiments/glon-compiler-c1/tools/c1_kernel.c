#define _POSIX_C_SOURCE 200809L
/* C1: execute the unchanged hf/left kernel on the 12-operation candidate. */
#include "c1_compiler.h"
#include "b3_machine.h"
#include "r0_s1.h"
#include "bench_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_BASE 40000
#define CANDIDATE (B3_NATIVE_CALL|B3_NATIVE_RET|B3_NATIVE_TO_R|B3_NATIVE_FROM_R|B3_NATIVE_RFETCH)

typedef struct { const char *name; long c[16]; int l; long left; long expected; } Vec;
static Vec vecs[] = {
 {"all_zero",       {0}, 1, 1, 32768},
 {"complete_len1_2",{0,2}, 1, 1, 0},
 {"incomplete_len1_1",{0,1}, 1, 1, 16384},
 {"over_len1_3",    {0,3}, 1, 1, -1},
 {"len2_4",         {0,0,4}, 1, 1, 0},
 {"len3_8",         {0,0,0,8}, 1, 1, 0},
 {"two_codes_len2",  {0,0,4}, 2, 1, -1},
 {"boundary_l16",   {0}, 16, 123, 123},
 {"zero_left",      {0}, 1, 0, 0},
 {"partial",        {0,1,2}, 1, 1, 0},
};
#define NV ((int)(sizeof vecs/sizeof vecs[0]))

#ifdef B3_COUNT
static long hf_ref(const long *c,int l,long left){
    if (l>15) return left;
    long k = left*2 - c[l];
    if (k<0) return -1;
    return hf_ref(c,l+1,k);
}
#endif
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb"); if(!f){perror(path);return NULL;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    char *s=malloc((size_t)n+1); if(!s){fclose(f);return NULL;}
    if(fread(s,1,(size_t)n,f)!=(size_t)n){fclose(f);free(s);return NULL;}
    fclose(f); s[n]=0; return s;
}
static void push_cell(cell v) {
    cell sp = M[REG_SP] - 1;
    M[REG_SP] = sp;
    M[sp] = v;
}
static void push_args(const Vec *v){
    push_cell(BLOCK_BASE);          /* block handle: raw base cell */
    push_cell(mk_int(v->l));
    push_cell(mk_int(v->left));
}
static void fill_block(const Vec *v){
    for (int i=0;i<16;i++) M[BLOCK_BASE+i] = mk_int(v->c[i]);
}

#ifndef B3_COUNT
typedef struct { const char *src; const char *fn; unsigned flags; } CompileCtx;
static void compile_cb(void *v){
    CompileCtx *c=v; b3_code code; char e[256];
    (void)c1_compile(c->src,c->fn,c->flags,B3_CONV_RETSTACK,&code,e,sizeof e);
}
#endif

int main(int argc,char **argv){
    if (argc<2 || (strcmp(argv[1],"--measure") && strcmp(argv[1],"--bench"))) {
        fprintf(stderr,"usage: %s --measure|--bench [none|candidate]\n",argv[0]); return 2;
    }
    const char *which = argc>2 ? argv[2] : "candidate";
    unsigned flags = strcmp(which,"none") ? CANDIDATE : 0;
    char err[512];
    char *src=read_file("reference/hf_left.glon");
    if(!src)return 2;
    b3_code code;
    if(!c1_compile(src,"hf/left",flags,B3_CONV_RETSTACK,&code,err,sizeof err)){
        printf("{\"error\":\"%s\"}\n",err); free(src); return 1;
    }
    cell tramp=asm_here();
    if(flags & B3_NATIVE_CALL){
        cell p=b3_native_call_fwd(); asm_halt(); s1_set_mem(p,code.entry);
    } else {
        asm_call(code.entry); asm_halt();
    }
    long total_cells=asm_here()-256;
#ifdef B3_COUNT
    printf("{\"config\":\"%s\",\"function_cells\":%ld,\"total_cells\":%ld,\"vectors\":[",which,code.code_cells,total_cells);
    int first=1;
    for(int i=0;i<NV;i++){
        fill_block(&vecs[i]);
        s1_reset(); b3_reset_counts(); push_args(&vecs[i]); s1_run(tramp);
        long got=int_val(s1_top());
        long want=hf_ref(vecs[i].c,vecs[i].l,vecs[i].left);
        int pass=(got==want && M[REG_SP]==16384-1 && M[REG_RP]==24576);
        if(!first)printf(",");
        first=0;
        printf("{\"name\":\"%s\",\"got\":%ld,\"want\":%ld,\"pass\":%s,"
               "\"instructions\":%ld,\"ds\":%ld,\"rs\":%ld}",
               vecs[i].name,got,want,pass?"true":"false",b3_insns,
               16384-b3_min_sp,24576-b3_min_rp);
        if(!pass) fprintf(stderr,"FAIL %s got %ld want %ld\n",vecs[i].name,got,want);
    }
    printf("]}\n");
#else
    (void)tramp;
    CompileCtx cc={src,"hf/left",flags};
    bench_stats cs=bench_measure(compile_cb,&cc,0.02,7);
    printf("{\"config\":\"%s\",\"function_cells\":%ld,\"total_cells\":%ld,"
           "\"compile_median_us\":%.3f,\"vectors\":[",which,code.code_cells,total_cells,cs.median*1e6);
    int first=1;
    for(int i=0;i<NV;i++){
        /* one independent timing batch per vector */
        double t0=bench_now(); long iters=1;
        for(;;){
            for(long k=0;k<iters;k++){
                fill_block(&vecs[i]); s1_reset(); push_args(&vecs[i]); s1_run(tramp);
            }
            double el=bench_now()-t0;
            if(el>=0.05 || iters>=100000000L) break;
            iters*=2;
        }
        double times[9];
        for(int t=0;t<9;t++){
            double s=bench_now();
            for(long k=0;k<iters;k++){
                fill_block(&vecs[i]); s1_reset(); push_args(&vecs[i]); s1_run(tramp);
            }
            times[t]=(bench_now()-s)/iters;
        }
        for(int a=0;a<9;a++)for(int b=a+1;b<9;b++)if(times[b]<times[a]){double x=times[a];times[a]=times[b];times[b]=x;}
        double med=times[4], mn=times[0], mx=times[8];
        if(!first)printf(",");
        first=0;
        printf("{\"name\":\"%s\",\"exec_median_us\":%.3f,\"exec_min_us\":%.3f,"
               "\"exec_max_us\":%.3f,\"exec_spread_pct\":%.2f}",
               vecs[i].name,med*1e6,mn*1e6,mx*1e6,med>0?100*(mx-mn)/med:0);
    }
    printf("]}\n");
#endif
    free(src); return 0;
}
