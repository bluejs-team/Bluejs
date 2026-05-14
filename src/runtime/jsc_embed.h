/*
 * blue_embed.h - Embedded public/ assets lookup (built by blue -build).
 * jsc_embed_stub.cpp provides empty table; generated jsc_assets.cpp replaces it.
 */

#ifndef JSC_EMBED_H
#define JSC_EMBED_H

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Normalize path (strip leading '/', compare case-sensitive). Returns 1 on hit. */
int jsc_embed_lookup(const char* path, const unsigned char** out_data,
                     size_t* out_len, const char** out_mime);

#ifdef __cplusplus
}
#endif

#endif /* JSC_EMBED_H */
