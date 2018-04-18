/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include <config.h>

#include <libintl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "mdm-signal-handler.h"
#include "mdm-log.h"

#include "csm-util.h"
#include "csm-manager.h"
#include "csm-session-fill.h"
#include "csm-store.h"
#include "csm-system.h"
#include "csm-fail-whale-dialog.h"

#define CSM_DBUS_NAME "org.gnome.SessionManager"

static gboolean failsafe = FALSE;
static gboolean show_version = FALSE;
static gboolean debug = FALSE;
static gboolean please_fail = FALSE;

static DBusGProxy *bus_proxy = NULL;

static void shutdown_cb (gpointer data);

static void
on_bus_name_lost (DBusGProxy *bus_proxy,
                  const char *name,
                  gpointer    data)
{
        g_warning ("Lost name on bus: %s, exiting", name);
        exit (1);
}

static gboolean
acquire_name_on_proxy (DBusGProxy *bus_proxy,
                       const char *name)
{
        GError     *error;
        guint       result;
        gboolean    res;
        gboolean    ret;

        ret = FALSE;

        if (bus_proxy == NULL) {
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (bus_proxy,
                                 "RequestName",
                                 &error,
                                 G_TYPE_STRING, name,
                                 G_TYPE_UINT, 0,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, &result,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", name, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", name);
                }
                goto out;
        }

        if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", name, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", name);
                }
                goto out;
        }

        /* register for name lost */
        dbus_g_proxy_add_signal (bus_proxy,
                                 "NameLost",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (bus_proxy,
                                     "NameLost",
                                     G_CALLBACK (on_bus_name_lost),
                                     NULL,
                                     NULL);


        ret = TRUE;

 out:
        return ret;
}

static gboolean
acquire_name (void)
{
        GError          *error;
        DBusGConnection *connection;

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (connection == NULL) {
                csm_util_init_error (TRUE,
                                     "Could not connect to session bus: %s",
                                     error->message);
                return FALSE;
        }

        bus_proxy = dbus_g_proxy_new_for_name_owner (connection,
                                                     DBUS_SERVICE_DBUS,
                                                     DBUS_PATH_DBUS,
                                                     DBUS_INTERFACE_DBUS,
                                                     &error);
        if (error != NULL) {
                csm_util_init_error (TRUE,
                                     "Could not connect to session bus: %s",
                                     error->message);
                return FALSE;
        }

        g_signal_connect_swapped (bus_proxy,
                                  "destroy",
                                  G_CALLBACK (shutdown_cb),
                                  NULL);

        if (! acquire_name_on_proxy (bus_proxy, CSM_DBUS_NAME) ) {
                csm_util_init_error (TRUE,
                                     "%s",
                                     "Could not acquire name on session bus");
                return FALSE;
        }

        return TRUE;
}

static void
shutdown_cb (gpointer data)
{
        CsmManager *manager = (CsmManager *)data;
        g_debug ("Calling shutdown callback function");

        /*
         * When the signal handler gets a shutdown signal, it calls
         * this function to inform CsmManager to not restart
         * applications in the off chance a handler is already queued
         * to dispatch following the below call to gtk_main_quit.
         */
        if (manager) {
            csm_manager_set_phase (manager, CSM_MANAGER_PHASE_EXIT);
            gtk_main_quit ();
        }
}

static gboolean
require_dbus_session (int      argc,
                      char   **argv,
                      GError **error)
{
        char **new_argv;
        int    i;

        if (g_getenv ("DBUS_SESSION_BUS_ADDRESS"))
                return TRUE;

        /* Just a sanity check to prevent infinite recursion if
         * dbus-launch fails to set DBUS_SESSION_BUS_ADDRESS 
         */
        g_return_val_if_fail (!g_str_has_prefix (argv[0], "dbus-launch"),
                              TRUE);

        /* +2 for our new arguments, +1 for NULL */
        new_argv = g_malloc ((argc + 3) * sizeof (*argv));

        new_argv[0] = "dbus-launch";
        new_argv[1] = "--exit-with-session";
        for (i = 0; i < argc; i++) {
                new_argv[i + 2] = argv[i];
	}
        new_argv[i + 2] = NULL;
        
        if (!execvp ("dbus-launch", new_argv)) {
                g_set_error (error, 
                             G_SPAWN_ERROR,
                             G_SPAWN_ERROR_FAILED,
                             "No session bus and could not exec dbus-launch: %s",
                             g_strerror (errno));
                return FALSE;
        }

        /* Should not be reached */
        return TRUE;
}

