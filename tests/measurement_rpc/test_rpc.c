#include "echoy_measurement_diagnostics.h"
#include "esp_iris.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static esp_iris_rpc_handler_t handlers[5];
static void *contexts[5];
static unsigned starts, cancels, result_reads, fail_method;
static unsigned fail_unreg;
static bool fail_alloc;
static int identity;
void fake_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
static echoy_output_measurement_status_t status = {
    .phase=ECHOY_OUTPUT_MEASUREMENT_COMPLETE,.generation=7,.requested_generation=7,
    .runs_started=1,.runs_remaining=1,.frames_written=216000,.frames_captured=216000,
    .elapsed_us=9000123,.input_gain_db=18,.result_ready=true};
esp_err_t esp_iris_rpc_register(uint16_t s,uint16_t m,esp_iris_rpc_handler_t fn,void *ctx)
{ assert(s==0xeca2 && m>=1 && m<=4); if(m==fail_method)return ESP_FAIL;
  handlers[m]=fn; contexts[m]=ctx; return ESP_OK; }
esp_err_t esp_iris_rpc_unregister(uint16_t s,uint16_t m)
{ assert(s==0xeca2); if(m==fail_unreg)return ESP_ERR_INVALID_STATE;
  handlers[m]=NULL; contexts[m]=NULL; return ESP_OK; }
void *heap_caps_calloc(size_t n,size_t s,unsigned caps)
{ assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)); return fail_alloc?NULL:calloc(n,s); }
esp_err_t echoy_device_measure_get(echoy_device_handle_t d,echoy_output_measurement_status_t *out)
{ assert(d==(void*)&identity); *out=status; return ESP_OK; }
esp_err_t echoy_device_measure_request(echoy_device_handle_t d,bool start,unsigned gain,
                                     echoy_output_measurement_status_t *out)
{ assert(d==(void*)&identity); if(start){ assert(gain==18); ++starts; } else ++cancels;
  *out=status; return ESP_OK; }
esp_err_t echoy_device_measure_result(echoy_device_handle_t d,unsigned run,
                                    echoy_output_measurement_result_t *out)
{ assert(d==(void*)&identity); ++result_reads; if(run!=7)return ESP_ERR_INVALID_STATE;
  memset(out,0,sizeof(*out)); out->flags=256; out->frames_captured=216000; out->tones_analyzed=15;
  out->noise.sample_count=7200; out->noise.rms=.001f;
  out->tones[0]=(echoy_output_measurement_tone_t){.frequency_hz=220,.pcm_peak=.12f,
      .sample_count=4800,.flags=512,.adc_peak=.2f,.rms=.1f,.harmonic_amplitude={.14f,.014f},
      .harmonic_ratio={.1f},.harmonic_rss_ratio=.1f,.fundamental_snr_db=40,
      .excerpt_count=256,.excerpt={12345}}; return ESP_OK; }
static esp_err_t call(unsigned m,const uint8_t *p,size_t n,uint8_t *out,size_t cap,size_t *size)
{ esp_iris_rpc_request_t req={.service_id=0xeca2,.method_id=m,.payload=p,.payload_size=n};
  return handlers[m](&req,out,cap,size,contexts[m]); }
int main(void)
{
 assert(echoy_measurement_diagnostics_register(NULL)==ESP_ERR_INVALID_ARG);
 fail_method=3;
 assert(echoy_measurement_diagnostics_register((void*)&identity)==ESP_FAIL);
 assert(!handlers[1]&&!handlers[2]); fail_method=0;
 fail_method=3; fail_unreg=2;
 assert(echoy_measurement_diagnostics_register((void*)&identity)==ESP_FAIL);
 uint8_t denied[1024], denied_gain=18; size_t denied_size=999;
 assert(handlers[2]);
 assert(call(2,&denied_gain,1,denied,sizeof(denied),&denied_size)==ESP_ERR_INVALID_STATE);
 assert(denied_size==0&&starts==0);
 fail_method=fail_unreg=0; handlers[2]=NULL; contexts[2]=NULL;
 assert(echoy_measurement_diagnostics_register((void*)&identity)==ESP_OK);
 assert(starts==0&&cancels==0); uint8_t out[1024], gain=18; size_t n=999;
 assert(call(2,&gain,1,out,32,&n)==ESP_ERR_INVALID_SIZE&&n==0&&starts==0);
 assert(call(2,NULL,1,out,sizeof(out),&n)==ESP_ERR_INVALID_SIZE&&starts==0);
 uint8_t bad=19;
 assert(call(2,&bad,1,out,sizeof(out),&n)==ESP_ERR_INVALID_ARG&&starts==0);
 assert(call(1,&gain,1,out,sizeof(out),&n)==ESP_ERR_INVALID_SIZE&&starts==0);
 assert(call(1,NULL,0,out,sizeof(out),&n)==ESP_OK&&n<sizeof(out)); puts((char*)out);
 assert(call(2,&gain,1,out,sizeof(out),&n)==ESP_OK&&starts==1);
 assert(call(3,NULL,0,out,sizeof(out),&n)==ESP_OK&&cancels==1);
 uint8_t p[5]={7,0,0,0,255};
 fail_alloc=true; assert(call(4,p,5,out,sizeof(out),&n)==ESP_ERR_NO_MEM); fail_alloc=false;
 assert(call(4,p,4,out,sizeof(out),&n)==ESP_ERR_INVALID_SIZE);
 p[4]=15; assert(call(4,p,5,out,sizeof(out),&n)==ESP_ERR_INVALID_ARG);
 p[4]=255; assert(call(4,p,5,out,sizeof(out),&n)==ESP_OK); puts((char*)out);
 p[4]=0; assert(call(4,p,5,out,sizeof(out),&n)==ESP_OK); puts((char*)out);
 assert(!strstr((char*)out,"excerpt")&&!strstr((char*)out,"12345"));
 p[0]=6; assert(call(4,p,5,out,sizeof(out),&n)==ESP_ERR_INVALID_STATE&&n==0);
 assert(starts==1&&cancels==1&&result_reads==3);
 return 0;
}
