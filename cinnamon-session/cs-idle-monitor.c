/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA 02110-1335, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <time.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/extensions/sync.h>

#ifdef HAVE_XTEST
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#endif /* HAVE_XTEST */

#include <glib.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>

#include "cs-idle-monitor.h"

static void cs_idle_monitor_finalize   (GObject             *object);

#define CS_IDLE_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CS_TYPE_IDLE_MONITOR, CSIdleMonitorPrivate))

struct CSIdleMonitorPrivate
{
        Display     *display;

        GHashTable  *watches;
        int          sync_event_base;
        XSyncCounter counter;

        /* For use with XTest */
        int         *keycode;
        int          keycode1;
        int          keycode2;
        gboolean     have_xtest;
};

typedef struct
{
        Display               *display;
        guint                  id;
        XSyncValue             interval;
        CSIdleMonitorWatchFunc callback;
        gpointer               user_data;
        XSyncAlarm             xalarm_positive;
        XSyncAlarm             xalarm_negative;
} CSIdleMonitorWatch;

static guint32 watch_serial = 1;

G_DEFINE_TYPE (CSIdleMonitor, cs_idle_monitor, G_TYPE_OBJECT)

static gint64
_xsyncvalue_to_int64 (XSyncValue value)
{
        return ((guint64) XSyncValueHigh32 (value)) << 32
                | (guint64) XSyncValueLow32 (value);
}

static XSyncValue
_int64_to_xsyncvalue (gint64 value)
{
        XSyncValue ret;

        XSyncIntsToValue (&ret, value, ((guint64)value) >> 32);

        return ret;
}

static void
cs_idle_monitor_dispose (GObject *object)
{
        CSIdleMonitor *monitor;

        g_return_if_fail (CS_IS_IDLE_MONITOR (object));

        monitor = CS_IDLE_MONITOR (object);

        if (monitor->priv->watches != NULL) {
                g_hash_table_destroy (monitor->priv->watches);
                monitor->priv->watches = NULL;
        }

        G_OBJECT_CLASS (cs_idle_monitor_parent_class)->dispose (object);
}

static gboolean
_find_alarm (gpointer            key,
             CSIdleMonitorWatch *watch,
             XSyncAlarm         *alarm)
{
        g_debug ("Searching for %d in %d,%d", (int)*alarm, (int)watch->xalarm_positive, (int)watch->xalarm_negative);
        if (watch->xalarm_positive == *alarm
            || watch->xalarm_negative == *alarm) {
                return TRUE;
        }
        return FALSE;
}

static CSIdleMonitorWatch *
find_watch_for_alarm (CSIdleMonitor *monitor,
                      XSyncAlarm     alarm)
{
        CSIdleMonitorWatch *watch;

        watch = g_hash_table_find (monitor->priv->watches,
                                   (GHRFunc)_find_alarm,
                                   &alarm);
        return watch;
}

#ifdef HAVE_XTEST
static gboolean
send_fake_event (CSIdleMonitor *monitor)
{
        if (! monitor->priv->have_xtest) {
                return FALSE;
        }

        g_debug ("CSIdleMonitor: sending fake key");

        XLockDisplay (monitor->priv->display);
        XTestFakeKeyEvent (monitor->priv->display,
                           *monitor->priv->keycode,
                           True,
                           CurrentTime);
        XTestFakeKeyEvent (monitor->priv->display,
                           *monitor->priv->keycode,
                           False,
                           CurrentTime);
        XUnlockDisplay (monitor->priv->display);

        /* Swap the keycode */
        if (monitor->priv->keycode == &monitor->priv->keycode1) {
                monitor->priv->keycode = &monitor->priv->keycode2;
        } else {
                monitor->priv->keycode = &monitor->priv->keycode1;
        }

        return TRUE;
}
#endif /* HAVE_XTEST */

void
cs_idle_monitor_reset (CSIdleMonitor *monitor)
{
        g_return_if_fail (CS_IS_IDLE_MONITOR (monitor));

#ifdef HAVE_XTEST
        /* FIXME: is there a better way to reset the IDLETIME? */
        send_fake_event (monitor);
#endif
}

static void
handle_alarm_notify_event (CSIdleMonitor         *monitor,
                           XSyncAlarmNotifyEvent *alarm_event)
{
        CSIdleMonitorWatch *watch;
        gboolean            res;
        gboolean            condition;

        if (alarm_event->state == XSyncAlarmDestroyed) {
                return;
        }

        watch = find_watch_for_alarm (monitor, alarm_event->alarm);

        if (watch == NULL) {
                g_debug ("Unable to find watch for alarm %d", (int)alarm_event->alarm);
                return;
        }

        g_debug ("Watch %d fired, idle time = %" G_GINT64_FORMAT,
                 watch->id,
                 _xsyncvalue_to_int64 (alarm_event->counter_value));

        if (alarm_event->alarm == watch->xalarm_positive) {
                condition = TRUE;
        } else {
                condition = FALSE;
        }

        res = TRUE;
        if (watch->callback != NULL) {
                res = watch->callback (monitor,
                                       watch->id,
                                       condition,
                                       watch->user_data);
        }

        if (! res) {
                /* reset all timers */
                g_debug ("CSIdleMonitor: callback returned FALSE; resetting idle time");
                cs_idle_monitor_reset (monitor);
        }
}