int
main (int argc, char **argv)
{
        struct sigaction  sa;
        GError           *error;
        char             *display_str;
        CsmManager       *manager;
        CsmStore         *client_store;
        static char     **override_autostart_dirs = NULL;
        static char      *session_name = NULL;
        static GOptionEntry entries[] = {
                { "autostart", 'a', 0, G_OPTION_ARG_STRING_ARRAY, &override_autostart_dirs, N_("Override standard autostart directories"), N_("AUTOSTART_DIR") },
                { "session", 0, 0, G_OPTION_ARG_STRING, &session_name, N_("Session to use"), N_("SESSION_NAME") },
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { "failsafe", 'f', 0, G_OPTION_ARG_NONE, &failsafe, N_("Do not load user-specified applications"), NULL },
                { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
                /* Translators: the 'fail whale' is the black dialog we show when something goes seriously wrong */
                { "whale", 0, 0, G_OPTION_ARG_NONE, &please_fail, N_("Show the fail whale dialog for testing"), NULL },
                { NULL, 0, 0, 0, NULL, NULL, NULL }
        };

        /* Make sure that we have a session bus */
        if (!require_dbus_session (argc, argv, &error)) {
                csm_util_init_error (TRUE, "%s", error->message);
        }

        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        GSettings *settings;

        settings = g_settings_new ("org.cinnamon.SessionManager");
        if (g_settings_get_boolean (settings, "debug")) {
            debug = TRUE;
        }
        g_clear_object (&settings);

        sa.sa_handler = SIG_IGN;
        sa.sa_flags = 0;
        sigemptyset (&sa.sa_mask);
        sigaction (SIGPIPE, &sa, 0);

        error = NULL;
        gtk_init_with_args (&argc, &argv,
                            (char *) _(" - the Cinnamon session manager"),
                            entries, GETTEXT_PACKAGE,
                            &error);
        if (error != NULL) {
                g_warning ("%s", error->message);
                exit (1);
        }

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (1);
        }

        if (please_fail) {
                csm_fail_whale_dialog_we_failed (TRUE, TRUE);
                gtk_main ();
                exit (1);
        }

        csm_util_export_activation_environment (NULL);
        csm_util_export_user_environment (NULL);

        mdm_log_init ();
        mdm_log_set_debug (debug);

        /* Set DISPLAY explicitly for all our children, in case --display
         * was specified on the command line.
         */
        display_str = gdk_get_display ();
        csm_util_setenv ("DISPLAY", display_str);
        g_free (display_str);

        const gchar *gtk_modules;
        gchar *new_gtk_modules = NULL;

        gtk_modules = g_getenv ("GTK_MODULES");

        if (gtk_modules != NULL && g_strstr_len (gtk_modules, -1, "overlay-scrollbar")) {
            int i = 0;
            new_gtk_modules = g_strconcat ("", NULL);

            gchar **module_list = g_strsplit (gtk_modules, ":", -1);

            for (i = 0; i < g_strv_length (module_list); i++) {
                if (!g_strstr_len (module_list[i], -1, "overlay-scrollbar")) {
                    gchar *tmp = new_gtk_modules;
                    new_gtk_modules = g_strconcat (tmp, ":", module_list[i], NULL);
                    g_free (tmp);
                }
            }

            g_strfreev (module_list);
        }

        if (new_gtk_modules) {
            csm_util_setenv ("GTK_MODULES", new_gtk_modules);
        }

        g_free (new_gtk_modules);

        /* Some third-party programs rely on GNOME_DESKTOP_SESSION_ID to
         * detect if GNOME is running. We keep this for compatibility reasons.
         */
        csm_util_setenv ("GNOME_DESKTOP_SESSION_ID", "this-is-deprecated");

        /* GTK Overlay scrollbars */
        settings = g_settings_new ("org.cinnamon.desktop.interface");

        if (g_settings_get_boolean (settings, "gtk-overlay-scrollbars")) {
            csm_util_setenv ("GTK_OVERLAY_SCROLLING", "1");
        } else {
            csm_util_setenv ("GTK_OVERLAY_SCROLLING", "0");
        }

        g_clear_object (&settings);

        client_store = csm_store_new ();

        /* Talk to logind before acquiring a name, since it does synchronous
         * calls at initialization time that invoke a main loop and if we
         * already owned a name, then we would service too early during
         * that main loop.
         */
        g_object_unref (csm_get_system ());

        if (!acquire_name ()) {
                csm_fail_whale_dialog_we_failed (TRUE, TRUE);
                gtk_main ();
                exit (1);
        }

        manager = csm_manager_new (client_store, failsafe);

        if (IS_STRING_EMPTY (session_name))
                session_name = _csm_manager_get_default_session (manager);

        csm_util_set_autostart_dirs (override_autostart_dirs);

        if (!csm_session_fill (manager, session_name)) {
                csm_util_init_error (TRUE, "Failed to load session \"%s\"", session_name ? session_name : "(null)");
        }

        csm_manager_start (manager);

        gtk_main ();

        if (manager != NULL) {
                g_debug ("Unreffing manager");
                g_object_unref (manager);
        }

        if (client_store != NULL) {
                g_object_unref (client_store);
        }

        if (bus_proxy != NULL) {
                g_object_unref (bus_proxy);
        }

        mdm_log_shutdown ();

        return 0;
}
