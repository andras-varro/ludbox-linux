/* backup_parse.h — the pure half of the BACKUP page: reading the lines
 * device-files/arca-backup prints.  No framebuffer, no I/O, so a host test
 * drives it (tests/backup_parse_test.c).
 *
 * Contract (the engine's fixed stdout lines):
 *   item <id> unit=<yes|no> secret=<yes|no> reboot=<yes|no> [default=no] <name with spaces>
 *   note <id> <text>            (after an item line that has note=)
 *   archive <filename>          info host=<h> version=<v> date=<d>
 *   unknown <id>                imported <id>            done reboot=<yes|no>
 *   error <text>                (last line of a failed run)
 */
#ifndef BACKUP_PARSE_H
#define BACKUP_PARSE_H

#include <stdbool.h>
#include <stddef.h>

#define BK_ID_MAX    48
#define BK_NAME_MAX  64
#define BK_FILE_MAX  96
#define BK_HOST_MAX  48
#define BK_NOTE_MAX  160

typedef struct {
    char id[BK_ID_MAX];
    char name[BK_NAME_MAX];
    bool unit;      /* measured on or tied to this unit */
    bool secret;
    bool reboot;    /* takes effect only after a reboot */
    bool off_by_default;   /* default=no: not marked in the export view */
    char note[BK_NOTE_MAX];   /* optional text to show when the user marks it; "" = none */
} BkItem;

/* An id or archive file name that may be put on an argv: non-empty, [A-Za-z0-9._-]
 * only, no leading '-' or '.' (so never an option, "." or ".."). */
bool bk_name_ok(const char *s);

bool bk_parse_item(const char *line, BkItem *out);
/* "note <id> <text>": the engine prints it after that item's item line.  The text is
 * truncated to fit n (always terminated); false when the id is bad or the text empty. */
bool bk_parse_note(const char *line, char *id, size_t idn, char *note, size_t nn);
bool bk_parse_archive(const char *line, char *out, size_t n);
bool bk_parse_info(const char *line, char *host, size_t hn, char *version, size_t vn,
                   char *date, size_t dn);
bool bk_parse_unknown(const char *line, char *id, size_t n);
bool bk_parse_imported(const char *line, char *id, size_t n);
bool bk_parse_done(const char *line, bool *reboot);
/* The text after the last "error " line of out; false when there is none. */
bool bk_last_error(const char *out, char *msg, size_t n);

/* Does an item start marked?  Secrets never; a unit-bound item not when the
 * archive came from another unit (foreign); a default=no item not in the export
 * view (exporting), where the user opts in, but yes when restoring it. */
bool bk_start_marked(const BkItem *it, bool foreign, bool exporting);

#endif
