/* Host regression for jellyfin/jf_queue_store.c: the persistent play queue file.
 *
 *   cd native_apps && gcc -Wall -Wextra -I common -I jellyfin -I jf_shared -o build/jf_queue_store_test tests/jf_queue_store_test.c \
 *       jellyfin/jf_queue_store.c jf_shared/jf_url.c common/config.c && ./build/jf_queue_store_test
 *
 * Round trip, UTF-8 and tab / newline / backslash in titles, a cut-short file, a wrong version, an empty queue, an index out
 * of range, bad lines between good ones, and the file half (atomic save, load, missing file).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jf_queue_store.h"
#include "jf_core.h"      /* JF_QUEUE_MAX: the loader app.c calls with it */

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static JfItem mk(unsigned n, const char *name)
{
    JfItem it;
    memset(&it, 0, sizeof it);
    it.type = JF_ITEM_TRACK;
    snprintf(it.id, sizeof it.id, "%032x", n);
    snprintf(it.name, sizeof it.name, "%s", name);
    snprintf(it.artist, sizeof it.artist, "Artist %u", n);
    snprintf(it.album_artist, sizeof it.album_artist, "AA");
    snprintf(it.album, sizeof it.album, "Album");
    snprintf(it.album_id, sizeof it.album_id, "%032x", n + 1000);
    snprintf(it.image_tag, sizeof it.image_tag, "tag%u", n);
    snprintf(it.artist_id, sizeof it.artist_id, "%032x", n + 2000);
    it.runtime_s = 200 + (int)n; it.norm_gain_mdb = -3500; it.year = 1999; it.index = 7;
    return it;
}

static bool same(const JfItem *a, const JfItem *b)
{
    return !strcmp(a->id, b->id) && !strcmp(a->name, b->name) && !strcmp(a->artist, b->artist) && !strcmp(a->album_artist, b->album_artist) &&
           !strcmp(a->album, b->album) && !strcmp(a->album_id, b->album_id) && !strcmp(a->image_tag, b->image_tag) && !strcmp(a->artist_id, b->artist_id) &&
           a->runtime_s == b->runtime_s && a->norm_gain_mdb == b->norm_gain_mdb && a->year == b->year && a->index == b->index && b->type == JF_ITEM_TRACK;
}

#define QCAP JF_QUEUE_MAX
static JfItem out[JF_QUEUE_MAX + 50];

