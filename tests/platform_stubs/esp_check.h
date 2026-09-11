#include "esp_err.h"
#define ESP_RETURN_ON_FALSE(test, err, tag, ...) do { (void)(tag); if (!(test)) return (err); } while(0)
