/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * gsm-util.c
 * Copyright (C) 2008 Lucas Rocha.
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
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <dbus/dbus-glib.h>

#include "gsm-util.h"

static gchar *_saved_session_dir = NULL;

char *
gsm_util_find_desktop_file_for_app_name (const char *name,
                                         gboolean    look_in_saved_session,
                                         gboolean    autostart_first)
{
        char     *app_path;
        char    **app_dirs;
        GKeyFile *key_file;
        char     *desktop_file;
        int       i;

        app_path = NULL;

        app_dirs = gsm_util_get_desktop_dirs (look_in_saved_session, autostart_first);

        key_file = g_key_file_new ();

        desktop_file = g_strdup_printf ("%s.desktop", name);

        g_debug ("GsmUtil: Looking for file '%s'", desktop_file);

        for (i = 0; app_dirs[i] != NULL; i++) {
                g_debug ("GsmUtil: Looking in '%s'", app_dirs[i]);
        }

        g_key_file_load_from_dirs (key_file,
                                   desktop_file,
                                   (const char **) app_dirs,
                                   &app_path,
                                   G_KEY_FILE_NONE,
                                   NULL);

        if (app_path != NULL) {
                g_debug ("GsmUtil: found in XDG dirs: '%s'", app_path);
        }

        /* look for gnome vendor prefix */
        if (app_path == NULL) {
                g_free (desktop_file);
                desktop_file = g_strdup_printf ("gnome-%s.desktop", name);

                g_key_file_load_from_dirs (key_file,
                                           desktop_file,
                                           (const char **) app_dirs,
                                           &app_path,
                                           G_KEY_FILE_NONE,
                                           NULL);
                if (app_path != NULL) {
                        g_debug ("GsmUtil: found in XDG dirs: '%s'", app_path);
                }
        }

        g_free (desktop_file);
        g_key_file_free (key_file);

        g_strfreev (app_dirs);

        return app_path;
}

static gboolean
ensure_dir_exists (const char *dir)
{
        if (g_file_test (dir, G_FILE_TEST_IS_DIR))
                return TRUE;

        if (g_mkdir_with_parents (dir, 0755) == 0)
                return TRUE;

        if (errno == EEXIST)
                return g_file_test (dir, G_FILE_TEST_IS_DIR);

        g_warning ("GsmSessionSave: Failed to create directory %s: %s", dir, strerror (errno));

        return FALSE;
}

gchar *
gsm_util_get_empty_tmp_session_dir (void)
{
        char *tmp;
        gboolean exists;

        tmp = g_build_filename (g_get_user_config_dir (),
                                "gnome-session",
                                "saved-session.new",
                                NULL);

        exists = ensure_dir_exists (tmp);

        if (G_UNLIKELY (!exists)) {
                g_warning ("GsmSessionSave: could not create directory for saved session: %s", tmp);
                g_free (tmp);
                return NULL;
        } else {
                /* make sure it's empty */
                GDir       *dir;
                const char *filename;

                dir = g_dir_open (tmp, 0, NULL);
                if (dir) {
                        while ((filename = g_dir_read_name (dir))) {
                                char *path = g_build_filename (tmp, filename,
                                                               NULL);
                                g_unlink (path);
                        }
                        g_dir_close (dir);
                }
        }

        return tmp;
}

const gchar *
gsm_util_get_saved_session_dir (void)
{
        if (_saved_session_dir == NULL) {
                gboolean exists;

                _saved_session_dir =
                        g_build_filename (g_get_user_config_dir (),
                                          "gnome-session",
                                          "saved-session",
                                          NULL);

                exists = ensure_dir_exists (_saved_session_dir);

                if (G_UNLIKELY (!exists)) {
                        static gboolean printed_warning = FALSE;

                        if (!printed_warning) {
                                g_warning ("GsmSessionSave: could not create directory for saved session: %s", _saved_session_dir);
                                printed_warning = TRUE;
                        }

                        _saved_session_dir = NULL;

                        return NULL;
                }
        }

        return _saved_session_dir;
}

static char ** autostart_dirs;

void
gsm_util_set_autostart_dirs (char ** dirs)
{
        autostart_dirs = g_strdupv (dirs);
}

static char **
gsm_util_get_standard_autostart_dirs ()
{
        GPtrArray          *dirs;
        const char * const *system_config_dirs;
        const char * const *system_data_dirs;
        int                 i;

        dirs = g_ptr_array_new ();

        g_ptr_array_add (dirs,
                         g_build_filename (g_get_user_config_dir (),
                                           "autostart", NULL));

        system_data_dirs = g_get_system_data_dirs ();
        for (i = 0; system_data_dirs[i]; i++) {
                g_ptr_array_add (dirs,
                                 g_build_filename (system_data_dirs[i],
                                                   "gnome", "autostart", NULL));
        }

        system_config_dirs = g_get_system_config_dirs ();
        for (i = 0; system_config_dirs[i]; i++) {
                g_ptr_array_add (dirs,
                                 g_build_filename (system_config_dirs[i],
                                                   "autostart", NULL));
        }

        g_ptr_array_add (dirs, NULL);

        return (char **) g_ptr_array_free (dirs, FALSE);
}

