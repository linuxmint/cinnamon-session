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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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

#include "gdm-signal-handler.h"
#include "gdm-log.h"

#include "gsm-util.h"
#include "gsm-manager.h"
#include "gsm-session-fill.h"
#include "gsm-store.h"
#include "gsm-system.h"
#include "gsm-xsmp-server.h"
#include "gsm-fail-whale-dialog.h"

#define GSM_DBUS_NAME "org.gnome.SessionManager"

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
                gsm_util_init_error (TRUE,
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
                gsm_util_init_error (TRUE,
                                     "Could not connect to session bus: %s",
                                     error->message);
                return FALSE;
        }

        g_signal_connect_swapped (bus_proxy,
                                  "destroy",
                                  G_CALLBACK (shutdown_cb),
                                  NULL);

        if (! acquire_name_on_proxy (bus_proxy, GSM_DBUS_NAME) ) {
                gsm_util_init_error (TRUE,
                                     "%s",
                                     "Could not acquire name on session bus");
                return FALSE;
        }

        return TRUE;
}

static gboolean
signal_cb (int      signo,
           gpointer data)
{
        int ret;
        GsmManager *manager;

        g_debug ("Got callback for signal %d", signo);

        ret = TRUE;

        switch (signo) {
        case SIGFPE:
        case SIGPIPE:
                /* let the fatal signals interrupt us */
                g_debug ("Caught signal %d, shutting down abnormally.", signo);
                ret = FALSE;
                break;
        case SIGINT:
        case SIGTERM:
                manager = (GsmManager *)data;
                gsm_manager_logout (manager, GSM_MANAGER_LOGOUT_MODE_FORCE, NULL);

                /* let the fatal signals interrupt us */
                g_debug ("Caught signal %d, shutting down normally.", signo);
                ret = TRUE;
                break;
        case SIGHUP:
                g_debug ("Got HUP signal");
                ret = TRUE;
                break;
        case SIGUSR1:
                g_debug ("Got USR1 signal");
                ret = TRUE;
                gdm_log_toggle_debug ();
                break;
        default:
                g_debug ("Caught unhandled signal %d", signo);
                ret = TRUE;

                break;
        }

        return ret;
}

static void
shutdown_cb (gpointer data)
{
        GsmManager *manager = (GsmManager *)data;
        g_debug ("Calling shutdown callback function");

        /*
         * When the signal handler gets a shutdown signal, it calls
         * this function to inform GsmManager to not restart
         * applications in the off chance a handler is already queued
         * to dispatch following the below call to gtk_main_quit.
         */
        gsm_manager_set_phase (manager, GSM_MANAGER_PHASE_EXIT);

        gtk_main_quit ();
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
        new_argv = g_malloc (argc + 3 * sizeof (*argv));

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
        GsmManager       *manager;
        GsmStore         *client_store;
        GsmXsmpServer    *xsmp_server;
        GdmSignalHandler *signal_handler;
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
                gsm_util_init_error (TRUE, "%s", error->message);
        }

        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        sa.sa_handler = SIG_IGN;
        sa.sa_flags = 0;
        sigemptyset (&sa.sa_mask);
        sigaction (SIGPIPE, &sa, 0);

        error = NULL;
        gtk_init_with_args (&argc, &argv,
                            (char *) _(" - the GNOME session manager"),
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
                gsm_fail_whale_dialog_we_failed (TRUE, TRUE, NULL);
                gtk_main ();
                exit (1);
        }

        gdm_log_init ();
        gdm_log_set_debug (debug);

        /* Set DISPLAY explicitly for all our children, in case --display
         * was specified on the command line.
         */
        display_str = gdk_get_display ();
        gsm_util_setenv ("DISPLAY", display_str);
        g_free (display_str);

        /* Some third-party programs rely on GNOME_DESKTOP_SESSION_ID to
         * detect if GNOME is running. We keep this for compatibility reasons.
         */
        gsm_util_setenv ("GNOME_DESKTOP_SESSION_ID", "this-is-deprecated");

        client_store = gsm_store_new ();

        /* Talk to logind before acquiring a name, since it does synchronous
         * calls at initialization time that invoke a main loop and if we
         * already owned a name, then we would service too early during
         * that main loop.
         */
        g_object_unref (gsm_get_system ());

        xsmp_server = gsm_xsmp_server_new (client_store);

        if (!acquire_name ()) {
                gsm_fail_whale_dialog_we_failed (TRUE, TRUE, NULL);
                gtk_main ();
                exit (1);
        }

        manager = gsm_manager_new (client_store, failsafe);

        signal_handler = gdm_signal_handler_new ();
        gdm_signal_handler_add_fatal (signal_handler);
        gdm_signal_handler_add (signal_handler, SIGFPE, signal_cb, NULL);
        gdm_signal_handler_add (signal_handler, SIGHUP, signal_cb, NULL);
        gdm_signal_handler_add (signal_handler, SIGUSR1, signal_cb, NULL);
        gdm_signal_handler_add (signal_handler, SIGTERM, signal_cb, manager);
        gdm_signal_handler_add (signal_handler, SIGINT, signal_cb, manager);
        gdm_signal_handler_set_fatal_func (signal_handler, shutdown_cb, manager);

        if (IS_STRING_EMPTY (session_name))
                session_name = _gsm_manager_get_default_session (manager);

        gsm_util_set_autostart_dirs (override_autostart_dirs);

        if (!gsm_session_fill (manager, session_name)) {
                gsm_util_init_error (TRUE, "Failed to load session \"%s\"", session_name ? session_name : "(null)");
        }

        gsm_xsmp_server_start (xsmp_server);
        gsm_manager_start (manager);

        gtk_main ();

        if (xsmp_server != NULL) {
                g_object_unref (xsmp_server);
        }

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

        gdm_log_shutdown ();

        return 0;
}
