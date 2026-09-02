#include "canvas.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"

static uint16_t *framebuffer = NULL;

static inline float clamp_unit(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }

    if (value > 1.0f) {
        return 1.0f;
    }

    return value;
}

bool canvas_begin(void) {
    framebuffer = (uint16_t *)heap_caps_malloc(
        (size_t)CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    return framebuffer != NULL;
}

uint16_t *canvas_pixels(void) {
    return framebuffer;
}

void canvas_fill(uint16_t color) {
    canvas_fill_rect(0, CANVAS_HEIGHT, 0, CANVAS_WIDTH, color);
}

void canvas_fill_rect(int top, int height, int left, int width, uint16_t color) {
    const int first = top < 0 ? 0 : top;
    const int last = top + height > CANVAS_HEIGHT ? CANVAS_HEIGHT : top + height;
    const int from = left < 0 ? 0 : left;
    const int to = left + width > CANVAS_WIDTH ? CANVAS_WIDTH : left + width;

    for (int row = first; row < last; row++) {
        uint16_t *pixel = &framebuffer[(size_t)row * CANVAS_WIDTH];
        for (int column = from; column < to; column++) {
            pixel[column] = color;
        }
    }
}

void canvas_blend(int x, int y, uint16_t color, uint8_t alpha) {
    if (alpha == 0 || x < 0 || y < 0 || x >= CANVAS_WIDTH || y >= CANVAS_HEIGHT) {
        return;
    }

    uint16_t *pixel = &framebuffer[(size_t)y * CANVAS_WIDTH + (size_t)x];
    if (alpha == 255) {
        *pixel = color;
        return;
    }

    const uint16_t behind = *pixel;
    const uint16_t inverse = (uint16_t)(255u - alpha);

    // Same exact-divide identity as canvas_scale, on sums that stay under 65025.
    const uint16_t red_sum = (uint16_t)(((color >> 11) & 0x1Fu) * alpha + ((behind >> 11) & 0x1Fu) * inverse);
    const uint16_t green_sum = (uint16_t)(((color >> 5) & 0x3Fu) * alpha + ((behind >> 5) & 0x3Fu) * inverse);
    const uint16_t blue_sum = (uint16_t)((color & 0x1Fu) * alpha + (behind & 0x1Fu) * inverse);

    const uint16_t red = (uint16_t)((red_sum + (red_sum >> 8) + 1) >> 8);
    const uint16_t green = (uint16_t)((green_sum + (green_sum >> 8) + 1) >> 8);
    const uint16_t blue = (uint16_t)((blue_sum + (blue_sum >> 8) + 1) >> 8);

    *pixel = (uint16_t)((red << 11) | (green << 5) | blue);
}

// Coverage of one pixel centre against the capsule swept along the segment.
static uint8_t capsule_coverage(float x, float y, float from_x, float from_y,
                                float span_x, float span_y, float span_length_squared,
                                float half_width) {
    float along = ((x - from_x) * span_x + (y - from_y) * span_y) / span_length_squared;
    along = clamp_unit(along);

    const float offset_x = x - (from_x + along * span_x);
    const float offset_y = y - (from_y + along * span_y);
    const float distance = sqrtf(offset_x * offset_x + offset_y * offset_y);

    const float coverage = half_width + 0.5f - distance;
    if (coverage <= 0.0f) {
        return 0;
    }

    if (coverage >= 1.0f) {
        return 255;
    }

    return (uint8_t)(coverage * 255.0f + 0.5f);
}

void canvas_line(float from_x, float from_y, float to_x, float to_y, float width,
                 uint16_t color, uint8_t alpha) {
    const float half_width = width * 0.5f;
    const float reach = half_width + 0.5f;
    const float span_x = to_x - from_x;
    const float span_y = to_y - from_y;
    const float span_length_squared = span_x * span_x + span_y * span_y;

    if (span_length_squared < 1e-6f || alpha == 0) {
        return;
    }

    const float span_length = sqrtf(span_length_squared);

    // Walk the dominant axis so the perpendicular span stays a few pixels wide.
    if (fabsf(span_x) >= fabsf(span_y)) {
        const float first = fminf(from_x, to_x) - reach;
        const float last = fmaxf(from_x, to_x) + reach;
        const float spread = reach * span_length / fabsf(span_x);
        for (int x = (int)floorf(first); x <= (int)ceilf(last); x++) {
            const float centre = from_y + span_y * ((float)x - from_x) / span_x;
            for (int y = (int)floorf(centre - spread); y <= (int)ceilf(centre + spread); y++) {
                const uint8_t coverage = capsule_coverage((float)x, (float)y, from_x, from_y,
                                                          span_x, span_y, span_length_squared,
                                                          half_width);
                canvas_blend(x, y, color, canvas_scale(coverage, alpha));
            }
        }
    } else {
        const float first = fminf(from_y, to_y) - reach;
        const float last = fmaxf(from_y, to_y) + reach;
        const float spread = reach * span_length / fabsf(span_y);
        for (int y = (int)floorf(first); y <= (int)ceilf(last); y++) {
            const float centre = from_x + span_x * ((float)y - from_y) / span_y;
            for (int x = (int)floorf(centre - spread); x <= (int)ceilf(centre + spread); x++) {
                const uint8_t coverage = capsule_coverage((float)x, (float)y, from_x, from_y,
                                                          span_x, span_y, span_length_squared,
                                                          half_width);
                canvas_blend(x, y, color, canvas_scale(coverage, alpha));
            }
        }
    }
}

void canvas_arc(float centre_x, float centre_y, float radius, float from_angle, float sweep,
                float width, uint16_t color, uint8_t alpha) {
    canvas_arc_gradient(centre_x, centre_y, radius, from_angle, sweep, width, color, alpha,
                        alpha);
}

void canvas_arc_gradient(float centre_x, float centre_y, float radius, float from_angle,
                         float sweep, float width, uint16_t color, uint8_t alpha_from,
                         uint8_t alpha_to) {
    const float half_width = width * 0.5f;
    const float reach = half_width + 0.5f;
    const float outer = radius + reach;
    const float inner = radius - reach;
    const float two_pi = 2.0f * (float)M_PI;
    const bool uniform_ring = sweep >= two_pi && alpha_from == alpha_to;
    const float span = sweep >= two_pi ? two_pi : sweep;

    if ((alpha_from == 0 && alpha_to == 0) || radius <= reach || span <= 0.0f) {
        return;
    }

    // Walk each row, visiting only the two chords the annulus cuts through it.
    for (int y = (int)floorf(centre_y - outer); y <= (int)ceilf(centre_y + outer); y++) {
        const float offset_y = (float)y - centre_y;
        const float outer_half = sqrtf(fmaxf(outer * outer - offset_y * offset_y, 0.0f));
        const float inner_half =
            (fabsf(offset_y) < inner) ? sqrtf(inner * inner - offset_y * offset_y) : 0.0f;

        // Two chords where the row crosses the hole, one where it runs along
        // the top or bottom of the ring.
        const int chord_count = inner_half > 0.0f ? 2 : 1;
        for (int chord = 0; chord < chord_count; chord++) {
            const float chord_from = chord == 0 ? centre_x - outer_half : centre_x + inner_half;
            const float chord_to =
                (chord_count == 1 || chord == 1) ? centre_x + outer_half : centre_x - inner_half;
            for (int x = (int)floorf(chord_from); x <= (int)ceilf(chord_to); x++) {
                const float offset_x = (float)x - centre_x;
                const float distance = sqrtf(offset_x * offset_x + offset_y * offset_y);
                const float coverage = half_width + 0.5f - fabsf(distance - radius);
                if (coverage <= 0.0f) {
                    continue;
                }

                float alpha = (float)alpha_from;
                if (!uniform_ring) {
                    float relative = atan2f(offset_y, offset_x) - from_angle;
                    while (relative < 0.0f) {
                        relative += two_pi;
                    }

                    if (relative > span) {
                        continue;
                    }

                    alpha += ((float)alpha_to - (float)alpha_from) * relative / span;
                }

                const float clamped = coverage >= 1.0f ? 1.0f : coverage;
                canvas_blend(x, y, color, (uint8_t)(clamped * alpha + 0.5f));
            }
        }
    }
}

void canvas_shift_rows(int top, int height, int delta_x, uint16_t fill) {
    const int first = top < 0 ? 0 : top;
    const int last = top + height > CANVAS_HEIGHT ? CANVAS_HEIGHT : top + height;
    const int magnitude = delta_x < 0 ? -delta_x : delta_x;

    if (delta_x == 0 || magnitude >= CANVAS_WIDTH) {
        return;
    }

    for (int row = first; row < last; row++) {
        uint16_t *pixel = &framebuffer[(size_t)row * CANVAS_WIDTH];
        const size_t moved = (size_t)(CANVAS_WIDTH - magnitude);
        if (delta_x > 0) {
            memmove(pixel + magnitude, pixel, moved * sizeof(uint16_t));
            for (int column = 0; column < magnitude; column++) {
                pixel[column] = fill;
            }
        } else {
            memmove(pixel, pixel + magnitude, moved * sizeof(uint16_t));
            for (int column = CANVAS_WIDTH - magnitude; column < CANVAS_WIDTH; column++) {
                pixel[column] = fill;
            }
        }
    }
}

void canvas_text(const font_t *font, const char *text, int left_x, int baseline_y,
                 uint16_t color, uint8_t alpha) {
    if (alpha == 0) {
        return;
    }

    int pen_x = left_x;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        const glyph_t *glyph = font_find_glyph(font, (uint8_t)*cursor);
        if (glyph == NULL) {
            continue;
        }

        const int origin_x = pen_x + glyph->left;
        const int origin_y = baseline_y - glyph->top;
        for (int row = 0; row < glyph->height; row++) {
            for (int column = 0; column < glyph->width; column++) {
                const uint8_t coverage = font_coverage_at(font, glyph, column, row);
                canvas_blend(origin_x + column, origin_y + row, color,
                             canvas_scale(coverage, alpha));
            }
        }

        pen_x += glyph->advance;
    }
}

