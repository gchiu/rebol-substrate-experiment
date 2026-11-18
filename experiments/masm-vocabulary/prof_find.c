#include "r0_s1.h"
#include "s1.h"
#include "s1_prof.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f=fopen(path,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
    char *s=malloc((size_t)n+1); if(!s){fclose(f);return NULL;}
    size_t g=fread(s,1,(size_t)n,f); fclose(f); s[g]=0; return s;
}
static void strip(char *s){char *r=s,*w=s;while(*r){if(r[0]==';'&&r[1]==';'){while(*r&&*r!='\n')r++;continue;}*w++=*r++;}*w=0;}
static int load(const char *path){char *s=read_file(path);if(!s){fprintf(stderr,"read %s\n",path);return -1;}strip(s);int e=0;cell b=r0_s1_parse(s,&e);free(s);if(e){fprintf(stderr,"parse %s\n",path);return -1;}r0_s1_run_persistent(b);return 0;}
int main(int argc,char **argv){
 if(argc!=2){fprintf(stderr,"usage: prof_find BENCH.glon\n");return 2;}
 r0_s1_init();
 if(load("glon-lib/prelude.glon")||load("glon-lib/strings.glon")||load("glon-lib/strings-ext.glon")||load("experiments/masm-vocabulary/find.glon"))return 1;
 char *s=read_file(argv[1]); if(!s)return 1; strip(s); int e=0; cell b=r0_s1_parse(s,&e); free(s); if(e){fprintf(stderr,"bench parse\n");return 1;}
 s1_prof_reset();
 int n=r0_s1_run_persistent(b);
 long ops=0; for(int i=0;i<8;i++)ops+=s1_prof_op_count[i];
 long hosts=0; for(int i=0;i<16;i++)hosts+=s1_prof_host_count[i];
 printf("result_count %d opcodes %ld hosts %ld\n",n,ops,hosts);
 for(int i=0;i<8;i++)printf("op%d %ld\n",i,s1_prof_op_count[i]);
 for(int i=0;i<16;i++)printf("host%d %ld\n",i,s1_prof_host_count[i]);
 return 0;
}
