#include "echoy_audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "oracle_cases.h"

static void save(const char *directory, const char *name, const char *kind, const float *data, size_t frames)
{
    char path[1024];
    int count = snprintf(path, sizeof(path), "%s/%s-%s.f32", directory, name, kind);
    assert(count > 0 && count < (int)sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(data, sizeof(*data), frames, file) == frames);
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *kinds[] = {"source", "foreground", "full"};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        echoy_audio_loops_t *loops = NULL;
        echoy_audio_result_t result = echoy_audio_prepare(&cases[i].plan, cases[i].original, &loops);
        if (result != ECHOY_AUDIO_OK) { fprintf(stderr, "%s: result %d\n", cases[i].name, result); return 1; }
        for (unsigned kind = 0; kind < 3; ++kind) save(argv[1], cases[i].name, kinds[kind], loops->samples[kind], loops->frames);
        echoy_audio_free_loops(loops);
    }
    const char *cues[] = {"tap", "guide", "capture"};
    for (unsigned i = 0; i < 3; ++i) {
        echoy_audio_buffer_t *buffer = NULL;
        assert(echoy_audio_render_cue((echoy_audio_cue_t)i, &buffer) == ECHOY_AUDIO_OK);
        save(argv[1], "cue", cues[i], buffer->samples, buffer->frames);
        echoy_audio_free_buffer(buffer);
    }
    return 0;
}
