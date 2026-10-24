#include "echoy_fixed_audio.h"
#include "echoy_fixed_audio_format.h"
#include "echoy_device_pcm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail_alloc;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t n, size_t size)
{ return fail_alloc ? NULL : __real_calloc(n, size); }

static void seal(unsigned char *data, size_t size)
{ echoy_fixed_put(data + size - 4, echoy_fixed_checksum(data, size - 4)); }

int main(int argc, char **argv)
{
    assert(argc == 3);
    bool speaker = !strcmp(argv[2], "office");
    const float *gains = speaker ? echoy_office_role_gains : echoy_device_role_gains;
    if (!strcmp(argv[2], "reference")) gains = NULL;
    FILE *f = fopen(argv[1], "rb"); assert(f);
    assert(!fseek(f, 0, SEEK_END)); long length = ftell(f); rewind(f);
    assert(length > 0); size_t size = (size_t)length;
    unsigned char *data = malloc(size + 1), *bad = malloc(size); assert(data && bad);
    assert(fread(data + 1, 1, size, f) == size); assert(!fclose(f));
    /* Deliberately unaligned input: decoding must not cast packed integers/floats. */
    echoy_fixed_audio_t view;
    assert(echoy_fixed_audio_open(data + 1, size, speaker, gains, &view));
    memmove(data, data + 1, size);
    assert(echoy_fixed_audio_open(data, size, speaker, gains, &view));
    for (unsigned i = 0; i < 3; i++) {
        echoy_audio_buffer_t *expected = NULL, *actual = NULL;
        assert(echoy_audio_render_cue(i, &expected) == ECHOY_AUDIO_OK);
        assert(echoy_fixed_audio_cue(&view, i, &actual) == ECHOY_AUDIO_OK);
        assert(actual->frames == expected->frames);
        assert(!memcmp(actual->samples, expected->samples, actual->frames * sizeof(float)));
        echoy_audio_free_buffer(expected); echoy_audio_free_buffer(actual);
        actual = (void *)1; fail_alloc = true;
        assert(echoy_fixed_audio_cue(&view, i, &actual) == ECHOY_AUDIO_NO_MEMORY);
        assert(!actual); fail_alloc = false;
    }
    for (unsigned i = 0; i < 4; i++) {
        echoy_audio_preview_pool_t expected, actual;
        assert(echoy_audio_render_preview_pool(i, speaker, gains ? gains[i] : 1, &expected) == ECHOY_AUDIO_OK);
        assert(echoy_fixed_audio_preview(&view, i, &actual) == ECHOY_AUDIO_OK);
        assert(actual.phrase->frames == expected.phrase->frames);
        assert(!memcmp(actual.phrase->samples, expected.phrase->samples, actual.phrase->frames * sizeof(float)));
        assert(!memcmp(actual.source_frames, expected.source_frames, sizeof(actual.source_frames)));
        assert(!memcmp(actual.short_frames, expected.short_frames, sizeof(actual.short_frames)));
        echoy_audio_free_preview_pool(&expected); echoy_audio_free_preview_pool(&actual);
        memset(&actual, 0xff, sizeof(actual)); fail_alloc = true;
        assert(echoy_fixed_audio_preview(&view, i, &actual) == ECHOY_AUDIO_NO_MEMORY);
        assert(!actual.phrase); fail_alloc = false;
    }
    assert(!echoy_fixed_audio_open(data, size, !speaker, gains, &view));
    float wrong[4] = {1, 1, 1, 1.1f};
    assert(!echoy_fixed_audio_open(data, size, speaker, wrong, &view));
    for (size_t n = 0; n < ECHOY_FIXED_HEADER + ECHOY_FIXED_ENTRIES * ECHOY_FIXED_ENTRY; n++)
        assert(!echoy_fixed_audio_open(data, n, speaker, gains, &view));
    assert(!echoy_fixed_audio_open(data, size - 1, speaker, gains, &view));
    assert(!echoy_fixed_audio_open(data, size + 1, speaker, gains, &view));
    /* Each header/table/payload corruption must fail, even with a valid checksum. */
    const size_t fields[] = {0, 8, 12, 16, 20, 24, 28, 32, 48, 52, 56, 60,
        48 + 3 * 80 + 16, 48 + 3 * 80 + 48};
    for (size_t i = 0; i < sizeof(fields)/sizeof(*fields); i++) {
        memcpy(bad, data, size); echoy_fixed_put(bad + fields[i], UINT32_MAX); seal(bad, size);
        assert(!echoy_fixed_audio_open(bad, size, speaker, gains, &view));
        assert(!view.bytes);
    }
    memcpy(bad, data, size); bad[size - 10] ^= 1;
    assert(!echoy_fixed_audio_open(bad, size, speaker, gains, &view));
    memcpy(bad, data, size); echoy_fixed_put(bad + ECHOY_FIXED_DATA, 0x7fc00000); seal(bad, size);
    assert(!echoy_fixed_audio_open(bad, size, speaker, gains, &view));
    memcpy(data + 1, bad, size); /* Unaligned invalid data remains rejected. */
    assert(!echoy_fixed_audio_open(data + 1, size, speaker, gains, &view));
    free(data); free(bad);
    puts("fixed audio: exact synthesis, onset bounds, profile, corruption and allocation checks passed");
}
