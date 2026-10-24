#include "echoy_fixed_audio.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"

extern const unsigned char echoy_fixed_audio_start[] asm("_binary_echoy_fixed_audio_bin_start");
extern const unsigned char echoy_fixed_audio_end[] asm("_binary_echoy_fixed_audio_bin_end");
#else
extern const unsigned char echoy_fixed_audio_bytes[];
extern const size_t echoy_fixed_audio_size;
#endif
#if CONFIG_ECHOY_VERIFY_PREBUILT_AUDIO
bool echoy_fixed_audio_verify(const echoy_fixed_audio_t *,bool,const float[4]);
#endif

bool echoy_fixed_audio_embedded(bool speaker, const float gains[4], echoy_fixed_audio_t *out)
{
#ifdef ESP_PLATFORM
    if (!echoy_fixed_audio_open(echoy_fixed_audio_start,
            (size_t)(echoy_fixed_audio_end-echoy_fixed_audio_start),speaker,gains,out)) {
        ESP_LOGE("echoy_fixed", "fixed audio resource invalid or profile mismatch");
        return false;
    }
#else
    if (!echoy_fixed_audio_open(echoy_fixed_audio_bytes,echoy_fixed_audio_size,speaker,gains,out)) return false;
#endif
#if CONFIG_ECHOY_VERIFY_PREBUILT_AUDIO
    if (!echoy_fixed_audio_verify(out,speaker,gains)) { *out=(echoy_fixed_audio_t){0}; return false; }
#endif
    return true;
}
