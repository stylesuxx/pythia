#include "coin.h"

#include <math.h>
#include <stddef.h>

#include "canvas.h"
#include "theme.h"

#define COIN_RADIUS 92.0f
#define COIN_HALF_THICKNESS 7.0f
#define COIN_CENTRE_Y 176.0f

// The coin tumbles about a horizontal axis, the way a tossed coin does, so the
// milled edge sweeps below the face and back rather than across it.
//
// It comes to rest part way through a turn rather than square on; see
// COIN_REST_TILT in the header for why.

// Brisk: the flip is a punctuation mark, not a performance.
#define FLIP_MS 760
#define FLIP_MIN_TURNS 3

// A short tail after a flip so the settled frame is always drawn.
#define SETTLE_MS 80

// Below this the face is edge on and its content is thinner than a pixel.
#define FACE_VISIBLE 0.04f

// How far the lit edge of the engraving sits from the groove, in pixels.
#define ENGRAVE_DEPTH 2.0f

// Milling around the rim. Ridges sit at fixed heights and only their
// visibility changes as the coin turns, which is what a real coin does.
#define RIDGE_COUNT 34
#define RIDGE_THICKNESS 1.7f

// Vertical supersampling for the silhouette, which has no closed-form
// coverage the way a plain ellipse does.
#define SUBSAMPLES 3

#define TWO_PI 6.2831853f

static uint32_t flip_started_ms = 0;
static float flip_from = 0.0f;
static float flip_to = 0.0f;

static float ease_out_quart(float t) {
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse * inverse;
}

static float angle_at(uint32_t now) {
    const uint32_t elapsed = now - flip_started_ms;
    if (elapsed >= FLIP_MS) {
        return flip_to;
    }

    return flip_from + (flip_to - flip_from) * ease_out_quart((float)elapsed / (float)FLIP_MS);
}

