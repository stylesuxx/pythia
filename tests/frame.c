// The frame module's promises: marks accumulate into one run of rows, a frame
// fills exactly those rows before drawing, presents exactly those rows and
// forgets them, rows beyond the canvas are dropped, and frames are paced.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "canvas.h"
#include "frame.h"

#define BACKGROUND 0x1234
#define UNTOUCHED 0xFFFF

static int failures = 0;

#define EXPECT(condition, ...)                                                                    \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            fputs("FAIL: ", stderr);                                                              \
            fprintf(stderr, __VA_ARGS__);                                                         \
            fputc('\n', stderr);                                                                  \
            failures++;                                                                           \
        }                                                                                         \
    } while (0)

static int draws = 0;
static frame_rows_t drawn_rows = {0, 0};

static void record_draw(void *context, frame_rows_t rows) {
    (void)context;
    draws++;
    drawn_rows = rows;
}

static frame_rows_t render(uint32_t now) {
    return frame_render(now, BACKGROUND, record_draw, NULL);
}

static bool same(frame_rows_t a, int top, int height) {
    return a.top == top && a.height == height;
}

// Rows [top, top + height) hold the background and every other row is
// untouched.
static bool filled_exactly(int top, int height) {
    const uint16_t *pixels = canvas_pixels();
    for (int row = 0; row < CANVAS_HEIGHT; row++) {
        const bool inside = row >= top && row < top + height;
        const uint16_t expected = inside ? BACKGROUND : UNTOUCHED;
        for (int column = 0; column < CANVAS_WIDTH; column++) {
            if (pixels[row * CANVAS_WIDTH + column] != expected) {
                return false;
            }
        }
    }

    return true;
}

static void reset(uint32_t now) {
    canvas_fill(UNTOUCHED);
    frame_begin(now);

    draws = 0;
}

static void check_nothing_marked_draws_nothing(void) {
    reset(0);
    const frame_rows_t rows = render(0);

    EXPECT(rows.height == 0, "an unmarked frame presented %d rows", rows.height);
    EXPECT(draws == 0, "an unmarked frame called draw");
    EXPECT(filled_exactly(0, 0), "an unmarked frame touched the canvas");
}

static void check_marked_rows_are_filled_drawn_and_forgotten(void) {
    reset(0);
    frame_mark((frame_rows_t){100, 20});
    const frame_rows_t rows = render(0);

    EXPECT(same(rows, 100, 20), "marked 100+20, presented %d+%d", rows.top, rows.height);
    EXPECT(draws == 1 && same(drawn_rows, 100, 20), "draw saw %d+%d", drawn_rows.top,
           drawn_rows.height);
    EXPECT(filled_exactly(100, 20), "the fill did not cover exactly the marked rows");

    const frame_rows_t again = render(100);
    EXPECT(again.height == 0 && draws == 1, "the marks survived the frame that drew them");
}

static void check_marks_union(void) {
    reset(0);
    frame_mark((frame_rows_t){200, 5});
    frame_mark((frame_rows_t){50, 10});
    const frame_rows_t rows = render(0);

    EXPECT(same(rows, 50, 155), "50+10 and 200+5 presented as %d+%d", rows.top, rows.height);

    frame_mark((frame_rows_t){10, 10});
    frame_mark_whole();
    const frame_rows_t whole = render(100);

    EXPECT(same(whole, 0, CANVAS_HEIGHT), "whole presented as %d+%d", whole.top, whole.height);
}

static void check_rows_are_clamped(void) {
    reset(0);
    frame_mark((frame_rows_t){-10, 30});
    frame_rows_t rows = render(0);

    EXPECT(same(rows, 0, 20), "-10+30 presented as %d+%d", rows.top, rows.height);

    frame_mark((frame_rows_t){350, 30});
    rows = render(100);

    EXPECT(same(rows, 350, 10), "350+30 presented as %d+%d", rows.top, rows.height);

    frame_mark((frame_rows_t){10, 0});
    frame_mark((frame_rows_t){400, 5});
    frame_mark((frame_rows_t){20, -4});
    rows = render(200);

    EXPECT(rows.height == 0, "empty and off-canvas marks presented %d+%d", rows.top, rows.height);
}

static void check_frames_are_paced(void) {
    reset(1000);
    frame_mark((frame_rows_t){0, 10});

    EXPECT(render(1000).height == 10, "the first frame after begin waited");

    frame_mark((frame_rows_t){100, 10});

    EXPECT(render(1002).height == 0, "a frame ran 2 ms after the last");
    EXPECT(render(1010).height == 0, "a frame ran 10 ms after the last");
    EXPECT(draws == 1, "pacing called draw %d times", draws);

    frame_mark((frame_rows_t){200, 10});
    const frame_rows_t rows = render(1016);

    EXPECT(same(rows, 100, 110), "the marks kept while paced presented as %d+%d", rows.top,
           rows.height);
    EXPECT(draws == 2, "the paced frame called draw %d times in total", draws);
}

int main(void) {
    if (!canvas_begin()) {
        fputs("frame: no framebuffer\n", stderr);
        return 1;
    }

    check_nothing_marked_draws_nothing();
    check_marked_rows_are_filled_drawn_and_forgotten();
    check_marks_union();
    check_rows_are_clamped();
    check_frames_are_paced();

    if (failures > 0) {
        fprintf(stderr, "frame: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("frame: ok");
    return 0;
}
