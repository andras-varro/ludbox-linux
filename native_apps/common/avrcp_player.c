/* See avrcp_player.h.  The bus half; the decisions live in avrcp_logic.c. */
#include "avrcp_player.h"
#include "hardware.h"
#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>

#define PLAYER_PATH   "/org/arca/ludfin/player"
#define PLAYER_IFACE  "org.mpris.MediaPlayer2.Player"
#define PROPS_IFACE   "org.freedesktop.DBus.Properties"
#define BLUEZ_NAME    "org.bluez"
#define MEDIA_IFACE   "org.bluez.Media1"
#define ADAPTER_PATH  "/org/bluez/hci0"   /* the one adapter a unit has */
#define IDENTITY      "Ludfin"
#define RETRY_MS      10000u
#define IO_MS         100u      /* bus service period (10 Hz) */
#define CALL_TMO_MS   5000
#define ACTQ_MAX      8

struct AvrcpPlayer {
    DBusConnection *conn;
    bool     registered;
    bool     pending;               /* a RegisterPlayer call is in flight */
    int      state;                 /* AvrcpState, or -1 before the first set_status */
    uint32_t next_try;              /* when to (re)register */
    bool     armed;                 /* next_try is on the caller's clock (set by the first poll) */
    uint32_t next_io;               /* when to service the bus next */
    bool     io_armed;
    uint32_t last_now;
    InputMedia q[ACTQ_MAX];
    int      qn;
};

static int cur_state(const AvrcpPlayer *p) { return p->state < 0 ? AVRCP_STOPPED : p->state; }

static void append_prop(DBusMessageIter *dict, const char *key, int type, const void *val)
{
    DBusMessageIter e, v;
    char sig[2] = { (char)type, 0 };
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, sig, &v);
    dbus_message_iter_append_basic(&v, type, val);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(dict, &e);
}

/* The property dictionary: just PlaybackStatus (a change), or all of them. */
static void append_props(DBusMessageIter *it, const AvrcpPlayer *p, bool all)
{
    DBusMessageIter dict;
    const char *st = avrcp_state_name(cur_state(p));
    dbus_bool_t yes = TRUE;
    const char *ident = IDENTITY;
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    append_prop(&dict, "PlaybackStatus", DBUS_TYPE_STRING, &st);
    if (all) {
        append_prop(&dict, "CanPlay", DBUS_TYPE_BOOLEAN, &yes);
        append_prop(&dict, "CanPause", DBUS_TYPE_BOOLEAN, &yes);
        append_prop(&dict, "CanGoNext", DBUS_TYPE_BOOLEAN, &yes);
        append_prop(&dict, "CanGoPrevious", DBUS_TYPE_BOOLEAN, &yes);
        append_prop(&dict, "CanControl", DBUS_TYPE_BOOLEAN, &yes);
        append_prop(&dict, "Identity", DBUS_TYPE_STRING, &ident);
    }
    dbus_message_iter_close_container(it, &dict);
}

static void emit_changed(AvrcpPlayer *p)
{
    if (!p->registered) return;
    DBusMessage *m = dbus_message_new_signal(PLAYER_PATH, PROPS_IFACE, "PropertiesChanged");
    if (!m) return;
    DBusMessageIter it, inv;
    const char *iface = PLAYER_IFACE;
    dbus_message_iter_init_append(m, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    append_props(&it, p, false);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inv);   /* invalidated: none */
    dbus_message_iter_close_container(&it, &inv);
    dbus_connection_send(p->conn, m, NULL);
    dbus_message_unref(m);
}

static void queue_action(AvrcpPlayer *p, InputMedia a)
{
    if (p->qn < ACTQ_MAX) p->q[p->qn++] = a;   /* past the cap the newest is dropped */
}

