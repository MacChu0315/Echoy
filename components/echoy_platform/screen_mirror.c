// SPDX-License-Identifier: Apache-2.0
#include "echoy_services.h"
#include <string.h>
bool echoy_mirror_begin(echoy_mirror_t *m){
    if(!m||m->pixels||!m->capture||!m->release)return false;
    uint8_t *pixels=NULL;size_t size=0;
    if(!m->capture(m->ctx,&pixels,&size)||!pixels||!size){if(pixels)m->release(m->ctx,pixels);return false;}
    m->pixels=pixels;m->size=size;return true;
}
bool echoy_mirror_read(echoy_mirror_t *m,size_t offset,uint8_t *out,size_t cap,size_t *n){
    if(n)*n=0;
    if(!m||!m->pixels||!out||!cap||!n||offset>=m->size)return false;
    *n=m->size-offset;if(*n>cap)*n=cap;memcpy(out,m->pixels+offset,*n);return true;
}
void echoy_mirror_end(echoy_mirror_t *m){if(!m)return;if(m->pixels)m->release(m->ctx,m->pixels);m->pixels=NULL;m->size=0;}
