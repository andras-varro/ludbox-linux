/* Host regression for the physical-keyboard path of common/keyboard.c
 * (kb_typed_char / kb_apply_key).  keyboard.c is #included so the static
 * functions are reachable; run-all.sh lists the keyboard_symbols_test row.
 *
 * Asserts, per layout: what a typed key becomes (case folding, the allowed
 * set), that Shift picks symbols on FULL, that the field stops at max_len,
 * that Backspace autorepeat deletes while a first press does nothing here (the
 * BACK edge owns it), and that keys with no character add nothing.
 * Fails to compile against the pre-change source (no kb_apply_key).
 * Cannot see: the blocking loop (Enter as OK, focus hiding), nor the WASD
 * suppression, which gamepad_text_test covers on the gamepad side. */
#include "../common/keyboard.c"

static int fails, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char T(KeyboardLayout l, int code, bool shift, bool caps) {
    GamepadKeyEv k = { code, shift, caps, false };
    return kb_typed_char(l, &k);
}

int main(void) {
    CHECK(T(KB_LAYOUT_FULL, KEY_W, false, false) == 'w');
    CHECK(T(KB_LAYOUT_FULL, KEY_W, true, false) == 'W');
    CHECK(T(KB_LAYOUT_FULL, KEY_W, false, true) == 'W');
    CHECK(T(KB_LAYOUT_FULL, KEY_2, true, false) == '@');
    CHECK(T(KB_LAYOUT_FULL, KEY_SPACE, false, false) == ' ');
    CHECK(T(KB_LAYOUT_FULL, KEY_SEMICOLON, true, false) == ':');
    CHECK(T(KB_LAYOUT_FULL, KEY_LEFTSHIFT, false, false) == 0);
    CHECK(T(KB_LAYOUT_FULL, KEY_F5, false, false) == 0);
    CHECK(T(KB_LAYOUT_ALPHA, KEY_Q, false, false) == 'Q');
    CHECK(T(KB_LAYOUT_ALPHA, KEY_SPACE, false, false) == ' ');
    CHECK(T(KB_LAYOUT_ALPHA, KEY_1, false, false) == 0);
    CHECK(T(KB_LAYOUT_ALPHANUM, KEY_1, false, false) == '1');
    CHECK(T(KB_LAYOUT_ALPHANUM, KEY_A, false, false) == 'A');
    CHECK(T(KB_LAYOUT_ALPHANUM, KEY_MINUS, false, false) == '-');
    CHECK(T(KB_LAYOUT_ALPHANUM, KEY_MINUS, true, false) == '_');
    CHECK(T(KB_LAYOUT_ALPHANUM, KEY_2, true, false) == 0);
    CHECK(T(KB_LAYOUT_NUMERIC, KEY_5, false, false) == '5');
    CHECK(T(KB_LAYOUT_NUMERIC, KEY_KP7, false, false) == '7');
    CHECK(T(KB_LAYOUT_NUMERIC, KEY_DOT, false, false) == '.');
    CHECK(T(KB_LAYOUT_NUMERIC, KEY_SEMICOLON, true, false) == ':');
    CHECK(T(KB_LAYOUT_NUMERIC, KEY_A, false, false) == 0);

    char work[5] = ""; int cur = 0;
    GamepadKeyEv a = { KEY_A, false, false, false };
    for (int i = 0; i < 6; i++) kb_apply_key(KB_LAYOUT_FULL, work, &cur, 4, &a);
    CHECK(cur == 4 && strcmp(work, "aaaa") == 0);            /* max_len holds */
    CHECK(!kb_apply_key(KB_LAYOUT_FULL, work, &cur, 4, &a));
    GamepadKeyEv bs = { KEY_BACKSPACE, false, false, false };
    CHECK(!kb_apply_key(KB_LAYOUT_FULL, work, &cur, 4, &bs)); /* press: BACK edge */
    CHECK(cur == 4);
    bs.repeat = true;
    CHECK(kb_apply_key(KB_LAYOUT_FULL, work, &cur, 4, &bs));
    CHECK(cur == 3 && strcmp(work, "aaa") == 0);
    cur = 0; work[0] = '\0';
    CHECK(!kb_apply_key(KB_LAYOUT_FULL, work, &cur, 4, &bs)); /* empty: no underflow */

    /* The mouse: motion shows the arrow, a left click becomes this frame's
     * touch at it, a real finger wins, and a keyboard-less frame (in == NULL)
     * leaves the touch alone. */
    {
        Pointer p; pointer_init(&p);
        TouchState ts; memset(&ts, 0, sizeof(ts));
        InputState in; memset(&in, 0, sizeof(in));
        in.mouse_dx = 40; in.mouse_dy = 30;
        kb_mouse_frame(&p, &in, false, &ts);
        CHECK(p.m.shown && !ts.pressed);
        int px = p.m.x, py = p.m.y;
        in.mouse_dx = in.mouse_dy = 0;
        in.mouse_left_pressed = 1; in.mouse_left_held = 1;
        kb_mouse_frame(&p, &in, false, &ts);
        CHECK(ts.pressed && ts.x == px && ts.y == py);
        in.mouse_left_pressed = 0; in.mouse_left_held = 0; in.mouse_left_released = 1;
        memset(&ts, 0, sizeof(ts));
        kb_mouse_frame(&p, &in, false, &ts);
        CHECK(ts.released && ts.x == px);
        memset(&ts, 0, sizeof(ts));
        kb_mouse_frame(&p, NULL, false, &ts);
        CHECK(!ts.pressed && !ts.released && !ts.held);
        in.mouse_left_released = 0; in.mouse_left_pressed = 1; in.mouse_left_held = 1;
        ts.pressed = true; ts.x = 5; ts.y = 6;                 /* a finger */
        kb_mouse_frame(&p, &in, false, &ts);
        CHECK(ts.x == 5 && ts.y == 6 && !p.m.shown);
    }

    printf("keyboard_typed_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
