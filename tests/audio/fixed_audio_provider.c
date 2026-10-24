#include "echoy_fixed_audio.h"
#include <stdio.h>
#include <stdlib.h>

/* Test fixture only: persistent, immutable file-backed bytes stand in for the
 * firmware's linker symbols. The portable production loader is unchanged. */
bool echoy_fixed_audio_embedded(bool speaker, const float gains[4], echoy_fixed_audio_t *out)
{
    static unsigned char *data[2]; static size_t size[2];
    unsigned profile=speaker?1:0;
    if (!data[profile]) {
        const char *path=getenv(speaker?"ECHOY_TEST_OFFICE":"ECHOY_TEST_REFERENCE");
        FILE *f=path?fopen(path,"rb"):NULL;
        if (!f) return false;
        if (fseek(f,0,SEEK_END)) { fclose(f); return false; }
        long length=ftell(f); rewind(f);
        if (length<=0) { fclose(f); return false; }
        size[profile]=(size_t)length; data[profile]=malloc(size[profile]);
        if (!data[profile] || fread(data[profile],1,size[profile],f)!=size[profile]) {
            free(data[profile]); data[profile]=NULL; fclose(f); return false;
        }
        fclose(f);
    }
    return echoy_fixed_audio_open(data[profile],size[profile],speaker,gains,out);
}