static void reg_done(DBusPendingCall *pc, void *data)
{
    AvrcpPlayer *p = data;
    DBusMessage *r = dbus_pending_call_steal_reply(pc);
    bool ok = false;
    p->pending = false;
    if (r) {
        if (dbus_message_get_type(r) != DBUS_MESSAGE_TYPE_ERROR) ok = true;
        else {   /* our own earlier registration still standing counts as registered */
            const char *n = dbus_message_get_error_name(r);
            if (n && strcmp(n, "org.bluez.Error.AlreadyExists") == 0) ok = true;
        }
        dbus_message_unref(r);
    }
    p->registered = ok;
    if (ok) emit_changed(p);          /* the state may have moved while the call was in flight */
    else p->next_try = p->last_now + RETRY_MS;
    dbus_pending_call_unref(pc);
}

static void try_register(AvrcpPlayer *p)
{
    DBusMessage *m = dbus_message_new_method_call(BLUEZ_NAME, ADAPTER_PATH, MEDIA_IFACE, "RegisterPlayer");
    if (!m) { p->next_try = p->last_now + RETRY_MS; return; }
    DBusMessageIter it;
    const char *path = PLAYER_PATH;
    dbus_message_iter_init_append(m, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &path);
    append_props(&it, p, true);
    DBusPendingCall *pc = NULL;
    if (dbus_connection_send_with_reply(p->conn, m, &pc, CALL_TMO_MS) && pc) {
        p->pending = true;
        if (!dbus_pending_call_set_notify(pc, reg_done, p, NULL)) {
            p->pending = false;
            dbus_pending_call_cancel(pc);
            dbus_pending_call_unref(pc);
        }
    }
    if (!p->pending) p->next_try = p->last_now + RETRY_MS;
    dbus_message_unref(m);
}

static void reply_prop(DBusConnection *c, DBusMessage *msg, AvrcpPlayer *p, const char *name)
{
    DBusMessage *r = dbus_message_new_method_return(msg);
    DBusMessageIter it, v;
    const char *st = avrcp_state_name(cur_state(p));
    const char *ident = IDENTITY;
    dbus_bool_t yes = TRUE;
    if (!r) return;
    dbus_message_iter_init_append(r, &it);
    if (strcmp(name, "PlaybackStatus") == 0) {
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &v);
        dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &st);
        dbus_message_iter_close_container(&it, &v);
    } else if (strcmp(name, "Identity") == 0) {
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &v);
        dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &ident);
        dbus_message_iter_close_container(&it, &v);
    } else if (strncmp(name, "Can", 3) == 0) {
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "b", &v);
        dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &yes);
        dbus_message_iter_close_container(&it, &v);
    } else {
        dbus_message_unref(r);
        r = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, "no such property");
        if (!r) return;
    }
    dbus_connection_send(c, r, NULL);
    dbus_message_unref(r);
}

