#include "echoy_strip_image.h"
#include "../../projects/echoy/ui/strip/labels.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
int main(int argc,char**argv)
{
    assert(argc==2);unsigned alphas[]={0,1,64,128,255};
    for(unsigned en=0;en<2;en++)for(unsigned k=0;k<4;k++)for(unsigned i=0;i<5;i++){
        uint8_t*encoded=NULL;size_t size=0;uint32_t color=echoy_strip_glyph_masks[en][k].color_rgb888;
        assert(echoy_strip_glyph_qoi(en,k,alphas[i],color,&encoded,&size));
        char path[1024];snprintf(path,sizeof(path),"%s/%u-%u-%u.qoi",argv[1],en,k,alphas[i]);
        FILE*f=fopen(path,"wb");assert(f);assert(fwrite(encoded,1,size,f)==size);fclose(f);free(encoded);
        snprintf(path,sizeof(path),"%s/%u-%u.mask",argv[1],en,k);f=fopen(path,"wb");assert(f);fwrite(echoy_strip_glyph_masks[en][k].alpha,1,3072,f);fclose(f);
    }
    uint8_t*out=(void*)1;size_t size=7;
    assert(!echoy_strip_glyph_qoi(0,4,255,0,&out,&size)&&!out&&!size);
    puts("PASS glyph boundary cases");
}
