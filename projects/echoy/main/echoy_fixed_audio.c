#include "echoy_fixed_audio.h"
#include "echoy_fixed_audio_format.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

_Static_assert(sizeof(float)==4 && FLT_RADIX==2 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,
               "Fixed audio requires IEEE binary32");
_Static_assert(ECHOY_AUDIO_LOOP_FRAMES==60000 && ECHOY_PREVIEW_COUNT==8,
               "Update fixed audio schema when loop/window counts change");

static float sample(const unsigned char *p)
{
    uint32_t bits=echoy_fixed_get(p); float value;
    memcpy(&value,&bits,sizeof(value)); return value;
}

bool echoy_fixed_audio_open(const void *bytes, size_t size, bool speaker,
                           const float gains[4], echoy_fixed_audio_t *out)
{
    if (out) *out=(echoy_fixed_audio_t){0};
    if (!bytes || !out || size!=ECHOY_FIXED_SIZE) return false;
    const unsigned char *p=bytes;
    if (memcmp(p,ECHOY_FIXED_MAGIC,8) || echoy_fixed_get(p+8)!=1 ||
        echoy_fixed_get(p+12)!=ECHOY_AUDIO_SAMPLE_RATE || echoy_fixed_get(p+16)!=ECHOY_PREVIEW_SEED ||
        echoy_fixed_get(p+20)!=ECHOY_PREVIEW_COUNT || echoy_fixed_get(p+24)!=(unsigned)speaker ||
        echoy_fixed_get(p+28)!=ECHOY_FIXED_ENTRIES) return false;
    for (unsigned i=0; i<4; i++) {
        float gain=gains?gains[i]:1;
        if (!isfinite(gain) || gain<0 || gain>4 || sample(p+32+i*4)!=gain) return false;
    }
    if (echoy_fixed_checksum(p,size-4)!=echoy_fixed_get(p+size-4)) return false;
    size_t offset=ECHOY_FIXED_DATA;
    for (unsigned i=0; i<ECHOY_FIXED_ENTRIES; i++) {
        const unsigned char *entry=p+ECHOY_FIXED_HEADER+i*ECHOY_FIXED_ENTRY;
        unsigned frames=i<3 ? (i==0?8640:720) : ECHOY_AUDIO_LOOP_FRAMES;
        if (echoy_fixed_get(entry)!=(i<3?0u:1u) || echoy_fixed_get(entry+4)!=(i<3?i:i-3) ||
            echoy_fixed_get(entry+8)!=frames || echoy_fixed_get(entry+12)!=offset) return false;
        for (unsigned j=0; j<ECHOY_PREVIEW_COUNT; j++) {
            uint32_t start=echoy_fixed_get(entry+16+j*4), length=echoy_fixed_get(entry+48+j*4);
            if (i<3) { if (start || length) return false; }
            else {
                if (start>=frames || length<=384 || length>ECHOY_AUDIO_SAMPLE_RATE/2 ||
                    length>frames-start) return false;
                for (unsigned k=0; k<j; k++)
                    if (echoy_fixed_get(entry+16+k*4)==start) return false;
            }
        }
        for (unsigned j=0; j<frames; j++) if (!isfinite(sample(p+offset+j*4))) return false;
        offset+=frames*4;
    }
    if (offset!=size-4) return false;
    out->bytes=p; return true;
}

static echoy_audio_result_t load(const echoy_fixed_audio_t *view, unsigned index,
                                echoy_audio_buffer_t **out)
{
    if (out) *out=NULL;
    if (!out || !view || !view->bytes || index>=ECHOY_FIXED_ENTRIES) return ECHOY_AUDIO_INVALID;
    const unsigned char *entry=view->bytes+ECHOY_FIXED_HEADER+index*ECHOY_FIXED_ENTRY;
    size_t frames=echoy_fixed_get(entry+8);
#ifdef ESP_PLATFORM
    echoy_audio_buffer_t *buffer=heap_caps_calloc(1,sizeof(*buffer)+frames*sizeof(float),
                                               MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    echoy_audio_buffer_t *buffer=calloc(1,sizeof(*buffer)+frames*sizeof(float));
#endif
    if (!buffer) return ECHOY_AUDIO_NO_MEMORY;
    buffer->frames=frames; buffer->samples=(float *)(buffer+1);
    const unsigned char *data=view->bytes+echoy_fixed_get(entry+12);
    for (size_t i=0; i<frames; i++) buffer->samples[i]=sample(data+i*4);
    *out=buffer; return ECHOY_AUDIO_OK;
}
echoy_audio_result_t echoy_fixed_audio_cue(const echoy_fixed_audio_t *view,
    echoy_audio_cue_t cue, echoy_audio_buffer_t **out)
{
    if (out) *out=NULL;
    if ((unsigned)cue> ECHOY_AUDIO_CAPTURE_START) return ECHOY_AUDIO_INVALID;
    return load(view,(unsigned)cue,out);
}
echoy_audio_result_t echoy_fixed_audio_preview(const echoy_fixed_audio_t *view,
    echoy_role_t role, echoy_audio_preview_pool_t *out)
{
    if (out) memset(out,0,sizeof(*out));
    if (!out || (unsigned)role>ECHOY_HARMONY) return ECHOY_AUDIO_INVALID;
    echoy_audio_result_t result=load(view,3+(unsigned)role,&out->phrase);
    if (result!=ECHOY_AUDIO_OK) return result;
    const unsigned char *entry=view->bytes+ECHOY_FIXED_HEADER+(3+(unsigned)role)*ECHOY_FIXED_ENTRY;
    for (unsigned j=0; j<ECHOY_PREVIEW_COUNT; j++) {
        out->source_frames[j]=echoy_fixed_get(entry+16+j*4);
        out->short_frames[j]=echoy_fixed_get(entry+48+j*4);
    }
    return ECHOY_AUDIO_OK;
}
