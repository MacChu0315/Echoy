/* Build-host executable: use the production synthesizer, never a translated
 * approximation. This file is not compiled into the device application. */
#include "echoy_fixed_audio_format.h"
#include "echoy_audio.h"
#include "echoy_device_pcm.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_float(unsigned char *p, float value)
{ uint32_t bits; memcpy(&bits,&value,4); echoy_fixed_put(p,bits); }

int main(int argc, char **argv)
{
    _Static_assert(sizeof(float)==4 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,"binary32 required");
    if (argc!=3) return 2;
    bool speaker=!strcmp(argv[1],"office");
    const float unity[4]={1,1,1,1};
    const float *gains=speaker?echoy_office_role_gains:echoy_device_role_gains;
    if (!strcmp(argv[1],"reference")) gains=unity;
    else if (!speaker && strcmp(argv[1],"device")) return 2;
    unsigned char *data=calloc(1,ECHOY_FIXED_SIZE);
    if (!data) return 1;
    memcpy(data,ECHOY_FIXED_MAGIC,8);
    echoy_fixed_put(data+8,1); echoy_fixed_put(data+12,ECHOY_AUDIO_SAMPLE_RATE);
    echoy_fixed_put(data+16,ECHOY_PREVIEW_SEED); echoy_fixed_put(data+20,ECHOY_PREVIEW_COUNT);
    echoy_fixed_put(data+24,speaker); echoy_fixed_put(data+28,ECHOY_FIXED_ENTRIES);
    for (unsigned i=0; i<4; i++) put_float(data+32+i*4,gains[i]);
    size_t offset=ECHOY_FIXED_DATA;
    for (unsigned i=0; i<ECHOY_FIXED_ENTRIES; i++) {
        echoy_audio_preview_pool_t pool={0}; echoy_audio_buffer_t *buffer=NULL;
        echoy_audio_result_t result=i<3 ? echoy_audio_render_cue(i,&buffer) :
            echoy_audio_render_preview_pool(i-3,speaker,gains[i-3],&pool);
        if (i>=3) buffer=pool.phrase;
        if (result!=ECHOY_AUDIO_OK || !buffer || buffer->frames>(ECHOY_FIXED_SIZE-4-offset)/4) {
            free(data); return 1;
        }
        unsigned char *entry=data+ECHOY_FIXED_HEADER+i*ECHOY_FIXED_ENTRY;
        echoy_fixed_put(entry,i<3?0:1); echoy_fixed_put(entry+4,i<3?i:i-3);
        echoy_fixed_put(entry+8,(uint32_t)buffer->frames); echoy_fixed_put(entry+12,(uint32_t)offset);
        for (unsigned j=0; j<ECHOY_PREVIEW_COUNT; j++) {
            echoy_fixed_put(entry+16+j*4,pool.source_frames[j]);
            echoy_fixed_put(entry+48+j*4,pool.short_frames[j]);
        }
        for (size_t j=0; j<buffer->frames; j++) put_float(data+offset+j*4,buffer->samples[j]);
        offset+=buffer->frames*4;
        echoy_audio_free_buffer(buffer);
    }
    if (offset!=ECHOY_FIXED_SIZE-4) { free(data); return 1; }
    echoy_fixed_put(data+offset,echoy_fixed_checksum(data,offset));
    FILE *file=fopen(argv[2],"wb");
    if (!file) { free(data); return 1; }
    bool ok=fwrite(data,1,ECHOY_FIXED_SIZE,file)==ECHOY_FIXED_SIZE;
    if (fclose(file)) ok=false;
    free(data); return ok?0:1;
}
