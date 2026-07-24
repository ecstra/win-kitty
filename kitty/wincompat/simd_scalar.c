// simde is not packaged for this mingw, so the vectorized 128/256-bit string
// variants (simd-string-128.c, simd-string-256.c) are excluded. simd-string.c
// still references them, so provide them here as the scalar implementations.
// They are correct, just not vectorized; the dispatcher may still select them.
#include "../data-types.h"
#include "../simd-string.h"

bool utf8_decode_to_esc_128(UTF8Decoder *d, const uint8_t *src, size_t src_sz) { return utf8_decode_to_esc_scalar(d, src, src_sz); }
bool utf8_decode_to_esc_256(UTF8Decoder *d, const uint8_t *src, size_t src_sz) { return utf8_decode_to_esc_scalar(d, src, src_sz); }

static const uint8_t*
find_either(const uint8_t *haystack, const size_t sz, const uint8_t a, const uint8_t b) {
    for (const uint8_t *limit = haystack + sz; haystack < limit; haystack++)
        if (*haystack == a || *haystack == b) return haystack;
    return NULL;
}
const uint8_t* find_either_of_two_bytes_128(const uint8_t *haystack, const size_t sz, const uint8_t a, const uint8_t b) { return find_either(haystack, sz, a, b); }
const uint8_t* find_either_of_two_bytes_256(const uint8_t *haystack, const size_t sz, const uint8_t a, const uint8_t b) { return find_either(haystack, sz, a, b); }

static void
xor64(const uint8_t key[64], uint8_t* data, const size_t data_sz) { for (size_t i = 0; i < data_sz; i++) data[i] ^= key[i & 63]; }
void xor_data64_128(const uint8_t key[64], uint8_t* data, const size_t data_sz) { xor64(key, data, data_sz); }
void xor_data64_256(const uint8_t key[64], uint8_t* data, const size_t data_sz) { xor64(key, data, data_sz); }
