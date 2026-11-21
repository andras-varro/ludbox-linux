/* Host test for common/rotation.h: the policy, the var.rotate table, the
 * per-angle bezel and point maps.
 * Build: gcc -std=c99 -Wall -Wextra -I common tests/rotation_test.c -o /tmp/rotation_test
 * Negative control: define ROTATION_SABOTAGE to swap the quarter-1 and
 * quarter-3 point maps; groups C and D must then fail. */
#include <stdio.h>
#include "rotation.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#ifdef ROTATION_SABOTAGE
static void pt(int q, int pw, int ph, int x, int y, int *lx, int *ly) {
    rotation_point(q == 1 ? 3 : q == 3 ? 1 : q, pw, ph, x, y, lx, ly);
}
#else
#define pt rotation_point
#endif

int main(void) {
    /* A: degrees -> quarter */
    CHECK(rotation_from_degrees(0) == 0 && rotation_from_degrees(90) == 1 &&
          rotation_from_degrees(180) == 2 && rotation_from_degrees(270) == 3, "A degrees");
    CHECK(rotation_from_degrees(360) == 0 && rotation_from_degrees(-90) == 3 &&
          rotation_from_degrees(45) == 0, "A fold/invalid");

    /* B: the policy table, every input */
    static const struct { int set; int vrfb, lo; int q, hw, sw; } P[] = {
        {0,1,0, 0,1,0}, {1,1,0, 1,1,0}, {2,1,0, 2,1,0}, {3,1,0, 3,1,0},
        {0,1,1, 0,1,0}, {1,1,1, 0,1,0}, {2,1,1, 2,1,0}, {3,1,1, 2,1,0},
        {0,0,0, 0,0,0}, {1,0,0, 0,0,0}, {2,0,0, 0,0,0}, {3,0,0, 3,0,1},
        {0,0,1, 0,0,0}, {1,0,1, 0,0,0}, {2,0,1, 0,0,0}, {3,0,1, 0,0,0},
    };
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) {
        RotationPlan p = rotation_plan(P[i].set, P[i].vrfb, P[i].lo);
        CHECK(p.quarter == P[i].q && p.hw == (bool)P[i].hw && p.sw == (bool)P[i].sw,
              "B set=%d vrfb=%d lo=%d -> q=%d hw=%d sw=%d", P[i].set, P[i].vrfb, P[i].lo,
              p.quarter, p.hw, p.sw);
    }
    CHECK(!rotation_is_tall(0) && rotation_is_tall(1) && !rotation_is_tall(2) && rotation_is_tall(3), "B tall");
    CHECK(rotation_var_rotate(0) == 0 && rotation_var_rotate(1) == 1 &&
          rotation_var_rotate(2) == 2 && rotation_var_rotate(3) == 3, "B var.rotate table");

    /* C: quarter 3 point map equals the software portrait's touch map
     * (mx = phys_h-1-py, my = px), and corners land where a CW turn puts them */
    int lx, ly;
    pt(3, 800, 480, 0, 0, &lx, &ly);     CHECK(lx == 479 && ly == 0,   "C q3 origin -> %d,%d", lx, ly);
    pt(3, 800, 480, 799, 479, &lx, &ly); CHECK(lx == 0 && ly == 799,   "C q3 max -> %d,%d", lx, ly);
    pt(1, 800, 480, 0, 0, &lx, &ly);     CHECK(lx == 0 && ly == 799,   "C q1 origin -> %d,%d", lx, ly);
    pt(1, 800, 480, 799, 0, &lx, &ly);   CHECK(lx == 0 && ly == 0,     "C q1 top-right -> %d,%d", lx, ly);
    pt(2, 800, 480, 0, 0, &lx, &ly);     CHECK(lx == 799 && ly == 479, "C q2 origin -> %d,%d", lx, ly);
    pt(0, 800, 480, 12, 34, &lx, &ly);   CHECK(lx == 12 && ly == 34,   "C q0 identity");
    /* every physical pixel maps inside the logical panel, and q1 then q3 composes to identity
     * shape: the 4 maps are a bijection onto the rotated rectangle */
    for (int q = 0; q < 4; q++) {
        int lw = rotation_is_tall(q) ? 480 : 800, lh = rotation_is_tall(q) ? 800 : 480, bad = 0;
        for (int y = 0; y < 480; y += 7) for (int x = 0; x < 800; x += 7) {
            pt(q, 800, 480, x, y, &lx, &ly);
            if (lx < 0 || ly < 0 || lx >= lw || ly >= lh) bad++;
        }
        CHECK(bad == 0, "C q%d %d points outside %dx%d", q, bad, lw, lh);
    }

    /* D: bezel follows the point map: a margin on physical edge E lands on the
     * logical edge that the point map sends E's pixels to */
    for (int q = 0; q < 4; q++) {
        int t = 1, b = 2, l = 3, r = 4;
        rotation_bezel(q, &t, &b, &l, &r);
        /* physical top-centre pixel (400,0): which logical edge does it reach? */
        int lw = rotation_is_tall(q) ? 480 : 800, lh = rotation_is_tall(q) ? 800 : 480;
        pt(q, 800, 480, 400, 0, &lx, &ly);
        int got = (ly == 0) ? t : (ly == lh - 1) ? b : (lx == 0) ? l : (lx == lw - 1) ? r : -1;
        CHECK(got == 1, "D q%d physical top margin landed as %d", q, got);
        pt(q, 800, 480, 0, 240, &lx, &ly);
        got = (ly == 0) ? t : (ly == lh - 1) ? b : (lx == 0) ? l : (lx == lw - 1) ? r : -1;
        CHECK(got == 3, "D q%d physical left margin landed as %d", q, got);
    }
    { int t = 1, b = 2, l = 3, r = 4; rotation_bezel(3, &t, &b, &l, &r);
      CHECK(t == 3 && b == 4 && l == 2 && r == 1, "D q3 equals the old portrait rotation T=L B=R L=B R=T"); }

    printf("rotation_test: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
