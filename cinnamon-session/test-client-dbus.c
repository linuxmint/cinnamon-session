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

#include <glib.h>
#include <gio/gio.h>

#define SM_DBUS_NAME      "org.gnome.SessionManager"
#define SM_DBUS_PATH      "/org/gnome/SessionManager"
#define SM_DBUS_INTERFACE "org.gnome.SessionManager"

#define SM_CLIENT_DBUS_INTERFACE "org.gnome.SessionManager.ClientPrivate"

static GDBusConnection *connection = NULL;
static GDBusProxy      *sm_proxy = NULL;
static char            *client_id = NULL;
static GDBusProxy      *client_proxy = NULL;
static GMainLoop       *main_loop = NULL;

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

static void
on_client_query_end_session (GDBusProxy *proxy,
                             GVariant   *parameters)
{
        GError     *error;
        gboolean    is_ok;
        const char *reason;
        guint       flags;
        GVariant   *reply;

        is_ok = FALSE;
        reason = "Unsaved files";
        g_variant_get (parameters, "(u)", &flags);

        g_debug ("Got query end session signal flags=%u", flags);

        error = NULL;
        reply = g_dbus_proxy_call_sync (proxy,
                                        "EndSessionResponse",
                                        g_variant_new ("(bs)",
                                                       is_ok,
                                                       reason),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1, NULL, &error);

        if (error != NULL) {
                g_warning ("Failed to respond to EndSession: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_unref (reply);
        }
}

static void
on_client_end_session (GVariant *parameters)
{
        guint flags;
        g_variant_get (parameters, "(u)", &flags);
        g_debug ("Got end session signal flags=%u", flags);
}

static void
on_client_cancel_end_session (void)
{
        g_debug ("Got end session cancelled signal");
}

static void
on_client_stop (void)
{
        g_debug ("Got client stop signal");
        g_main_loop_quit (main_loop);
}

static void
on_client_dbus_signal (GDBusProxy  *proxy,
                       gchar       *sender_name,
                       gchar       *signal_name,
                       GVariant    *parameters,
                       gpointer     user_data)
{
        if (g_strcmp0 (signal_name, "Stop") == 0) {
                on_client_stop ();
        } else if (g_strcmp0 (signal_name, "CancelEndSession") == 0) {
                on_client_cancel_end_session ();
        } else if (g_strcmp0 (signal_name, "EndSession") == 0) {
                on_client_end_session (parameters);
        } else if (g_strcmp0 (signal_name, "QueryEndSession") == 0) {
                on_client_query_end_session (proxy, parameters);
        }
}

static gboolean
register_client (void)
{
        GError     *error;
        GVariant   *object_path_variant;
        const char *startup_id;
        const char *app_id;

        app_id = "gedit";

        startup_id = g_getenv ("DESKTOP_AUTOSTART_ID");
        if (!startup_id) {
                startup_id = "";
        }

        error = NULL;
        object_path_variant = g_dbus_proxy_call_sync (sm_proxy,
                                                      "RegisterClient",
                                                      g_variant_new ("(ss)",
                                                                     app_id,
                                                                     startup_id),
                                                      G_DBUS_CALL_FLAGS_NONE,
                                                      -1, NULL, &error);

        if (error != NULL) {
                g_warning ("Failed to register client: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_get (object_path_variant, "(o)", &client_id);
        g_debug ("Client registered with session manager: %s", client_id);

        client_proxy = g_dbus_proxy_new_sync (connection,
                                              G_DBUS_PROXY_FLAGS_NONE,
                                              NULL,
                                              SM_DBUS_NAME,
                                              client_id,
                                              SM_CLIENT_DBUS_INTERFACE,
                                              NULL, NULL);
        if (client_proxy != NULL) {
                g_signal_connect (client_proxy, "g-signal",
                                  G_CALLBACK (on_client_dbus_signal), NULL);
        }

        g_variant_unref (object_path_variant);

        return (client_proxy != NULL);
}

static gboolean
session_manager_disconnect (void)
{
        if (sm_proxy != NULL) {
                g_object_unref (sm_proxy);
                sm_proxy = NULL;
        }

        return TRUE;
}

static gboolean
unregister_client (void)
{
        GError *error;
        GVariant *reply;

        error = NULL;
        reply = g_dbus_proxy_call_sync (sm_proxy,
                                        "UnregisterClient",
                                        g_variant_new ("(o)", client_id),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1, NULL, &error);
        if (error != NULL) {
                g_warning ("Failed to unregister client: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_free (client_id);
        client_id = NULL;

        g_variant_unref (reply);

        return TRUE;
}

static gboolean
quit_test (gpointer data)
{
        g_main_loop_quit (main_loop);
        return FALSE;
}

int
main (int   argc,
      char *argv[])
{
        gboolean res;

        g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

        res = session_manager_connect ();
        if (! res) {
                g_warning ("Unable to connect to session manager");
                exit (1);
        }

        res = register_client ();
        if (! res) {
                g_warning ("Unable to register client with session manager");
        }

        main_loop = g_main_loop_new (NULL, FALSE);

        g_timeout_add_seconds (30, quit_test, NULL);

        g_main_loop_run (main_loop);
        g_main_loop_unref (main_loop);

        unregister_client ();
        session_manager_disconnect ();

        return 0;
}