char **
gsm_util_get_autostart_dirs ()
{
        if (autostart_dirs) {
                return g_strdupv ((char **)autostart_dirs);
        }

        return gsm_util_get_standard_autostart_dirs ();
}

char **
gsm_util_get_app_dirs ()
{
        GPtrArray          *dirs;
        const char * const *system_data_dirs;
        int                 i;

        dirs = g_ptr_array_new ();

        g_ptr_array_add (dirs,
			 g_build_filename (g_get_user_data_dir (),
					   "applications",
					   NULL));

        system_data_dirs = g_get_system_data_dirs ();
        for (i = 0; system_data_dirs[i]; i++) {
                g_ptr_array_add (dirs,
                                 g_build_filename (system_data_dirs[i],
                                                   "applications",
                                                   NULL));
        }

        g_ptr_array_add (dirs, NULL);

        return (char **) g_ptr_array_free (dirs, FALSE);
}

char **
gsm_util_get_desktop_dirs (gboolean include_saved_session,
                           gboolean autostart_first)
{
	char **apps;
	char **autostart;
	char **standard_autostart;
	char **result;
	int    size;
	int    i;

	apps = gsm_util_get_app_dirs ();
	autostart = gsm_util_get_autostart_dirs ();

        /* Still, check the standard autostart dirs for things like fulfilling session reqs,
         * if using a non-standard autostart dir for autostarting */
        if (autostart_dirs != NULL)
                standard_autostart = gsm_util_get_standard_autostart_dirs ();
        else
                standard_autostart = NULL;

	size = 0;
	for (i = 0; apps[i] != NULL; i++) { size++; }
	for (i = 0; autostart[i] != NULL; i++) { size++; }
        if (autostart_dirs != NULL)
                for (i = 0; standard_autostart[i] != NULL; i++) { size++; }
        if (include_saved_session)
                size += 1;

	result = g_new (char *, size + 1); /* including last NULL */

        size = 0;

        if (autostart_first) {
                if (include_saved_session)
                        result[size++] = g_strdup (gsm_util_get_saved_session_dir ());

                for (i = 0; autostart[i] != NULL; i++, size++) {
                        result[size] = autostart[i];
                }
                if (standard_autostart != NULL) {
                        for (i = 0; standard_autostart[i] != NULL; i++, size++) {
                                result[size] = standard_autostart[i];
                        }
                }
                for (i = 0; apps[i] != NULL; i++, size++) {
                        result[size] = apps[i];
                }
        } else {
                for (i = 0; apps[i] != NULL; i++, size++) {
                        result[size] = apps[i];
                }
                if (standard_autostart != NULL) {
                        for (i = 0; standard_autostart[i] != NULL; i++, size++) {
                                result[size] = standard_autostart[i];
                        }
                }
                for (i = 0; autostart[i] != NULL; i++, size++) {
                        result[size] = autostart[i];
                }

                if (include_saved_session)
                        result[size++] = g_strdup (gsm_util_get_saved_session_dir ());
        }

	g_free (apps);
	g_free (autostart);
	g_free (standard_autostart);

	result[size] = NULL;

	return result;
}

const char *
gsm_util_get_current_desktop ()
{
        static char *current_desktop = NULL;

        /* Support XDG_CURRENT_DESKTOP environment variable; this can be used
         * to abuse gnome-session in non-GNOME desktops. */
        if (!current_desktop) {
                const char *desktop;

                desktop = g_getenv ("XDG_CURRENT_DESKTOP");

                /* Note: if XDG_CURRENT_DESKTOP is set but empty, do as if it
                 * was not set */
                if (!desktop || desktop[0] == '\0')
                        current_desktop = g_strdup ("GNOME");
                else
                        current_desktop = g_strdup (desktop);
        }

        /* Using "*" means skipping desktop-related checks */
        if (g_strcmp0 (current_desktop, "*") == 0)
                return NULL;

        return current_desktop;
}

gboolean
gsm_util_text_is_blank (const char *str)
{
        if (str == NULL) {
                return TRUE;
        }

        while (*str) {
                if (!isspace(*str)) {
                        return FALSE;
                }

                str++;
        }

        return TRUE;
}

/**
 * gsm_util_init_error:
 * @fatal: whether or not the error is fatal to the login session
 * @format: printf-style error message format
 * @...: error message args
 *
 * Displays the error message to the user. If @fatal is %TRUE, gsm
 * will exit after displaying the message.
 *
 * This should be called for major errors that occur before the
 * session is up and running. (Notably, it positions the dialog box
 * itself, since no window manager will be running yet.)
 **/
