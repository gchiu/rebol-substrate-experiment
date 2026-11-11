/* B8 configurable textual-Glon benchmark on the B3 experimental machine. */
#include "bench_util.h"
#include "b3_compiler.h"
#include "b3_machine.h"
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODE_BASE 256
typedef struct { cell entry; long cells; unsigned flags; const char *src; } Ctx;

static int compile_ctx(void *v, char *err, size_t errlen) {
    Ctx *c=v; b3_code b;
    if (!b3_compile(c->src,c->flags,B3_CONV_RETSTACK,&b,err,errlen)) return 0;
    c->entry=b.entry; c->cells=b.code_cells; return 1;
}
static unsigned long long hash_code(long cells) {
    unsigned long long h=1469598103934665603ULL; unsigned char *p=(unsigned char*)(M+CODE_BASE);
    for (size_t i=0;i<(size_t)cells*sizeof(cell);i++){h^=p[i];h*=1099511628211ULL;}
    return h;
}
static char *readfile(const char *path) {
    FILE *f=fopen(path,"rb"); if(!f){perror(path);return NULL;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    char *s=malloc((size_t)n+1); if(!s){fclose(f);return NULL;}
    if(fread(s,1,(size_t)n,f)!=(size_t)n){fclose(f);free(s);return NULL;}
    fclose(f); s[n]=0; return s;
}
#ifndef B3_COUNT
typedef struct { cell entry; } RunCtx;
static void run_cb(void *v){RunCtx *r=v;s1_reset();s1_run(r->entry);}
static void compile_cb(void *v){Ctx *c=v;char e[256];(void)compile_ctx(c,e,sizeof e);}
#endif

int main(int argc,char **argv){
    if(argc==3 && !strcmp(argv[1],"--check")){
        char *src=readfile(argv[2]); if(!src)return 2;
        Ctx c={0,0,0xFFFFFFFFu,src}; char err[512];
        int ok=compile_ctx(&c,err,sizeof err);
        printf("{\"file\":\"%s\",\"ok\":%s,\"error\":\"%s\"}\n",argv[2],ok?"true":"false",ok?"":err);
        free(src); return ok?0:1;
    }
    if(argc!=4){fprintf(stderr,"usage: %s --check FILE | bench FILE FLAGS\n",argv[0]);return 2;}
    unsigned flags=(unsigned)strtoul(argv[3],NULL,0);
    char *src=readfile(argv[2]); if(!src)return 2;
    Ctx c={0,0,flags,src}; char err[512];
    if(!compile_ctx(&c,err,sizeof err)){
        printf("{\"error\":\"%s\"}\n",err); free(src); return 1;
    }
#ifdef B3_COUNT
    s1_reset(); b3_reset_counts(); s1_run(c.entry);
    printf("{\"cells\":%ld,\"instructions\":%ld,\"result\":%ld,"
           "\"stack_ok\":%s,\"code_hash\":\"%016llx\",\"ds\":%ld,\"rs\":%ld}\n",
           c.cells,b3_insns,int_val(s1_top()),
           (M[REG_SP]==16384-1&&M[REG_RP]==24576)?"true":"false",
           hash_code(c.cells),16384-b3_min_sp,24576-b3_min_rp);
#else
    RunCtx r={c.entry};
    bench_stats ex=bench_measure(run_cb,&r,0.05,9);
    bench_stats co=bench_measure(compile_cb,&c,0.02,7);
    printf("{\"cells\":%ld,\"exec_median_us\":%.3f,\"exec_min_us\":%.3f,"
           "\"exec_max_us\":%.3f,\"exec_spread_pct\":%.2f,"
           "\"compile_median_us\":%.3f,\"code_hash\":\"%016llx\"}\n",
           c.cells,ex.median*1e6,ex.min*1e6,ex.max*1e6,ex.spread,
           co.median*1e6,hash_code(c.cells));
#endif
    free(src); return 0;
}
