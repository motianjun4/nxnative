// Touchscreen unit test: dm_touch_set -> touch_fill, and the TouchScreenState<16> written out (0x290 bytes) matches the hand-computed expectation byte for byte.
// Expected values are hand-written by layout (sample number / touch count / per point delta_time attribute finger_id x y diameter rotation angle), not computed by calling the implementation under test.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef struct { uint32_t id; float x, y; } dm_touch;
void dm_touch_set(const dm_touch *t, int n);
int touch_fill(void *out, uint64_t now_ns);
int touch_fill_n(void *out, int max, uint64_t now_ns);
void dm_host_frame_tick(void);   // a present: the reads in this test are separated into frames with it
static int fails, cases;
typedef struct { int64_t dt; uint32_t attr; int32_t fid, x, y; } want_pt;
static void expect(const char *what, uint64_t now, int64_t samp, int n, const want_pt *w) {
    uint8_t got[0x290], exp[0x290]; memset(exp, 0, sizeof exp);
    dm_host_frame_tick();   // a present since the last read: this read is a new frame's first
    int r = touch_fill(got, now);
    int32_t cnt = n; memcpy(exp, &samp, 8); memcpy(exp + 8, &cnt, 4);
    for (int i = 0; i < n; i++) {
        uint8_t *s = exp + 0x10 + 0x28 * i; int32_t d = 30;
        memcpy(s, &w[i].dt, 8); memcpy(s + 8, &w[i].attr, 4); memcpy(s + 0xc, &w[i].fid, 4);
        memcpy(s + 0x10, &w[i].x, 4); memcpy(s + 0x14, &w[i].y, 4); memcpy(s + 0x18, &d, 4); memcpy(s + 0x1c, &d, 4);
    }
    cases++;
    int ok = r == n && !memcmp(got, exp, sizeof exp);
    if (!ok) { fails++; int k = 0; while (k < 0x290 && got[k] == exp[k]) k++; printf("✗ %s: returned %d (expected %d), first differing byte +%#x (got %02x expected %02x)\n", what, r, n, k, k < 0x290 ? got[k] : 0, k < 0x290 ? exp[k] : 0); }
    else printf("✓ %s\n", what);
}
int main(void) {
    const uint64_t T0 = 1000000000ull;
    // 1 Down: id 2 at (0.5, 0.25) -> Start, x = 0.5*1280 = 640, y = 0.25*720 = 180, delta_time = 0 on the first read
    dm_touch a = {2, 0.5f, 0.25f}; dm_touch_set(&a, 1);
    expect("press reports Start (640, 180)", T0, 1, 1, (want_pt[]){{0, 1, 2, 640, 180}});
    // 2 Move to (0.75, 0.5) -> attribute 0, x = 960, y = 360, delta_time = 16,666,667 ns
    a.x = 0.75f; a.y = 0.5f; dm_touch_set(&a, 1);
    expect("move attribute 0 (960, 360), delta_time is the interval between two reads", T0 + 16666667, 2, 1, (want_pt[]){{16666667, 0, 2, 960, 360}});
    // 3 Lifted -> End reported at the last position, and the next read has 0 touch points
    dm_touch_set(NULL, 0);
    expect("lift reports End (960, 360)", T0 + 33333334, 3, 1, (want_pt[]){{16666667, 2, 2, 960, 360}});
    expect("0 touch points after End", T0 + 50000001, 4, 0, NULL);
    // 4 Down and up between two reads (id 0 at (0.1, 0.1)) -> Start (128, 72) this time, End next time, empty the time after
    dm_touch b = {0, 0.1f, 0.1f}; dm_touch_set(&b, 1); dm_touch_set(NULL, 0);
    expect("short-press latch: Start first (128, 72)", T0 + 66666668, 5, 1, (want_pt[]){{16666667, 1, 0, 128, 72}});
    expect("short-press latch: then End", T0 + 83333335, 6, 1, (want_pt[]){{16666667, 2, 0, 128, 72}});
    expect("empty after the short-press latch", T0 + 100000002, 7, 0, NULL);
    // 5 Clamping: (0.9999, 1.5) -> x = min(1279, ⌊0.9999*1280⌋ = 1279), y = min(719, 1080) = 719; (-0.2, -1) -> 0, 0
    dm_touch c[2] = {{3, 0.9999f, 1.5f}, {4, -0.2f, -1.0f}}; dm_touch_set(c, 2);
    expect("coordinates clamped to [0,1279]×[0,719]", T0 + 116666669, 8, 2, (want_pt[]){{16666667, 1, 3, 1279, 719}, {16666667, 1, 4, 0, 0}});
    dm_touch_set(NULL, 0); dm_host_frame_tick(); touch_fill((uint8_t[0x290]){0}, T0 + 133333336);   // sample 9: two End
    expect("empty after both Ends", T0 + 150000003, 10, 0, NULL);
    // 6 Two fingers id 5 and 1 (pushed with 5 first) -> ascending by id: 1 comes first
    dm_touch d[2] = {{5, 0.25f, 0.5f}, {1, 0.5f, 0.75f}}; dm_touch_set(d, 2);
    expect("multiple fingers in ascending id order (1:(640,540) 5:(320,360))", T0 + 166666670, 11, 2, (want_pt[]){{16666667, 1, 1, 640, 540}, {16666667, 1, 5, 320, 360}});
    dm_touch_set(NULL, 0); dm_host_frame_tick(); touch_fill((uint8_t[0x290]){0}, T0 + 183333337);   // sample 12: two End
    dm_host_frame_tick(); touch_fill((uint8_t[0x290]){0}, T0 + 200000004);                          // sample 13: empty
    // 7 Same id pressed again before End is reported: id 3 down (Start) -> lifted -> immediately down again at (0.5, 0.5) -> this read reports End (old position), the next reports a new Start
    dm_touch e = {3, 0.25f, 0.25f}; dm_touch_set(&e, 1);
    expect("before the re-press: Start (320, 180)", T0 + 216666671, 14, 1, (want_pt[]){{16666667, 1, 3, 320, 180}});
    dm_touch_set(NULL, 0); e.x = 0.5f; e.y = 0.5f; dm_touch_set(&e, 1);
    expect("re-press: the old End first (320, 180)", T0 + 233333338, 15, 1, (want_pt[]){{16666667, 2, 3, 320, 180}});
    expect("re-press: then the new Start (640, 360)", T0 + 250000005, 16, 1, (want_pt[]){{16666667, 1, 3, 640, 360}});
    // 8 Frame-coherent snapshots: repeat reads in one frame serve the frame's snapshot byte for byte, the latch and the
    //    sampling number advance once (at the frame's first read), and a narrow reader is served from the same snapshot
    dm_touch_set(NULL, 0);
    expect("frame cache: the finger held from (7) Ends", T0 + 266666672, 17, 1, (want_pt[]){{16666667, 2, 3, 640, 360}});
    expect("frame cache: empty the frame after", T0 + 283333339, 18, 0, NULL);
    dm_touch g = {7, 0.25f, 0.75f}; dm_touch_set(&g, 1);
    uint8_t fa[0x290], fb[0x290];
    dm_host_frame_tick();
    int ra = touch_fill(fa, T0 + 300000006);
    int rb = touch_fill(fb, T0 + 300000106);   // no present between the reads: the same frame
    int64_t fas; int32_t faa, fax, fay; memcpy(&fas, fa, 8); memcpy(&faa, fa + 0x10 + 8, 4); memcpy(&fax, fa + 0x10 + 0x10, 4); memcpy(&fay, fa + 0x10 + 0x14, 4);
    cases++;
    if (ra == 1 && rb == 1 && !memcmp(fa, fb, sizeof fa) && fas == 19 && faa == 1 && fax == 320 && fay == 540) printf("✓ same frame: two reads byte-identical (sample 19, Start (320, 540))\n");
    else { fails++; printf("✗ same frame: reads returned %d and %d, sample %lld, attribute %d at (%d, %d)\n", ra, rb, (long long)fas, faa, fax, fay); }
    // A finger that lands mid-frame joins at the next frame; a max=1 read in this frame takes the snapshot's first point
    dm_touch h[2] = {{7, 0.25f, 0.75f}, {8, 0.75f, 0.25f}}; dm_touch_set(h, 2);
    uint8_t fn[0x290]; int rn = touch_fill_n(fn, 1, T0 + 300000206);
    cases++;
    if (rn == 1 && !memcmp(fn, fa, 0x10 + 0x28)) printf("✓ same frame: a max=1 read serves the snapshot's first point\n");
    else { fails++; printf("✗ same frame: a max=1 read returned %d or differs from the snapshot\n", rn); }
    // The next frame's snapshot is filled full width: the wide reader sees both fingers, and the second finger's Start
    // was not used up by the narrow reads before it (the first finger's Start was spent once, in sample 19's snapshot)
    expect("next frame: both fingers (7 held (320, 540), 8 Start (960, 180))", T0 + 316666673, 20, 2, (want_pt[]){{16666667, 0, 7, 320, 540}, {16666667, 1, 8, 960, 180}});
    // The End comes at the next frame's first read, and a repeat read in that frame serves it again instead of a new sample
    dm_touch_set(NULL, 0);
    expect("next frame: both End", T0 + 333333340, 21, 2, (want_pt[]){{16666667, 2, 7, 320, 540}, {16666667, 2, 8, 960, 180}});
    uint8_t fe[0x290]; int re = touch_fill(fe, T0 + 333333440);
    int64_t fes; memcpy(&fes, fe, 8);
    cases++;
    if (re == 2 && fes == 21) printf("✓ same frame: the Ends are served again from the snapshot (still sample 21)\n");
    else { fails++; printf("✗ same frame: the repeat End read returned %d, sample %lld\n", re, (long long)fes); }
    expect("empty the frame after the Ends", T0 + 350000007, 22, 0, NULL);
    // A press and lift between frames, then a re-press before the End is read: Start, the old End, the new Start, as in (7)
    dm_touch p = {7, 0.5f, 0.5f}; dm_touch_set(&p, 1); dm_touch_set(NULL, 0);
    expect("frame cache re-press: Start first (640, 360)", T0 + 366666674, 23, 1, (want_pt[]){{16666667, 1, 7, 640, 360}});
    p.x = 0.25f; p.y = 0.25f; dm_touch_set(&p, 1);
    expect("frame cache re-press: the old End first (640, 360)", T0 + 383333341, 24, 1, (want_pt[]){{16666667, 2, 7, 640, 360}});
    expect("frame cache re-press: then the new Start (320, 180)", T0 + 400000008, 25, 1, (want_pt[]){{16666667, 1, 7, 320, 180}});
    printf("== %d checks, %d failed ==\n", cases, fails);
    return fails != 0;
}