// Bilinear coverage sample in glyph bitmap space, zero beyond the bitmap.
static float sample_coverage(const font_t *font, const glyph_t *glyph, float x, float y) {
    const float left = floorf(x);
    const float top = floorf(y);
    const float fraction_x = x - left;
    const float fraction_y = y - top;

    float total = 0.0f;
    for (int corner = 0; corner < 4; corner++) {
        const int sample_x = (int)left + (corner & 1);
        const int sample_y = (int)top + (corner >> 1);
        if (sample_x < 0 || sample_y < 0 || sample_x >= glyph->width || sample_y >= glyph->height) {
            continue;
        }

        const float weight_x = (corner & 1) ? fraction_x : (1.0f - fraction_x);
        const float weight_y = (corner >> 1) ? fraction_y : (1.0f - fraction_y);
        total += weight_x * weight_y * (float)font_coverage_at(font, glyph, sample_x, sample_y);
    }

    return total;
}

// Arc length the run occupies, including the gaps tracking opens between glyphs.
static float arc_text_span(const font_t *font, const char *text, float tracking) {
    float span = 0.0f;
    int glyphs = 0;

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        const glyph_t *glyph = font_find_glyph(font, (uint8_t)*cursor);
        if (glyph != NULL) {
            span += (float)glyph->advance;
            glyphs++;
        }
    }

    if (glyphs > 1) {
        span += tracking * (float)(glyphs - 1);
    }

    return span;
}


