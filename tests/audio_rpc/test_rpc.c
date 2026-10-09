#include "echoy_audio_diagnostics.h"
#include "esp_iris.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static esp_iris_rpc_handler_t handlers[4];
static unsigned queued, stopped, reads, fail_method, unregistered;
static esp_err_t enqueue_error;
void fake_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
static void *const context = (void *)1;
esp_err_t esp_iris_rpc_register(uint16_t service, uint16_t method, esp_iris_rpc_handler_t h, void *ctx)
{
    assert(service==0xeca0&&method>=1&&method<=3&&ctx==context);
    if(method==fail_method)return ESP_FAIL;
    handlers[method]=h;return ESP_OK;
}
esp_err_t esp_iris_rpc_unregister(uint16_t service, uint16_t method)
{
    assert(service==0xeca0&&handlers[method]);handlers[method]=NULL;++unregistered;return ESP_OK;
}
esp_err_t echoy_device_probe_request(echoy_device_handle_t d,bool start)
{
    assert(d==context);if(enqueue_error)return enqueue_error;
    if(start)++queued;else ++stopped;return ESP_OK;
}
esp_err_t echoy_device_probe_get(echoy_device_handle_t d,echoy_device_diagnostics_t *out)
{
    assert(d==context);++reads;
    *out=(echoy_device_diagnostics_t){.phase=ECHOY_PROBE_DONE,.generation=6,.requested_generation=7,
      .frames_accepted=24000,.volume_percent=100,
      .before={.registers={0x80,0x3f,0x0c,0x60,188,0x83,0x11},.valid_mask=127,.sampled_at_us=100},
      .after={.registers={0x80,0x3f,0x0c,0,188,0x83,0x11},.valid_mask=127,.sampled_at_us=1100000,.pa_requested=true},
      .output={.frames_written=24720,.nonzero_frames=23000,.peak_pcm=8192}};
    return ESP_OK;
}
int main(void)
{
    assert(echoy_audio_diagnostics_register(NULL)==ESP_ERR_INVALID_ARG);
    fail_method=2;assert(echoy_audio_diagnostics_register(context)==ESP_FAIL);
    assert(unregistered==1&&!handlers[1]);fail_method=0;
    assert(echoy_audio_diagnostics_register(context)==ESP_OK);
    uint8_t reply[1024];size_t size=0;
    esp_iris_rpc_request_t req={.service_id=0xeca0,.method_id=1};
    assert(handlers[1](&req,reply,sizeof(reply),&size,context)==ESP_OK);
    assert(size==strlen((char*)reply)&&queued==0&&stopped==0&&reads==1);
    puts((char*)reply);
    req.method_id=2;req.payload_size=1;
    assert(handlers[2](&req,reply,sizeof(reply),&size,context)==ESP_ERR_INVALID_SIZE&&queued==0&&size==0);
    req.payload_size=0;
    assert(handlers[2](&req,reply,8,&size,context)==ESP_ERR_INVALID_SIZE&&queued==0&&size==0);
    enqueue_error=ESP_ERR_INVALID_STATE;
    assert(handlers[2](&req,reply,sizeof(reply),&size,context)==ESP_ERR_INVALID_STATE&&queued==0);
    enqueue_error=ESP_OK;
    assert(handlers[2](&req,reply,sizeof(reply),&size,context)==ESP_OK&&queued==1);
    req.method_id=3;assert(handlers[3](&req,reply,sizeof(reply),&size,context)==ESP_OK&&stopped==1);
    req.method_id=1;assert(handlers[1](&req,reply,8,&size,context)==ESP_ERR_INVALID_SIZE&&size==0);
    req.method_id=4;assert(handlers[1](&req,reply,sizeof(reply),&size,context)==ESP_ERR_NOT_SUPPORTED);
    fprintf(stderr,"PASS RPC registration rollback, empty payload, bounded response, command errors and cache reads\n");
}