int main(void)
{
    CHECK(QCAP == 500, "the loader cap the app uses is 500 (%d)", QCAP);
    JfItem in[4] = { mk(1, "Plain"), mk(2, "Caf\303\251 \342\230\203 \344\270\255\346\226\207"), mk(3, "tab\there\nnl\\back\rcr"), mk(4, "Last") };
    size_t len = 0;
    char *t = jf_queue_serialize(in, 4, 2, 4321, &len);
    int cur = -1, pos = -1;
    CHECK(t && len == strlen(t), "serialize returns the text and its length");
    int n = jf_queue_parse(t, len, out, QCAP, &cur, &pos);
    CHECK(n == 4 && cur == 2 && pos == 4321, "round trip: n %d cur %d pos %d", n, cur, pos);
    for (int i = 0; i < n && i < 4; i++) CHECK(same(&in[i], &out[i]), "round trip: item %d differs", i);
    CHECK(strchr(t, '\r') == NULL && strstr(t, "tab") != NULL && strstr(t, "tab")[3] == 92 && strstr(t, "tab")[4] == 't', "a tab / newline / return never reaches the file raw");

    /* truncation: cut anywhere inside the last line, or at the last line's end without its LF */
    size_t last = len - 1;
    while (last > 0 && t[last - 1] != '\n') last--;
    for (size_t cut = last; cut < len; cut += 7) {
        n = jf_queue_parse(t, cut, out, QCAP, &cur, &pos);
        CHECK(n == 3 && cur == 2, "cut at %zu: the whole lines load, the cut one is skipped (n %d)", cut, n);
    }
    n = jf_queue_parse(t, len - 1, out, QCAP, &cur, &pos);
    CHECK(n == 3, "the final LF missing: the last line counts as cut short (n %d)", n);

    /* a bad line in the middle is skipped, the rest still load, the index clamps to what loaded */
    char mid[4096];
    snprintf(mid, sizeof mid, "ARCA-JF-QUEUE 1\ncur\t9\t0\ngarbage line\n");
    size_t ml = strlen(mid);
    char *one = jf_queue_serialize(&in[0], 1, 0, 0, &len);
    snprintf(mid + ml, sizeof mid - ml, "%s", strchr(strchr(one, '\n') + 1, '\n') + 1);
    n = jf_queue_parse(mid, strlen(mid), out, QCAP, &cur, &pos);
    CHECK(n == 1 && cur == 0 && same(&in[0], &out[0]), "garbage skipped, cur 9 clamped (n %d cur %d)", n, cur);
    free(one);

    /* wrong version / not a queue file */
    CHECK(jf_queue_parse("ARCA-JF-QUEUE 2\ncur\t0\t0\n", 24, out, QCAP, &cur, &pos) == -1, "version 2 refused");
    CHECK(jf_queue_parse("hello\n", 6, out, QCAP, &cur, &pos) == -1 && jf_queue_parse("", 0, out, QCAP, &cur, &pos) == -1, "foreign / empty file refused");

    /* empty queue; negative / oversize index and position */
    free(t);
    t = jf_queue_serialize(in, 0, 5, -9, &len);
    n = jf_queue_parse(t, len, out, QCAP, &cur, &pos);
    CHECK(n == 0 && cur == 0 && pos == 0, "empty queue round trip");
    free(t);
    t = jf_queue_serialize(in, 4, 77, 10, &len);
    n = jf_queue_parse(t, len, out, QCAP, &cur, &pos);
    CHECK(n == 4 && cur == 3, "index past the end is clamped on write (%d)", cur);
    free(t);
    n = jf_queue_parse("ARCA-JF-QUEUE 1\ncur\t-4\t-5\n", 26, out, QCAP, &cur, &pos);
    CHECK(n == 0 && cur == 0 && pos == 0, "negative values clamp");

    /* cap: more entries than room */
    t = jf_queue_serialize(in, 4, 0, 0, &len);
    n = jf_queue_parse(t, len, out, 2, &cur, &pos);
    CHECK(n == 2, "cap respected (%d)", n);
    free(t);

    /* the file half */
    const char *path = "/tmp/jfp_queue_store_test_dir/sub/queue.txt";
    system("rm -rf /tmp/jfp_queue_store_test_dir");
    CHECK(jf_queue_load(path, out, QCAP, &cur, &pos) == -1, "missing file: -1");
    CHECK(jf_queue_save(path, in, 4, 1, 99) == 0, "save makes the directories and writes");
    n = jf_queue_load(path, out, QCAP, &cur, &pos);
    CHECK(n == 4 && cur == 1 && pos == 99 && same(&in[2], &out[2]), "load reads back what save wrote");
    CHECK(jf_queue_save(path, in, 0, 0, 0) == 0 && jf_queue_load(path, out, QCAP, &cur, &pos) == 0, "CLR saves an empty queue");
    system("rm -rf /tmp/jfp_queue_store_test_dir");

    /* a full queue: JF_QUEUE_MAX items save, read back whole and in order; an old 200-item file (same format) still loads;
     * a file holding more than the cap is clamped, never written past the array */
    static JfItem many[JF_QUEUE_MAX + 50];
    for (int i = 0; i < JF_QUEUE_MAX + 50; i++) many[i] = mk(100 + (unsigned)i, "Full");
    const char *p2 = "/tmp/jfp_queue_store_test_dir2/q.txt";
    system("rm -rf /tmp/jfp_queue_store_test_dir2");
    CHECK(jf_queue_save(p2, many, JF_QUEUE_MAX, JF_QUEUE_MAX - 1, 7) == 0, "a full queue saves");
    memset(out, 0, sizeof out);
    n = jf_queue_load(p2, out, JF_QUEUE_MAX, &cur, &pos);
    CHECK(n == JF_QUEUE_MAX && cur == JF_QUEUE_MAX - 1 && pos == 7 && same(&many[0], &out[0]) && same(&many[JF_QUEUE_MAX - 1], &out[JF_QUEUE_MAX - 1]), "a full queue loads back whole (n %d cur %d)", n, cur);
    CHECK(jf_queue_save(p2, many, 200, 150, 0) == 0, "an old-size file");
    n = jf_queue_load(p2, out, JF_QUEUE_MAX, &cur, &pos);
    CHECK(n == 200 && cur == 150 && same(&many[199], &out[199]), "a 200-item file still loads (n %d cur %d)", n, cur);
    CHECK(jf_queue_save(p2, many, JF_QUEUE_MAX + 50, JF_QUEUE_MAX + 20, 0) == 0, "a file past the cap");
    memset(&out[JF_QUEUE_MAX], 0x5a, 50 * sizeof out[0]);
    n = jf_queue_load(p2, out, JF_QUEUE_MAX, &cur, &pos);
    CHECK(n == JF_QUEUE_MAX && cur == JF_QUEUE_MAX - 1 && ((unsigned char *)&out[JF_QUEUE_MAX])[0] == 0x5a && ((unsigned char *)&out[JF_QUEUE_MAX + 49])[0] == 0x5a, "a file past the cap is clamped, the array behind it untouched (n %d)", n);
    system("rm -rf /tmp/jfp_queue_store_test_dir2");

    printf("jf_queue_store_test: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
