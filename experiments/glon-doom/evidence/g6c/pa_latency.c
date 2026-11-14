#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int done=0;
static void ctx_state(pa_context *c, void *u){ (void)u; if(pa_context_get_state(c)==PA_CONTEXT_READY) done=1; }
static void sink_input_cb(pa_context *c, const pa_sink_input_info *i, int eol, void *u){
    (void)c; (void)u;
    if(eol){ done=1; return; }
    const char *app=pa_proplist_gets(i->proplist,"application.name");
    const char *media=pa_proplist_gets(i->proplist,"media.name");
    const char *pid=pa_proplist_gets(i->proplist,"application.process.id");
    printf("sink_input index=%u app=%s media=%s pid=%s buffer=%.2fms sink=%.2fms total=%.2fms\n",
           i->index, app?app:"?", media?media:"?", pid?pid:"?",
           (double)i->buffer_usec/1000.0,(double)i->sink_usec/1000.0,
           (double)(i->buffer_usec+i->sink_usec)/1000.0);
}
int main(int argc,char**argv){
    int want=argc>1?atoi(argv[1]):0;
    pa_mainloop *m=pa_mainloop_new(); pa_context *c=pa_context_new(pa_mainloop_get_api(m),"pa_latency_probe");
    pa_context_set_state_callback(c,ctx_state,NULL); pa_context_connect(c,NULL,0,NULL);
    while(!done){ pa_mainloop_iterate(m,1,NULL); }
    done=0; pa_operation *o=pa_context_get_sink_input_info_list(c,sink_input_cb,NULL);
    while(o && pa_operation_get_state(o)==PA_OPERATION_RUNNING){ pa_mainloop_iterate(m,1,NULL); }
    if(o) pa_operation_unref(o);
    pa_context_disconnect(c); pa_context_unref(c); pa_mainloop_free(m);
    (void)want; return 0;
}
