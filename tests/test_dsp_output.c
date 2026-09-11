// SPDX-License-Identifier: Apache-2.0
#include "echoy_audio_output.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct {int open_result,write_result,close_result;unsigned opens,writes,closes;} sink_t;
static int open_sink(void *ctx,unsigned rate,unsigned bits,unsigned channels){sink_t *s=ctx;s->opens++;assert(rate==48000&&bits==16&&channels==2);return s->open_result;}
static int write_sink(void *ctx,const void *pcm,size_t bytes){sink_t *s=ctx;s->writes++;assert(pcm&&bytes==1024);return s->write_result;}
static int close_sink(void *ctx){sink_t *s=ctx;s->closes++;return s->close_result;}
int main(void){
    echoy_audio_t *a=malloc(echoy_audio_size());assert(echoy_audio_init(a,echoy_audio_size(),0));
    echoy_audio_output_t o;sink_t sink={0};echoy_output_ops_t ops={open_sink,write_sink,close_sink,&sink};
    assert(echoy_output_init(&o,a,ops));assert(echoy_output_open(&o,0));assert(echoy_output_block(&o));assert(sink.writes==1&&o.writes==1);
    sink.write_result=-71;assert(!echoy_output_block(&o));assert(o.retry_required&&o.last_error==-71&&o.write_errors==1);
    assert(echoy_audio_stats(a).output_fault);unsigned writes=sink.writes;
    assert(!echoy_output_block(&o));assert(sink.writes==writes);
    sink.open_result=-42;assert(!echoy_output_open(&o,1000000));assert(o.retry_required&&o.last_error==-42&&o.open_errors==1);
    sink.open_result=0;sink.write_result=0;assert(echoy_output_open(&o,1000000));
    assert(!echoy_audio_stats(a).output_fault&&echoy_audio_stats(a).cursor==48000);
    assert(echoy_output_block(&o));sink.close_result=-9;assert(!echoy_output_close(&o));assert(o.close_error==-9&&o.retry_required);
    free(a);puts("PASS DSP output format, write fault, explicit retry, reopen fault, close fault");return 0;
}
