#pragma once
#include <stddef.h>
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_SPIRAM 4
void *heap_caps_calloc(size_t count, size_t size, unsigned caps);
void *heap_caps_malloc(size_t size, unsigned caps);
size_t heap_caps_get_free_size(unsigned caps);
