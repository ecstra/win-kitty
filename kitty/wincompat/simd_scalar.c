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

// The compositing and ASCII run variants mirror the scalar ones in simd-string.c,
// which are static there. The per pixel helpers come from simd-string.h, so the
// results stay bit identical to the scalar path.
static void
blend_straight(uint8_t *dst, const uint8_t *src, size_t num_pixels) { for (size_t i = 0; i < num_pixels; i++) blend_pixel_over_straight(dst + 4 * i, src + 4 * i); }
void blend_over_straight_128(uint8_t *dst, const uint8_t *src, size_t num_pixels) { blend_straight(dst, src, num_pixels); }
void blend_over_straight_256(uint8_t *dst, const uint8_t *src, size_t num_pixels) { blend_straight(dst, src, num_pixels); }

static void
blend_opaque(uint8_t *dst, unsigned dst_bpp, const uint8_t *src, size_t num_pixels) { for (size_t i = 0; i < num_pixels; i++) blend_pixel_over_opaque(dst + dst_bpp * i, src + 4 * i, dst_bpp); }
void blend_over_opaque_128(uint8_t *dst, unsigned dst_bpp, const uint8_t *src, size_t num_pixels) { blend_opaque(dst, dst_bpp, src, num_pixels); }
void blend_over_opaque_256(uint8_t *dst, unsigned dst_bpp, const uint8_t *src, size_t num_pixels) { blend_opaque(dst, dst_bpp, src, num_pixels); }

static void
alpha_mask(uint32_t *dst, const uint8_t *mask, size_t num_pixels, uint32_t color_rgb) {
    const uint32_t col = (color_rgb << 8) & 0xffffff00;
    for (size_t i = 0; i < num_pixels; i++) {
        const uint32_t dst_alpha = dst[i] & 0xff, mask_alpha = mask[i];
        dst[i] = col | MAX(mask_alpha, dst_alpha);
    }
}
void composite_alpha_mask_128(uint32_t *dst, const uint8_t *mask, size_t num_pixels, uint32_t color_rgb) { alpha_mask(dst, mask, num_pixels, color_rgb); }
void composite_alpha_mask_256(uint32_t *dst, const uint8_t *mask, size_t num_pixels, uint32_t color_rgb) { alpha_mask(dst, mask, num_pixels, color_rgb); }

static size_t
ascii_run(const uint32_t *chars, const size_t sz) {
    size_t n = 0;
    while (n < sz && (chars[n] - 32u) < 95u) n++;
    return n;
}
size_t printable_ascii_run_length_128(const uint32_t *chars, const size_t sz) { return ascii_run(chars, sz); }
size_t printable_ascii_run_length_256(const uint32_t *chars, const size_t sz) { return ascii_run(chars, sz); }