void
gsm_util_init_error (gboolean    fatal,
                     const char *format, ...)
{
        GtkButtonsType  buttons;
        GtkWidget      *dialog;
        char           *msg;
        va_list         args;

        va_start (args, format);
        msg = g_strdup_vprintf (format, args);
        va_end (args);

        /* If option parsing failed, Gtk won't have been initialized... */
        if (!gdk_display_get_default ()) {
                if (!gtk_init_check (NULL, NULL)) {
                        /* Oh well, no X for you! */
                        g_printerr (_("Unable to start login session (and unable to connect to the X server)"));
                        g_printerr ("%s", msg);
                        exit (1);
                }
        }

        if (fatal)
                buttons = GTK_BUTTONS_NONE;
        else
                buttons = GTK_BUTTONS_CLOSE;

        dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR,
                                         buttons, "%s", msg);

        if (fatal)
                gtk_dialog_add_button (GTK_DIALOG (dialog),
                                       _("_Log Out"), GTK_RESPONSE_CLOSE);

        g_free (msg);

        gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
        gtk_dialog_run (GTK_DIALOG (dialog));

        gtk_widget_destroy (dialog);

        if (fatal) {
                if (gtk_main_level () > 0)
                        gtk_main_quit ();
                else
                        exit (1);
        }
}

/**
 * gsm_util_generate_startup_id:
 *
 * Generates a new SM client ID.
 *
 * Return value: an SM client ID.
 **/
char *
gsm_util_generate_startup_id (void)
{
        static int     sequence = -1;
        static guint   rand1 = 0;
        static guint   rand2 = 0;
        static pid_t   pid = 0;
        struct timeval tv;

        /* The XSMP spec defines the ID as:
         *
         * Version: "1"
         * Address type and address:
         *   "1" + an IPv4 address as 8 hex digits
         *   "2" + a DECNET address as 12 hex digits
         *   "6" + an IPv6 address as 32 hex digits
         * Time stamp: milliseconds since UNIX epoch as 13 decimal digits
         * Process-ID type and process-ID:
         *   "1" + POSIX PID as 10 decimal digits
         * Sequence number as 4 decimal digits
         *
         * XSMP client IDs are supposed to be globally unique: if
         * SmsGenerateClientID() is unable to determine a network
         * address for the machine, it gives up and returns %NULL.
         * GNOME and KDE have traditionally used a fourth address
         * format in this case:
         *   "0" + 16 random hex digits
         *
         * We don't even bother trying SmsGenerateClientID(), since the
         * user's IP address is probably "192.168.1.*" anyway, so a random
         * number is actually more likely to be globally unique.
         */

        if (!rand1) {
                rand1 = g_random_int ();
                rand2 = g_random_int ();
                pid = getpid ();
        }

        sequence = (sequence + 1) % 10000;
        gettimeofday (&tv, NULL);
        return g_strdup_printf ("10%.04x%.04x%.10lu%.3u%.10lu%.4d",
                                rand1,
                                rand2,
                                (unsigned long) tv.tv_sec,
                                (unsigned) tv.tv_usec,
                                (unsigned long) pid,
                                sequence);
}

static gboolean
gsm_util_update_activation_environment (const char  *variable,
                                        const char  *value,
                                        GError     **error)
{
        DBusGConnection *dbus_connection;
        DBusGProxy      *bus_proxy;
        GHashTable      *environment;
        gboolean         environment_updated;

        environment_updated = FALSE;
        bus_proxy = NULL;
        environment = NULL;

        dbus_connection = dbus_g_bus_get (DBUS_BUS_SESSION, error);

        if (dbus_connection == NULL) {
                return FALSE;
        }

        bus_proxy = dbus_g_proxy_new_for_name_owner (dbus_connection,
                                                     DBUS_SERVICE_DBUS,
                                                     DBUS_PATH_DBUS,
                                                     DBUS_INTERFACE_DBUS,
                                                     error);

        if (bus_proxy == NULL) {
                goto out;
        }

        environment = g_hash_table_new (g_str_hash, g_str_equal);

        g_hash_table_insert (environment, (void *) variable, (void *) value);

        if (!dbus_g_proxy_call (bus_proxy,
                                "UpdateActivationEnvironment", error,
                                DBUS_TYPE_G_STRING_STRING_HASHTABLE,
                                environment, G_TYPE_INVALID,
                                G_TYPE_INVALID))
                goto out;

        environment_updated = TRUE;

 out:

        if (bus_proxy != NULL) {
                g_object_unref (bus_proxy);
        }

        if (environment != NULL) {
                g_hash_table_destroy (environment);
        }

        return environment_updated;
}

void
gsm_util_setenv (const char *variable,
                 const char *value)
{
        GError *bus_error;

        g_setenv (variable, value, TRUE);

        bus_error = NULL;

        /* If this fails it isn't fatal, it means some things like session
         * management and keyring won't work in activated clients.
         */
        if (!gsm_util_update_activation_environment (variable, value, &bus_error)) {
                g_warning ("Could not make bus activated clients aware of %s=%s environment variable: %s", variable, value, bus_error->message);
                g_error_free (bus_error);
        }
}

GtkIconSize
gsm_util_get_computer_fail_icon_size (void)
{
        static GtkIconSize icon_size = 0;

        if (icon_size == 0)
                icon_size = gtk_icon_size_register ("gnome-session-computer-fail", 128, 128);

        return icon_size;
}
