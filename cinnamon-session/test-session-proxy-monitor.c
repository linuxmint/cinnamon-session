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

static gboolean
session_manager_disconnect (void)
{
        if (sm_proxy != NULL) {
                g_object_unref (sm_proxy);
                sm_proxy = NULL;
        }

        return TRUE;
}


static void
on_props_changed (GDBusProxy *proxy,
                  GVariant   *changed_properties,
                  GStrv       invalidated_properties,
                  gpointer    user_data)
{
        GVariantDict dict;

        if (changed_properties) {
                g_variant_dict_init (&dict, changed_properties);
        }

        if (g_variant_dict_contains (&dict, "InhibitedActions")) {
                GVariant *v;

                v = g_dbus_proxy_get_cached_property (sm_proxy, "InhibitedActions");

                if (v == NULL) {
                        g_debug ("Couldn't fetch InhibitedActions property");
                        return;
                }

                g_printerr ("InhibitedActions = %u\n", g_variant_get_uint32 (v));

                g_variant_unref (v);
        }
}

static void
on_signal_emitted (GDBusProxy *proxy,
                   gchar      *sender_name,
                   gchar      *signal_name,
                   GVariant   *parameters,
                   gpointer    user_data)
{
        g_printerr ("Signal received...\n");

        if (g_strcmp0 (signal_name, "InhibitorAdded") == 0) {
                const gchar *id;
                GVariant *v;

                v = g_dbus_proxy_get_cached_property (proxy, "InhibitedActions");

                g_variant_get (parameters, "(o)",
                               &id);

                g_printerr ("signal: InhibitorAdded - %s - InhibitedActions is %u\n",
                            id,
                            g_variant_get_uint32 (v));

                g_variant_unref (v);
        }

        if (g_strcmp0 (signal_name, "InhibitorRemoved") == 0) {
                const gchar *id;
                GVariant *v;

                v = g_dbus_proxy_get_cached_property (proxy, "InhibitedActions");

                g_variant_get (parameters, "(o)",
                               &id);

                g_printerr ("signal: InhibitorRemoved - %s - InhibitedActions is %u\n",
                            id,
                            g_variant_get_uint32 (v));

                g_variant_unref (v);
        }
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

        g_signal_connect (sm_proxy,
                          "g-properties-changed",
                          G_CALLBACK (on_props_changed),
                          NULL);

        g_signal_connect (sm_proxy,
                          "g-signal",
                          G_CALLBACK (on_signal_emitted),
                          NULL);

        main_loop = g_main_loop_new (NULL, FALSE);

        g_main_loop_run (main_loop);
        g_main_loop_unref (main_loop);

        session_manager_disconnect ();

        return 0;
}
