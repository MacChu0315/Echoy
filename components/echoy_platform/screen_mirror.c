// SPDX-License-Identifier: Apache-2.0
#include "echoy_services.h"
#include <string.h>
static void reset_pass(echoy_mirror_t *m){m->expected_offset=0;m->full_pass_complete=false;}
bool echoy_mirror_begin(echoy_mirror_t *m){
    if(!m||m->pixels||!m->capture||!m->release)return false;
    uint8_t *pixels=NULL;size_t size=0;
    if(!m->capture(m->ctx,&pixels,&size)||!pixels||!size){if(pixels)m->release(m->ctx,pixels);return false;}
    m->pixels=pixels;m->size=size;reset_pass(m);return true;
}
static bool refresh(echoy_mirror_t *m){
    uint8_t *pixels=NULL;size_t size=0;
    /* Iris retains begin's dimensions/total size throughout its pull stream.
     * Keep the old frame and completed pass intact until a valid replacement
     * exists, so failure can retry offset zero without leaking or mixing frames. */
    if(!m->capture(m->ctx,&pixels,&size)||!pixels||size!=m->size){
        if(pixels)m->release(m->ctx,pixels);
        return false;
    }
    m->release(m->ctx,m->pixels);m->pixels=pixels;reset_pass(m);return true;
}
bool echoy_mirror_read(echoy_mirror_t *m,size_t offset,uint8_t *out,size_t cap,size_t *n){
    if(n)*n=0;
    if(!m)return false;
    if(!m->pixels||!out||!cap||!n||offset>=m->size){reset_pass(m);return false;}
    if(offset==0&&m->full_pass_complete&&!refresh(m))return false;
    if(offset!=m->expected_offset)reset_pass(m);
    *n=m->size-offset;if(*n>cap)*n=cap;memcpy(out,m->pixels+offset,*n);
    /* After an out-of-order chunk only a new offset-zero read can start a
     * traversal. Merely reaching the tail does not complete a frame pass. */
    if(offset==m->expected_offset){
        m->expected_offset=offset+*n;
        m->full_pass_complete=m->expected_offset==m->size;
    }
    return true;
}
void echoy_mirror_end(echoy_mirror_t *m){
    if(!m)return;
    if(m->pixels)m->release(m->ctx,m->pixels);
    m->pixels=NULL;m->size=0;reset_pass(m);
}
