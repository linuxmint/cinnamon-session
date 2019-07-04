/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * save-session.c - Small program to talk to session manager.

   Copyright (C) 1998 Tom Tromey
   Copyright (C) 2008 Red Hat, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
   02110-1335, USA.
*/

#include <config.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#define CSM_SERVICE_DBUS   "org.gnome.SessionManager"
#define CSM_PATH_DBUS      "/org/gnome/SessionManager"
#define CSM_INTERFACE_DBUS "org.gnome.SessionManager"

enum {
        CSM_LOGOUT_MODE_NORMAL = 0,
        CSM_LOGOUT_MODE_NO_CONFIRMATION,
        CSM_LOGOUT_MODE_FORCE
};

static gboolean opt_logout = FALSE;
static gboolean opt_power_off = FALSE;
static gboolean opt_reboot = FALSE;
static gboolean opt_no_prompt = FALSE;
static gboolean opt_force = FALSE;

static GOptionEntry options[] = {
        {"logout", '\0', 0, G_OPTION_ARG_NONE, &opt_logout, N_("Log out"), NULL},
        {"power-off", '\0', 0, G_OPTION_ARG_NONE, &opt_power_off, N_("Power off"), NULL},
        {"reboot", '\0', 0, G_OPTION_ARG_NONE, &opt_reboot, N_("Reboot"), NULL},
        {"force", '\0', 0, G_OPTION_ARG_NONE, &opt_force, N_("Ignoring any existing inhibitors"), NULL},
        {"no-prompt", '\0', 0, G_OPTION_ARG_NONE, &opt_no_prompt, N_("Don't prompt for user confirmation"), NULL},
        {NULL}
};

static void
display_error (const char *message)
{
        g_printerr ("%s\n", message);
}

static GDBusProxy *
get_sm_proxy (void)
{
        GDBusProxy      *sm_proxy;

        sm_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  NULL,
                                                  CSM_SERVICE_DBUS,
                                                  CSM_PATH_DBUS,
                                                  CSM_INTERFACE_DBUS,
                                                  NULL,
                                                  NULL);

        if (sm_proxy == NULL) {
                display_error (_("Could not connect to the session manager"));
                return NULL;
        }

        return sm_proxy;
}

static void
do_logout (unsigned int mode)
{
        GDBusProxy *sm_proxy;
        GError     *error;
        GVariant   *res;

        sm_proxy = get_sm_proxy ();
        if (sm_proxy == NULL) {
                return;
        }

        error = NULL;

        res = g_dbus_proxy_call_sync (sm_proxy,
                                      "Logout",
                                      g_variant_new ("(u)", mode),
                                      G_DBUS_CALL_FLAGS_NONE,
                                      -1,
                                      NULL,
                                      &error);

        if (!res) {
                if (error != NULL) {
                        g_warning ("Failed to call logout: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to call logout");
                }
        }

        g_variant_unref (res);

        if (sm_proxy != NULL) {
                g_object_unref (sm_proxy);
        }
}

static void
do_power_off (const char *action)
{
        GDBusProxy *sm_proxy;
        GError     *error;
        GVariant   *res;

        sm_proxy = get_sm_proxy ();
        if (sm_proxy == NULL) {
                return;
        }

        error = NULL;

        res = g_dbus_proxy_call_sync (sm_proxy,
                                      action,
                                      NULL,
                                      G_DBUS_CALL_FLAGS_NONE,
                                      -1,
                                      NULL,
                                      &error);

        if (!res) {
                if (error != NULL) {
                        g_warning ("Failed to call %s: %s",
                                   action, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to call %s", action);
                }
        }

        g_variant_unref (res);

        if (sm_proxy != NULL) {
                g_object_unref (sm_proxy);
        }
}

int
main (int argc, char *argv[])
{
        GError *error;
        int     conflicting_options;

        /* Initialize the i18n stuff */
        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        error = NULL;
        if (! gtk_init_with_args (&argc, &argv, NULL, options, NULL, &error)) {
                g_warning ("Unable to start: %s", error->message);
                g_error_free (error);
                exit (1);
        }

        conflicting_options = 0;
        if (opt_logout)
                conflicting_options++;
        if (opt_power_off)
                conflicting_options++;
        if (opt_reboot)
                conflicting_options++;
        if (conflicting_options > 1)
                display_error (_("Program called with conflicting options"));

        if (opt_power_off) {
                do_power_off ("Shutdown");
        } else if (opt_reboot) {
                do_power_off ("Reboot");
        } else {
                /* default to logout */

                if (opt_force)
                        do_logout (CSM_LOGOUT_MODE_FORCE);
                else if (opt_no_prompt)
                        do_logout (CSM_LOGOUT_MODE_NO_CONFIRMATION);
                else
                        do_logout (CSM_LOGOUT_MODE_NORMAL);
        }

        return 0;
}
