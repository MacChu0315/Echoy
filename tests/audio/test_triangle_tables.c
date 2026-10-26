/* Include the production implementation to compare its private table builder
 * against generated lookup data without publishing a new application API. */
#include "echoy_audio.c"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    workspace_t *work=malloc(sizeof(*work)); assert(work); workspace_init(work);
    float reference[TRIANGLE_SIZE]; unsigned cached=0;
    for(unsigned note=0;note<128;note++) {
        unsigned harmonics=(unsigned)((SR/2-1)/midi((int)note));
        const float *table=prebuilt_triangle(harmonics);
        if(note>=48 && note<=84)assert(table);
        if(!table)continue;
        triangle_table(reference,harmonics,work->sine);
        assert(!memcmp(reference,table,sizeof(reference))); ++cached;
    }
    assert(!prebuilt_triangle(UINT32_MAX));
    printf("PASS %u MIDI pitches use bit-identical generated tables; other pitches retain synthesis\n",cached);
    free(work);
}
