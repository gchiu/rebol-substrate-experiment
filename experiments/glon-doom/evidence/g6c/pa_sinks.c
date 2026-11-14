#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <string.h>
static int done=0;
static void ctx_state(pa_context*c,void*u){(void)u; if(pa_context_get_state(c)==PA_CONTEXT_READY)done=1;}
static void sink_cb(pa_context*c,const pa_sink_info*i,int eol,void*u){(void)c;(void)u;if(eol){done=1;return;} const char*n=pa_proplist_gets(i->proplist,"device.description"); const char*d=pa_proplist_gets(i->proplist,"device.class"); printf("sink index=%u name=%s desc=%s class=%s latency=%.2fms configured=%.2fms\n",i->index,i->name,n?n:"?",d?d:"?",(double)i->latency/1000.0,(double)i->configured_latency/1000.0);}
int main(){pa_mainloop*m=pa_mainloop_new();pa_context*c=pa_context_new(pa_mainloop_get_api(m),"pa_sinks");pa_context_set_state_callback(c,ctx_state,NULL);pa_context_connect(c,NULL,0,NULL);while(!done)pa_mainloop_iterate(m,1,NULL);done=0;pa_operation*o=pa_context_get_sink_info_list(c,sink_cb,NULL);while(o&&pa_operation_get_state(o)==PA_OPERATION_RUNNING)pa_mainloop_iterate(m,1,NULL);if(o)pa_operation_unref(o);pa_context_disconnect(c);pa_context_unref(c);pa_mainloop_free(m);return 0;}