void canvas_text_scaled(const font_t *font, const char *text, float centre_x, float baseline_y,
                        float scale_x, float scale_y, uint16_t color, uint8_t alpha) {
    // Below this the run is thinner than a pixel and there is nothing to show.
    if (alpha == 0 || scale_x < 0.015f || scale_y < 0.015f) {
        return;
    }

    // Supersampling only earns its keep when the glyph is being minified, and
    // only along the axis doing the minifying. A face turned square on is the
    // largest destination and needs none at all, while a face turned away is a
    // few pixels across and can afford them, so the work stays roughly flat.
    const float squeeze = scale_x < scale_y ? scale_x : scale_y;
    const int subsamples = squeeze >= 0.7f ? 1 : (squeeze >= 0.35f ? 2 : 3);
    const bool along_x = scale_x <= scale_y;

    // The pen walks the unscaled run, measured from its centre, and only the
    // destination is scaled. That keeps the glyph spacing proportional.
    float pen = (float)font_text_width(font, text) * -0.5f;

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        const glyph_t *glyph = font_find_glyph(font, (uint8_t)*cursor);
        if (glyph == NULL) {
            continue;
        }

        const float glyph_left = pen + (float)glyph->left;
        const float glyph_top = -(float)glyph->top;

        const int first_x = (int)floorf(centre_x + glyph_left * scale_x) - 1;
        const int last_x = (int)ceilf(centre_x + (glyph_left + (float)glyph->width) * scale_x) + 1;
        const int first_y = (int)floorf(baseline_y + glyph_top * scale_y) - 1;
        const int last_y = (int)ceilf(baseline_y + (glyph_top + (float)glyph->height) * scale_y) + 1;

        for (int y = first_y; y <= last_y; y++) {
            for (int x = first_x; x <= last_x; x++) {
                float coverage = 0.0f;
                for (int sub = 0; sub < subsamples; sub++) {
                    const float shift = -0.5f + (0.5f + (float)sub) / (float)subsamples;
                    const float sample_x = along_x ? (float)x + shift : (float)x;
                    const float sample_y = along_x ? (float)y : (float)y + shift;

                    const float source_x = (sample_x - centre_x) / scale_x - glyph_left;
                    const float source_y = (sample_y - baseline_y) / scale_y - glyph_top;

                    coverage += sample_coverage(font, glyph, source_x, source_y);
                }

                coverage /= (float)subsamples;
                if (coverage > 0.5f) {
                    canvas_blend(x, y, color, canvas_scale((uint8_t)coverage, alpha));
                }
            }
        }

        pen += (float)glyph->advance;
    }
}