static GdkFilterReturn
xevent_filter (GdkXEvent     *xevent,
               GdkEvent      *event,
               CSIdleMonitor *monitor)
{
        XEvent                *ev;
        XSyncAlarmNotifyEvent *alarm_event;

        ev = xevent;
        if (ev->xany.type != monitor->priv->sync_event_base + XSyncAlarmNotify) {
                return GDK_FILTER_CONTINUE;
        }

        alarm_event = xevent;

        handle_alarm_notify_event (monitor, alarm_event);

        return GDK_FILTER_CONTINUE;
}

static gboolean
init_xsync (CSIdleMonitor *monitor)
{
        int                 sync_error_base;
        int                 res;
        int                 major;
        int                 minor;
        int                 i;
        int                 ncounters;
        XSyncSystemCounter *counters;

        res = XSyncQueryExtension (monitor->priv->display,
                                   &monitor->priv->sync_event_base,
                                   &sync_error_base);
        if (! res) {
                g_warning ("CSIdleMonitor: Sync extension not present");
                return FALSE;
        }

        res = XSyncInitialize (monitor->priv->display, &major, &minor);
        if (! res) {
                g_warning ("CSIdleMonitor: Unable to initialize Sync extension");
                return FALSE;
        }

        counters = XSyncListSystemCounters (monitor->priv->display, &ncounters);
        for (i = 0; i < ncounters; i++) {
                if (counters[i].name != NULL
                    && strcmp (counters[i].name, "IDLETIME") == 0) {
                        monitor->priv->counter = counters[i].counter;
                        break;
                }
        }
        XSyncFreeSystemCounterList (counters);

        if (monitor->priv->counter == None) {
                g_warning ("CSIdleMonitor: IDLETIME counter not found");
                return FALSE;
        }

        gdk_window_add_filter (NULL, (GdkFilterFunc)xevent_filter, monitor);

        return TRUE;
}

static void
_init_xtest (CSIdleMonitor *monitor)
{
#ifdef HAVE_XTEST
        int a, b, c, d;

        XLockDisplay (monitor->priv->display);
        monitor->priv->have_xtest = (XTestQueryExtension (monitor->priv->display, &a, &b, &c, &d) == True);
        if (monitor->priv->have_xtest) {
                monitor->priv->keycode1 = XKeysymToKeycode (monitor->priv->display, XK_Alt_L);
                if (monitor->priv->keycode1 == 0) {
                        g_warning ("keycode1 not existent");
                }
                monitor->priv->keycode2 = XKeysymToKeycode (monitor->priv->display, XK_Alt_R);
                if (monitor->priv->keycode2 == 0) {
                        monitor->priv->keycode2 = XKeysymToKeycode (monitor->priv->display, XK_Alt_L);
                        if (monitor->priv->keycode2 == 0) {
                                g_warning ("keycode2 not existent");
                        }
                }
                monitor->priv->keycode = &monitor->priv->keycode1;
        }
        XUnlockDisplay (monitor->priv->display);
#endif /* HAVE_XTEST */
}

static GObject *
cs_idle_monitor_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
        CSIdleMonitor *monitor;

        monitor = CS_IDLE_MONITOR (G_OBJECT_CLASS (cs_idle_monitor_parent_class)->constructor (type,
                                                                                               n_construct_properties,
                                                                                               construct_properties));

        monitor->priv->display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        _init_xtest (monitor);

        if (! init_xsync (monitor)) {
                g_object_unref (monitor);
                return NULL;
        }

        return G_OBJECT (monitor);
}

static void
cs_idle_monitor_class_init (CSIdleMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = cs_idle_monitor_finalize;
        object_class->dispose = cs_idle_monitor_dispose;
        object_class->constructor = cs_idle_monitor_constructor;

        g_type_class_add_private (klass, sizeof (CSIdleMonitorPrivate));
}

static guint32
get_next_watch_serial (void)
{
        guint32 serial;

        serial = watch_serial++;

        if ((gint32)watch_serial < 0) {
                watch_serial = 1;
        }

        /* FIXME: make sure it isn't in the hash */

        return serial;
}

static CSIdleMonitorWatch *
idle_monitor_watch_new (guint interval)
{
        CSIdleMonitorWatch *watch;

        watch = g_slice_new0 (CSIdleMonitorWatch);
        watch->interval = _int64_to_xsyncvalue ((gint64)interval);
        watch->id = get_next_watch_serial ();
        watch->xalarm_positive = None;
        watch->xalarm_negative = None;

        return watch;
}

