/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#define SM_DBUS_NAME      "org.gnome.SessionManager"
#define SM_DBUS_PATH      "/org/gnome/SessionManager"
#define SM_DBUS_INTERFACE "org.gnome.SessionManager"

static GDBusConnection *connection = NULL;
static GDBusProxy      *sm_proxy = NULL;
static guint            cookie = 0;

static gboolean
session_manager_connect (void)
{

        if (connection == NULL) {
                GError *error;

                error = NULL;
                connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
                if (connection == NULL) {
                        g_message ("Failed to connect to the session bus: %s",
                                   error->message);
                        g_error_free (error);
                        exit (1);
                }
        }

        sm_proxy = g_dbus_proxy_new_sync (connection,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          NULL,
                                          SM_DBUS_NAME,
                                          SM_DBUS_PATH,
                                          SM_DBUS_INTERFACE,
                                          NULL, NULL);
        return (sm_proxy != NULL);
}

typedef enum {
        CSM_INHIBITOR_FLAG_LOGOUT      = 1 << 0,
        CSM_INHIBITOR_FLAG_SWITCH_USER = 1 << 1,
        CSM_INHIBITOR_FLAG_SUSPEND     = 1 << 2
} GsmInhibitFlag;

static gboolean
do_inhibit_for_window (GdkWindow *window)
{
        GError     *error;
        GVariant   *cookie_variant;
        const char *app_id;
        const char *reason;
        guint       toplevel_xid;
        guint       flags;

#if 1
        app_id = "nautilus-cd-burner";
        reason = "A CD burn is in progress.";
#else
        app_id = "nautilus";
        reason = "A file transfer is in progress.";
#endif
        toplevel_xid = gdk_x11_window_get_xid (window);
        flags = CSM_INHIBITOR_FLAG_LOGOUT
                | CSM_INHIBITOR_FLAG_SWITCH_USER
                | CSM_INHIBITOR_FLAG_SUSPEND;

        error = NULL;
        cookie_variant = g_dbus_proxy_call_sync (sm_proxy,
                                                 "Inhibit",
                                                 g_variant_new ("(susu)",
                                                                app_id,
                                                                toplevel_xid,
                                                                reason,
                                                                flags),
                                                 G_DBUS_CALL_FLAGS_NONE,
                                                 -1, NULL, &error);

        if (error != NULL) {
                g_warning ("Failed to inhibit: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_get (cookie_variant, "(u)", &cookie);
        g_debug ("Inhibiting session manager: %u", cookie);
        g_variant_unref (cookie_variant);

        return TRUE;
}
static gboolean
session_manager_disconnect (void)
{
        g_clear_object (&sm_proxy);
        g_clear_object (&connection);

        return TRUE;
}

static gboolean
do_uninhibit (void)
{
        GError  *error;
        GVariant *reply;

        error = NULL;
        reply = g_dbus_proxy_call_sync (sm_proxy,
                                        "Uninhibit",
                                        g_variant_new ("(u)", cookie),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1, NULL, &error);
        if (error != NULL) {
                g_warning ("Failed to uninhibit: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_unref (reply);

        cookie = 0;

        return TRUE;
}

static void
on_widget_show (GtkWidget *dialog,
                gpointer   data)
{
        gboolean res;

        res = do_inhibit_for_window (gtk_widget_get_window (dialog));
        if (! res) {
                g_warning ("Unable to register client with session manager");
        }
}

int
main (int   argc,
      char *argv[])
{
        gboolean   res;
        GtkWidget *dialog;

        g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

        gtk_init (&argc, &argv);

        res = session_manager_connect ();
        if (! res) {
                g_warning ("Unable to connect to session manager");
                exit (1);
        }

        g_timeout_add_seconds (30, (GSourceFunc)gtk_main_quit, NULL);

        dialog = gtk_message_dialog_new (NULL,
                                         0,
                                         GTK_MESSAGE_INFO,
                                         GTK_BUTTONS_CANCEL,
                                         "Inhibiting logout, switch user, and suspend.");

        g_signal_connect (dialog, "response", G_CALLBACK (gtk_main_quit), NULL);
        g_signal_connect (dialog, "show", G_CALLBACK (on_widget_show), NULL);
        gtk_widget_show (dialog);

        gtk_main ();

        do_uninhibit ();
        session_manager_disconnect ();

        return 0;
}
