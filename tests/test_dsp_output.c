// SPDX-License-Identifier: Apache-2.0
#include "echoy_audio_output.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct {int open_result,write_result,close_result;unsigned opens,writes,closes;} sink_t;
static int open_sink(void *ctx,unsigned rate,unsigned bits,unsigned channels){sink_t *s=ctx;s->opens++;assert(rate==48000&&bits==16&&channels==2);return s->open_result;}
static int write_sink(void *ctx,const void *pcm,size_t bytes){sink_t *s=ctx;s->writes++;assert(pcm&&bytes==1024);return s->write_result;}
static int close_sink(void *ctx){sink_t *s=ctx;s->closes++;return s->close_result;}
/* Models the codec dependency: open marks output_opened BEFORE set_fs/enable,
 * and subsequent open returns success immediately while that flag remains set. */
typedef struct {
    bool output_opened,enabled;
    int fail_stage,close_result;
    unsigned opens,set_fs_calls,enable_calls,closes;
} partial_codec_t;
static int partial_open(void *ctx,unsigned rate,unsigned bits,unsigned channels){
    partial_codec_t *s=ctx;assert(rate==48000&&bits==16&&channels==2);s->opens++;
    if(s->output_opened)return 0;
    s->output_opened=true;s->set_fs_calls++;
    if(s->fail_stage==1)return -41;
    s->enable_calls++;if(s->fail_stage==2)return -42;
    s->enabled=true;return 0;
}
static int partial_write(void *ctx,const void *pcm,size_t bytes){
    partial_codec_t *s=ctx;assert(pcm&&bytes==1024);return s->enabled?0:-99;
}
static int partial_close(void *ctx){
    partial_codec_t *s=ctx;s->closes++;
    if(s->close_result)return s->close_result;
    s->output_opened=s->enabled=false;return 0;
}
static void partial_open_is_cleaned_before_retry(void){
    for(int stage=1;stage<=2;stage++){
        echoy_audio_t *a=malloc(echoy_audio_size());assert(echoy_audio_init(a,echoy_audio_size(),0));
        partial_codec_t codec={.fail_stage=stage};echoy_audio_output_t o;
        echoy_output_ops_t ops={partial_open,partial_write,partial_close,&codec};
        assert(echoy_output_init(&o,a,ops));assert(!echoy_output_open(&o,0));
        assert(codec.closes==1&&!codec.output_opened&&!o.opened&&o.retry_required);
        assert(o.last_error==-(40+stage)&&echoy_audio_stats(a).output_fault);
        codec.fail_stage=0;assert(echoy_output_open(&o,0));
        assert(codec.set_fs_calls==2&&codec.enable_calls==(stage==1?1u:2u)&&codec.enabled);
        assert(echoy_output_block(&o));assert(echoy_output_close(&o));free(a);
    }
    echoy_audio_t *a=malloc(echoy_audio_size());assert(echoy_audio_init(a,echoy_audio_size(),0));
    partial_codec_t codec={.fail_stage=1,.close_result=-91};echoy_audio_output_t o;
    echoy_output_ops_t ops={partial_open,partial_write,partial_close,&codec};
    assert(echoy_output_init(&o,a,ops));assert(!echoy_output_open(&o,0));
    assert(o.last_error==-41&&o.close_error==-91&&o.opened&&o.retry_required);
    codec.fail_stage=0;assert(!echoy_output_open(&o,0));
    assert(codec.opens==1&&codec.closes==2); /* No false-success open retry. */
    codec.close_result=0;assert(echoy_output_open(&o,0));
    assert(codec.closes==3&&codec.set_fs_calls==2&&codec.enabled);
    assert(!o.retry_required&&!o.close_error);assert(echoy_output_close(&o));free(a);
}
int main(void){
    partial_open_is_cleaned_before_retry();
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
