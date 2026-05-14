#include "jsc_embed.h"

extern "C" int jsc_embed_lookup(const char* path,
                                const unsigned char** out_data, size_t* out_len,
                                const char** out_mime) {
    (void)path;
    (void)out_data;
    (void)out_len;
    (void)out_mime;
    return 0;
}