// Scales an RGB565 colour, for the shading that separates rim from face.
static uint16_t shade(uint16_t color, float factor) {
    uint16_t red = (uint16_t)((float)((color >> 11) & 0x1Fu) * factor);
    uint16_t green = (uint16_t)((float)((color >> 5) & 0x3Fu) * factor);
    uint16_t blue = (uint16_t)((float)(color & 0x1Fu) * factor);

    if (red > 0x1Fu) {
        red = 0x1Fu;
    }

    if (green > 0x3Fu) {
        green = 0x3Fu;
    }

    if (blue > 0x1Fu) {
        blue = 0x1Fu;
    }

    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static float overlap(int x, float left, float right) {
    const float lower = (float)x - 0.5f > left ? (float)x - 0.5f : left;
    const float upper = (float)x + 0.5f < right ? (float)x + 0.5f : right;
    const float width = upper - lower;
    return width > 0.0f ? width : 0.0f;
}

// Draws the coin body in one pass.
//
// At a given row the cylinder covers [centre - |offset| - w, centre + |offset| + w], and the
// face covers [centre + offset - w, centre + offset + w]. Those differ by exactly one interval
// of width 2|offset|, on the side the face is not leaning towards: that interval is the rim.
//
// Filling the silhouette and then painting the face over it would cover the same pixels twice,
// and the face is most of them. Filling the two intervals in a single pass halves the work,
// which matters because every one of these pixels is a read-modify-write into PSRAM.
// Draws the coin body in one pass.
//
// The coin is a cylinder seen from above and to one side. Its two rims are
// ellipses of the same size, offset from each other by the projected
// thickness: horizontally by the turn, vertically by the fixed tilt. The
// silhouette is everything the cylinder sweeps between them, and the face is
// the near rim. Whatever the silhouette covers and the face does not is the
// metal edge, which is why the rim needs no geometry of its own.
//
// Coverage is taken for both shapes per pixel and the rim gets the difference.
// That costs the same as the older side-only version but no longer assumes the
// rim is a single interval, which stops being true once the coin is tilted.
static void draw_body(float centre_x, float offset_x, float offset_y, float half_width,
                      float half_height, uint16_t rim_color, uint16_t face_color,
                      uint8_t alpha) {
    if (alpha == 0) {
        return;
    }

    // The sweep reaches |offset_y| either side of centre whichever way the coin
    // is leaning. Using the signed value here shrinks the range once the coin
    // turns past square on, and slices the far rim off flat.
    const float reach = fabsf(offset_y);
    const int first = (int)floorf(COIN_CENTRE_Y - half_height - reach) - 2;
    const int last = (int)ceilf(COIN_CENTRE_Y + half_height + reach) + 2;

    for (int y = first; y <= last; y++) {
        float face_left[SUBSAMPLES];
        float face_right[SUBSAMPLES];
        float body_left[SUBSAMPLES];
        float body_right[SUBSAMPLES];
        int rows = 0;

        for (int sub = 0; sub < SUBSAMPLES; sub++) {
            const float sample_y = (float)y - 0.5f + (0.5f + (float)sub) / (float)SUBSAMPLES;

            // Three stations along the sweep: the far rim, the middle of the
            // metal and the near rim. The sweep is only a few pixels long, so
            // this traces its outline closely enough.
            float left = 0.0f;
            float right = 0.0f;
            bool any = false;

            for (int step = -1; step <= 1; step++) {
                const float station_y = COIN_CENTRE_Y - (float)step * offset_y;
                const float t = (sample_y - station_y) / half_height;
                if (fabsf(t) >= 1.0f) {
                    continue;
                }

                const float w = half_width * sqrtf(1.0f - t * t);
                const float centre = centre_x + (float)step * offset_x;
                if (!any || centre - w < left) {
                    left = centre - w;
                }

                if (!any || centre + w > right) {
                    right = centre + w;
                }

                any = true;
            }

            if (!any) {
                continue;
            }

            body_left[rows] = left;
            body_right[rows] = right;

            // The near rim: the face itself.
            const float face_t = (sample_y - (COIN_CENTRE_Y - offset_y)) / half_height;
            if (fabsf(face_t) < 1.0f) {
                const float fw = half_width * sqrtf(1.0f - face_t * face_t);
                face_left[rows] = centre_x + offset_x - fw;
                face_right[rows] = centre_x + offset_x + fw;
            } else {
                face_left[rows] = 0.0f;
                face_right[rows] = 0.0f;
            }

            rows++;
        }

        if (rows == 0) {
            continue;
        }

        float outer_left = body_left[0];
        float outer_right = body_right[0];
        float face_inner_left = face_left[0];
        float face_inner_right = face_right[0];

        for (int sub = 1; sub < rows; sub++) {
            if (body_left[sub] < outer_left) {
                outer_left = body_left[sub];
            }

            if (body_right[sub] > outer_right) {
                outer_right = body_right[sub];
            }

            if (face_left[sub] > face_inner_left) {
                face_inner_left = face_left[sub];
            }

            if (face_right[sub] < face_inner_right) {
                face_inner_right = face_right[sub];
            }
        }

        // Where every sample agreed the face is solid there is nothing to
        // compute: that is almost every pixel of the coin.
        const bool solid = (rows == SUBSAMPLES);
        const int face_solid_from = (int)ceilf(face_inner_left + 0.5f);
        const int face_solid_to = (int)floorf(face_inner_right - 0.5f);

        const int from = (int)floorf(outer_left) - 1;
        const int to = (int)ceilf(outer_right) + 1;

        for (int x = from; x <= to; x++) {
            if (solid && x >= face_solid_from && x <= face_solid_to) {
                canvas_blend(x, y, face_color, alpha);
                continue;
            }

            float body = 0.0f;
            float face = 0.0f;
            for (int sub = 0; sub < rows; sub++) {
                body += overlap(x, body_left[sub], body_right[sub]);
                face += overlap(x, face_left[sub], face_right[sub]);
            }

            const float rim = body - face;
            if (rim > 0.002f) {
                const float share = rim / (float)SUBSAMPLES;
                const uint8_t shade_alpha =
                    share >= 1.0f ? 255 : (uint8_t)(share * 255.0f + 0.5f);
                canvas_blend(x, y, rim_color, canvas_scale(shade_alpha, alpha));
            }

            if (face > 0.002f) {
                const float share = face / (float)SUBSAMPLES;
                const uint8_t shade_alpha =
                    share >= 1.0f ? 255 : (uint8_t)(share * 255.0f + 0.5f);
                canvas_blend(x, y, face_color, canvas_scale(shade_alpha, alpha));
            }
        }
    }
}

void coin_flip(uint8_t landing, uint32_t now) {
    flip_from = angle_at(now);

    // A face is square on at a whole number of turns, the other half a turn
    // later. Take the first such angle at least FLIP_MIN_TURNS away.
    const float rest = COIN_REST_TILT + ((landing == 1) ? 0.0f : (float)M_PI);
    const float earliest = flip_from + (float)FLIP_MIN_TURNS * TWO_PI;
    flip_to = rest + ceilf((earliest - rest) / TWO_PI) * TWO_PI;

    flip_started_ms = now;
}

bool coin_is_flipping(uint32_t now) {
    return (now - flip_started_ms) < FLIP_MS;
}

bool coin_is_animating(uint32_t now) {
    return (now - flip_started_ms) < (uint32_t)(FLIP_MS + SETTLE_MS);
}

float coin_facing(uint32_t now) {
    return cosf(angle_at(now));
}

static void draw_ridges(float centre_x, float offset_y, float half_width, float half_height,
                        uint16_t color, uint8_t alpha) {
    for (int index = 0; index < RIDGE_COUNT; index++) {
        const float around = TWO_PI * (float)index / (float)RIDGE_COUNT;

        // Only the rim on the near side of the tumble is showing, which is the
        // side the face is not displaced towards.
        if (sinf(around) * offset_y <= 0.0f) {
            continue;
        }

        // Each ridge runs across the thickness, from the near rim to the far
        // one, so with a horizontal tumble it is a short vertical stroke.
        const float x = centre_x + half_width * cosf(around);
        const float near_y = (COIN_CENTRE_Y - offset_y) + half_height * sinf(around);
        const float far_y = (COIN_CENTRE_Y + offset_y) + half_height * sinf(around);

        // Both endpoints sit exactly on the silhouette, and the stroke has
        // round caps, so drawn as-is every ridge pokes half its width past the
        // edge and the rim reads as a row of spikes. Pull the ends inside.
        const float inset = RIDGE_THICKNESS * 0.5f + 0.6f;
        const float span = far_y - near_y;
        if (fabsf(span) <= 2.0f * inset) {
            continue;
        }
        const float step = span > 0.0f ? inset : -inset;

        canvas_line(x, near_y + step, x, far_y - step, RIDGE_THICKNESS, color, alpha);
    }
}

void coin_draw(uint32_t now, uint8_t alpha) {
    if (alpha == 0) {
        return;
    }

    const theme_t *theme = theme_active();
    const float turn = angle_at(now);
    const float squash = cosf(turn);

    // Tumbling about a horizontal axis leaves the width alone and foreshortens
    // the height, and the thickness projects up or down rather than sideways.
    const float half_width = COIN_RADIUS;
    const float half_height = COIN_RADIUS * fabsf(squash);
    const float offset_y = COIN_HALF_THICKNESS * sinf(turn) * (squash >= 0.0f ? 1.0f : -1.0f);

    const uint16_t face_color = theme->answer;
    const uint16_t rim_color = shade(face_color, 0.55f);
    const uint16_t groove_color = shade(face_color, 0.3f);
    const float centre_x = CANVAS_WIDTH / 2.0f;

    draw_body(centre_x, 0.0f, offset_y, half_width, half_height, rim_color, face_color, alpha);
    if (fabsf(offset_y) > 1.0f) {
        draw_ridges(centre_x, offset_y, half_width, half_height, groove_color, alpha);
    }

    if (fabsf(squash) < FACE_VISIBLE) {
        return;
    }

    const char text[2] = {squash >= 0.0f ? '1' : '2', '\0'};
    const glyph_t *glyph = font_find_glyph(theme->number_font, (uint8_t)text[0]);
    if (glyph == NULL) {
        return;
    }

    // Struck into the metal, not punched through it. The numeral stays the
    // coin's own colour, only darker, and a lighter edge sits just below and
    // right of it: the far wall of a groove catching the light. Painting the
    // groove over the highlight leaves exactly that fringe showing.
    const uint16_t groove = shade(face_color, 0.42f);
    const uint16_t lit = shade(face_color, 1.35f);

    // The numeral rides on the face, so it foreshortens with it and sits on the
    // face's centre rather than the coin's.
    const float face_centre_y = COIN_CENTRE_Y - offset_y;
    const float baseline = face_centre_y + (float)(glyph->top / 2) * fabsf(squash);

    if (fabsf(squash) > 0.55f) {
        canvas_text_scaled(theme->number_font, text, centre_x + ENGRAVE_DEPTH,
                           baseline + ENGRAVE_DEPTH, 1.0f, fabsf(squash), lit, alpha);
    }

    canvas_text_scaled(theme->number_font, text, centre_x, baseline, 1.0f, fabsf(squash),
                       groove, alpha);
}

frame_rect_t coin_stage(void) {
    // Tumbling leaves the width fixed at the full diameter, and the height is
    // widest square on, plus half the thickness for the rim and a margin for
    // the anti-aliased edge.
    const float half_tall = COIN_RADIUS + COIN_HALF_THICKNESS + 3.0f;
    const float half_wide = COIN_RADIUS + 3.0f;

    const int top = ((int)(COIN_CENTRE_Y - half_tall)) & ~1;
    const int bottom = (int)(COIN_CENTRE_Y + half_tall) + 1;
    const int left = ((int)(CANVAS_WIDTH / 2.0f - half_wide)) & ~1;
    const int right = (int)(CANVAS_WIDTH / 2.0f + half_wide) + 1;

    const frame_rect_t rect = {top, (bottom - top + 1) & ~1, left, (right - left + 1) & ~1};
    return rect;
}