void canvas_text_arc(const font_t *font, const char *text, float origin_x, float origin_y,
                     float radius, float centre_angle, float tracking, uint16_t color,
                     uint8_t alpha) {
    if (alpha == 0 || radius <= 1.0f) {
        return;
    }

    // Advancing along the text walks the tangent, which turns the angle backwards.
    const float half_span = arc_text_span(font, text, tracking) * 0.5f / radius;
    float angle = centre_angle + half_span;

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        const glyph_t *glyph = font_find_glyph(font, (uint8_t)*cursor);
        if (glyph == NULL) {
            continue;
        }

        const float cosine = cosf(angle);
        const float sine = sinf(angle);
        const float pen_x = origin_x + radius * cosine;
        const float pen_y = origin_y + radius * sine;
        const float right_x = sine;
        const float right_y = -cosine;
        const float up_x = -cosine;
        const float up_y = -sine;

        // Bounding box of the rotated glyph rectangle in canvas space.
        const float corners_x[4] = {(float)glyph->left, (float)(glyph->left + glyph->width),
                                    (float)glyph->left, (float)(glyph->left + glyph->width)};
        const float corners_y[4] = {(float)-glyph->top, (float)-glyph->top,
                                    (float)(glyph->height - glyph->top),
                                    (float)(glyph->height - glyph->top)};
        float min_x = pen_x;
        float max_x = pen_x;
        float min_y = pen_y;
        float max_y = pen_y;
        for (int corner = 0; corner < 4; corner++) {
            const float x = pen_x + right_x * corners_x[corner] - up_x * corners_y[corner];
            const float y = pen_y + right_y * corners_x[corner] - up_y * corners_y[corner];
            min_x = fminf(min_x, x);
            max_x = fmaxf(max_x, x);
            min_y = fminf(min_y, y);
            max_y = fmaxf(max_y, y);
        }

        for (int y = (int)floorf(min_y) - 1; y <= (int)ceilf(max_y) + 1; y++) {
            for (int x = (int)floorf(min_x) - 1; x <= (int)ceilf(max_x) + 1; x++) {
                const float offset_x = (float)x - pen_x;
                const float offset_y = (float)y - pen_y;
                const float glyph_x = offset_x * right_x + offset_y * right_y;
                const float glyph_y = -(offset_x * up_x + offset_y * up_y);
                const float coverage = sample_coverage(font, glyph,
                                                       glyph_x - (float)glyph->left,
                                                       glyph_y + (float)glyph->top);
                if (coverage > 0.5f) {
                    canvas_blend(x, y, color, canvas_scale(coverage, alpha));
                }
            }
        }

        angle -= ((float)glyph->advance + tracking) / radius;
    }
}

