/*
 * Copyright (C) 2026 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "base64.h"

// Write the kitty graphics protocol escape codes for a 32-bit RGBA image to fp.
static inline void
fprint_rgba32(FILE *fp, const uint32_t *rgba, uint32_t width, uint32_t height) {
    const uint8_t *data = (const uint8_t *)rgba;
    const size_t total_bytes = (size_t)width * height * sizeof(uint32_t);
    uint8_t b64_buf[4096];
    size_t offset = 0;
    bool first = true;
    while (offset < total_bytes) {
        size_t chunk = total_bytes - offset;
        if (chunk > 3072) chunk = 3072;
        size_t b64_len = sizeof(b64_buf);
        base64_encode8(data + offset, chunk, b64_buf, &b64_len, false);
        offset += chunk;
        const bool last = (offset >= total_bytes);

        if (first) {
            fprintf(fp, "\x1b_Ga=T,f=32,s=%u,v=%u,m=%d;", width, height, last ? 0 : 1);
            first = false;
        } else fprintf(fp, "\x1b_Gm=%d;", last ? 0 : 1);
        fwrite(b64_buf, 1, b64_len, fp);
        fputs("\x1b\\", fp);
    }
}

static inline void
print_rgba32(const uint32_t *rgba, uint32_t width, uint32_t height) { fprint_rgba32(stdout, rgba, width, height); }
