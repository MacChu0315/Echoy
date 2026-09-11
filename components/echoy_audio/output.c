// SPDX-License-Identifier: Apache-2.0
#include "echoy_audio_output.h"
#include <string.h>
bool echoy_output_init(echoy_audio_output_t *o,echoy_audio_t *a,echoy_output_ops_t ops)
{
    if(!o||!a||!ops.open||!ops.write||!ops.close)return false;
    memset(o,0,sizeof *o);o->audio=a;o->ops=ops;return true;
}
bool echoy_output_close(echoy_audio_output_t *o)
{
    if(!o)return false;
    if(!o->opened)return true;
    o->close_error=o->ops.close(o->ops.ctx);
    if(o->close_error){o->last_error=o->close_error;o->retry_required=true;echoy_audio_output_fault(o->audio,true);return false;}
    o->opened=false;return true;
}
bool echoy_output_open(echoy_audio_output_t *o,int64_t us)
{
    if(!o)return false;
    if(o->opened&&!echoy_output_close(o))return false;
    o->last_error=o->ops.open(o->ops.ctx,ECHOY_AUDIO_RATE,16,2);
    if(o->last_error){
        int open_error=o->last_error;
        /* A backend can set its opened flag before failing set_fs/enable.
         * Treat it as needing close until cleanup has actually succeeded. */
        o->close_error=o->ops.close(o->ops.ctx);
        o->opened=o->close_error!=0;
        o->last_error=open_error;o->open_errors++;o->retry_required=true;
        echoy_audio_output_fault(o->audio,true);return false;
    }
    o->opened=true;o->retry_required=false;o->close_error=0;
    echoy_audio_skip_to(o->audio,us);echoy_audio_output_fault(o->audio,false);return true;
}
bool echoy_output_block(echoy_audio_output_t *o)
{
    if(!o||!o->opened||o->retry_required)return false;
    int16_t *pcm=o->blocks[o->block];echoy_audio_pcm(o->audio,pcm,ECHOY_AUDIO_FRAMES);
    o->last_error=o->ops.write(o->ops.ctx,pcm,sizeof o->blocks[0]);
    if(o->last_error){o->write_errors++;o->retry_required=true;echoy_audio_output_fault(o->audio,true);return false;}
    o->writes++;o->block=(o->block+1)%ECHOY_AUDIO_BLOCKS;return true;
}
