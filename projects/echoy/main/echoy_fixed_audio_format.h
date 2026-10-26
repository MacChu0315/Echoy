#pragma once
#include <stddef.h>
#include <stdint.h>

/* Private build artifact, not a persisted/user data format. All integers and
 * IEEE binary32 samples are little-endian. Entries are canonical: three cues,
 * then four roles. Each entry has kind/id/frames/offset and eight start/length
 * pairs (stored as two arrays). The final word is FNV-1a over preceding bytes. */
#define ECHOY_FIXED_HEADER 48u
#define ECHOY_FIXED_ENTRY 80u
#define ECHOY_FIXED_ENTRIES 7u
#define ECHOY_FIXED_DATA (ECHOY_FIXED_HEADER + ECHOY_FIXED_ENTRY * ECHOY_FIXED_ENTRIES)
#define ECHOY_FIXED_FRAMES (8640u + 720u + 720u + 4u * 60000u)
#define ECHOY_FIXED_SIZE (ECHOY_FIXED_DATA + ECHOY_FIXED_FRAMES * 4u + 4u)
#define ECHOY_FIXED_MAGIC "ECHOYFA\001"

static inline uint32_t echoy_fixed_get(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static inline void echoy_fixed_put(unsigned char *p, uint32_t v)
{ for (unsigned i=0; i<4; i++) p[i]=(unsigned char)(v>>(8*i)); }
static inline uint32_t echoy_fixed_checksum(const unsigned char *p, size_t size)
{
    uint32_t hash=2166136261u;
    for (size_t i=0; i<size; i++) hash=(hash^p[i])*16777619u;
    return hash;
}