static DBusHandlerResult filter(DBusConnection *c, DBusMessage *msg, void *data)
{
    AvrcpPlayer *p = data;

    if (dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
        const char *name = NULL, *old = NULL, *neu = NULL;
        if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old,
                                  DBUS_TYPE_STRING, &neu, DBUS_TYPE_INVALID)
            && name && strcmp(name, BLUEZ_NAME) == 0) {
            p->registered = false;                       /* bluetoothd went or came: our player is gone */
            p->next_try = p->last_now;                   /* register at once if it is back */
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL
        || !dbus_message_has_path(msg, PLAYER_PATH))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_has_interface(msg, PLAYER_IFACE)) {
        InputMedia a = avrcp_method_action(dbus_message_get_member(msg));
        if (a == INPUT_MEDIA_NONE) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        queue_action(p, a);
        DBusMessage *r = dbus_message_new_method_return(msg);
        if (r) { dbus_connection_send(c, r, NULL); dbus_message_unref(r); }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_has_interface(msg, PROPS_IFACE)) {
        if (dbus_message_is_method_call(msg, PROPS_IFACE, "Get")) {
            const char *iface = NULL, *name = NULL;
            if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &name,
                                      DBUS_TYPE_INVALID) && iface && name && strcmp(iface, PLAYER_IFACE) == 0)
                reply_prop(c, msg, p, name);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call(msg, PROPS_IFACE, "GetAll")) {
            DBusMessage *r = dbus_message_new_method_return(msg);
            if (r) {
                DBusMessageIter it;
                dbus_message_iter_init_append(r, &it);
                append_props(&it, p, true);
                dbus_connection_send(c, r, NULL);
                dbus_message_unref(r);
            }
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

AvrcpPlayer *avrcp_player_open(void)
{
    DBusConnection *c = dbus_bus_get_private(DBUS_BUS_SYSTEM, NULL);
    if (!c) return NULL;
    dbus_connection_set_exit_on_disconnect(c, FALSE);   /* a bus restart must not kill the app */
    AvrcpPlayer *p = calloc(1, sizeof *p);
    if (!p) { dbus_connection_close(c); dbus_connection_unref(c); return NULL; }
    p->conn = c;
    p->state = -1;
    if (!dbus_connection_add_filter(c, filter, p, NULL)) {
        dbus_connection_close(c); dbus_connection_unref(c); free(p);
        return NULL;
    }
    dbus_bus_add_match(c, "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
                          "member='NameOwnerChanged',arg0='" BLUEZ_NAME "'", NULL);
    return p;
}

void avrcp_player_set_status(AvrcpPlayer *p, AvrcpState s)
{
    if (!p) return;
    if (avrcp_state_changed(&p->state, (int)s)) emit_changed(p);
}

InputMedia avrcp_player_poll(AvrcpPlayer *p, uint32_t now_ms)
{
    if (!p || !p->conn) return INPUT_MEDIA_NONE;
    /* The bus is serviced at 10 Hz, not every frame: libdbus costs about a point of CPU at
     * frame rate (measured: Ludfin playing, 445 vs 409 ticks/30 s), and a button acting
     * up to 100 ms late is not noticed.  Actions already queued are still handed out. */
    if (p->qn == 0 && !avrcp_retry_tick(&p->io_armed, &p->next_io, now_ms)) return INPUT_MEDIA_NONE;
    p->next_io = now_ms + IO_MS;
    p->last_now = now_ms;
    bool due = avrcp_retry_tick(&p->armed, &p->next_try, now_ms);   /* also arms the clock on the first poll */
    if (!dbus_connection_get_is_connected(p->conn)) return INPUT_MEDIA_NONE;
    if (!p->registered && !p->pending && due) {
        p->next_try = now_ms + RETRY_MS;   /* a failure keeps this; a NameOwnerChanged overrides it */
        try_register(p);
    }
    dbus_connection_read_write(p->conn, 0);          /* write what is queued, read what is waiting */
    while (dbus_connection_dispatch(p->conn) == DBUS_DISPATCH_DATA_REMAINS) { }
    if (p->qn == 0) return INPUT_MEDIA_NONE;
    InputMedia a = p->q[0];
    memmove(p->q, p->q + 1, (size_t)(p->qn - 1) * sizeof p->q[0]);
    p->qn--;
    /* a key from the speaker wakes a dark panel AND acts, as poll_media does */
    hw_blank_note_activity();
    hw_blank_keepalive();
    return a;
}

void avrcp_player_close(AvrcpPlayer *p)
{
    if (!p) return;
    if (p->conn) {
        if (p->registered && dbus_connection_get_is_connected(p->conn)) {
            DBusMessage *m = dbus_message_new_method_call(BLUEZ_NAME, ADAPTER_PATH, MEDIA_IFACE, "UnregisterPlayer");
            if (m) {
                const char *path = PLAYER_PATH;
                dbus_message_append_args(m, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
                dbus_message_set_no_reply(m, TRUE);
                dbus_connection_send(p->conn, m, NULL);
                dbus_message_unref(m);
                dbus_connection_flush(p->conn);
            }
        }
        dbus_connection_close(p->conn);
        dbus_connection_unref(p->conn);
    }
    free(p);
}