static void
idle_monitor_watch_free (CSIdleMonitorWatch *watch)
{
        if (watch == NULL) {
                return;
        }
        if (watch->xalarm_positive != None) {
                XSyncDestroyAlarm (watch->display, watch->xalarm_positive);
        }
        if (watch->xalarm_negative != None) {
                XSyncDestroyAlarm (watch->display, watch->xalarm_negative);
        }
        g_slice_free (CSIdleMonitorWatch, watch);
}

static void
cs_idle_monitor_init (CSIdleMonitor *monitor)
{
        monitor->priv = CS_IDLE_MONITOR_GET_PRIVATE (monitor);

        monitor->priv->watches = g_hash_table_new_full (NULL,
                                                        NULL,
                                                        NULL,
                                                        (GDestroyNotify)idle_monitor_watch_free);

        monitor->priv->counter = None;
}

static void
cs_idle_monitor_finalize (GObject *object)
{
        CSIdleMonitor *idle_monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CS_IS_IDLE_MONITOR (object));

        idle_monitor = CS_IDLE_MONITOR (object);

        g_return_if_fail (idle_monitor->priv != NULL);

        G_OBJECT_CLASS (cs_idle_monitor_parent_class)->finalize (object);
}

CSIdleMonitor *
cs_idle_monitor_new (void)
{
        GObject *idle_monitor;

        idle_monitor = g_object_new (CS_TYPE_IDLE_MONITOR,
                                     NULL);

        return CS_IDLE_MONITOR (idle_monitor);
}

static gboolean
_xsync_alarm_set (CSIdleMonitor      *monitor,
                  CSIdleMonitorWatch *watch)
{
        XSyncAlarmAttributes attr;
        XSyncValue           delta;
        guint                flags;

        flags = XSyncCACounter
                | XSyncCAValueType
                | XSyncCATestType
                | XSyncCAValue
                | XSyncCADelta
                | XSyncCAEvents;

        XSyncIntToValue (&delta, 0);
        attr.trigger.counter = monitor->priv->counter;
        attr.trigger.value_type = XSyncAbsolute;
        attr.trigger.wait_value = watch->interval;
        attr.delta = delta;
        attr.events = TRUE;

        attr.trigger.test_type = XSyncPositiveTransition;
        if (watch->xalarm_positive != None) {
                g_debug ("CSIdleMonitor: updating alarm for positive transition wait=%" G_GINT64_FORMAT,
                         _xsyncvalue_to_int64 (attr.trigger.wait_value));
                XSyncChangeAlarm (monitor->priv->display, watch->xalarm_positive, flags, &attr);
        } else {
                g_debug ("CSIdleMonitor: creating new alarm for positive transition wait=%" G_GINT64_FORMAT,
                         _xsyncvalue_to_int64 (attr.trigger.wait_value));
                watch->xalarm_positive = XSyncCreateAlarm (monitor->priv->display, flags, &attr);
        }

        attr.trigger.wait_value = _int64_to_xsyncvalue (_xsyncvalue_to_int64 (watch->interval) - 1);
        attr.trigger.test_type = XSyncNegativeTransition;
        if (watch->xalarm_negative != None) {
                g_debug ("CSIdleMonitor: updating alarm for negative transition wait=%" G_GINT64_FORMAT,
                         _xsyncvalue_to_int64 (attr.trigger.wait_value));
                XSyncChangeAlarm (monitor->priv->display, watch->xalarm_negative, flags, &attr);
        } else {
                g_debug ("CSIdleMonitor: creating new alarm for negative transition wait=%" G_GINT64_FORMAT,
                         _xsyncvalue_to_int64 (attr.trigger.wait_value));
                watch->xalarm_negative = XSyncCreateAlarm (monitor->priv->display, flags, &attr);
        }

        return TRUE;
}

guint
cs_idle_monitor_add_watch (CSIdleMonitor         *monitor,
                           guint                  interval,
                           CSIdleMonitorWatchFunc callback,
                           gpointer               user_data)
{
        CSIdleMonitorWatch *watch;

        g_return_val_if_fail (CS_IS_IDLE_MONITOR (monitor), 0);
        g_return_val_if_fail (callback != NULL, 0);

        watch = idle_monitor_watch_new (interval);
        watch->display = monitor->priv->display;
        watch->callback = callback;
        watch->user_data = user_data;

        _xsync_alarm_set (monitor, watch);

        g_hash_table_insert (monitor->priv->watches,
                             GUINT_TO_POINTER (watch->id),
                             watch);
        return watch->id;
}

void
cs_idle_monitor_remove_watch (CSIdleMonitor *monitor,
                              guint          id)
{
        g_return_if_fail (CS_IS_IDLE_MONITOR (monitor));

        g_hash_table_remove (monitor->priv->watches,
                             GUINT_TO_POINTER (id));
}
