/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006, 2010 Novell, Inc.
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

#include "gsm-session-fill.h"

#include "gsm-system.h"
#include "gsm-manager.h"
#include "gsm-process-helper.h"
#include "gsm-util.h"

#define GSM_KEYFILE_SESSION_GROUP "GNOME Session"
#define GSM_KEYFILE_RUNNABLE_KEY "IsRunnableHelper"
#define GSM_KEYFILE_FALLBACK_KEY "FallbackSession"
#define GSM_KEYFILE_REQUIRED_COMPONENTS_KEY "RequiredComponents"
#define GSM_KEYFILE_REQUIRED_PROVIDERS_KEY  "RequiredProviders"
#define GSM_KEYFILE_DEFAULT_PROVIDER_PREFIX "DefaultProvider"

/* See https://bugzilla.gnome.org/show_bug.cgi?id=641992 for discussion */
#define GSM_RUNNABLE_HELPER_TIMEOUT 3000 /* ms */

typedef void (*GsmFillHandleProvider) (const char *provides,
                                       const char *default_provider,
                                       const char *app_path,
                                       gpointer    user_data);
typedef void (*GsmFillHandleComponent) (const char *component,
                                        const char *app_path,
                                        gpointer    user_data);

static void
handle_default_providers (GKeyFile              *keyfile,
                          gboolean               look_in_saved_session,
                          GsmFillHandleProvider  callback,
                          gpointer               user_data)
{
        char **default_providers;
        int    i;

        g_assert (keyfile != NULL);
        g_assert (callback != NULL);

        default_providers = g_key_file_get_string_list (keyfile,
                                                        GSM_KEYFILE_SESSION_GROUP,
                                                        GSM_KEYFILE_REQUIRED_PROVIDERS_KEY,
                                                        NULL, NULL);

        if (!default_providers)
                return;

        for (i = 0; default_providers[i] != NULL; i++) {
                char *key;
                char *value;
                char *app_path;

                if (IS_STRING_EMPTY (default_providers[i]))
                        continue;

                key = g_strdup_printf ("%s-%s",
                                       GSM_KEYFILE_DEFAULT_PROVIDER_PREFIX,
                                       default_providers[i]);
                value = g_key_file_get_string (keyfile,
                                               GSM_KEYFILE_SESSION_GROUP, key,
                                               NULL);
                g_free (key);

                if (IS_STRING_EMPTY (value)) {
                        g_free (value);
                        continue;
                }

                g_debug ("fill: provider '%s' looking for component: '%s'",
                         default_providers[i], value);
                app_path = gsm_util_find_desktop_file_for_app_name (value,
                                                                    look_in_saved_session, TRUE);

                callback (default_providers[i], value, app_path, user_data);
                g_free (app_path);

                g_free (value);
        }

        g_strfreev (default_providers);
}

static void
handle_required_components (GKeyFile               *keyfile,
                            gboolean                look_in_saved_session,
                            GsmFillHandleComponent  callback,
                            gpointer                user_data)
{
        char **required_components;
        int    i;

        g_assert (keyfile != NULL);
        g_assert (callback != NULL);

        required_components = g_key_file_get_string_list (keyfile,
                                                          GSM_KEYFILE_SESSION_GROUP,
                                                          GSM_KEYFILE_REQUIRED_COMPONENTS_KEY,
                                                          NULL, NULL);

        if (!required_components)
                return;

        for (i = 0; required_components[i] != NULL; i++) {
                char *app_path;

                app_path = gsm_util_find_desktop_file_for_app_name (required_components[i],
                                                                    look_in_saved_session, TRUE);
                callback (required_components[i], app_path, user_data);
                g_free (app_path);
        }

        g_strfreev (required_components);
}

static void
check_required_providers_helper (const char *provides,
                                 const char *default_provider,
                                 const char *app_path,
                                 gpointer    user_data)
{
        gboolean *error = user_data;

        if (app_path == NULL) {
                g_warning ("Unable to find default provider '%s' of required provider '%s'",
                           default_provider, provides);
                *error = TRUE;
        }
}

static void
check_required_components_helper (const char *component,
                                  const char *app_path,
                                  gpointer    user_data)
{
        gboolean *error = user_data;

        if (app_path == NULL) {
                g_warning ("Unable to find required component '%s'", component);
                *error = TRUE;
        }
}

static gboolean
check_required (GKeyFile *keyfile)
{
        gboolean error = FALSE;

        g_debug ("fill: *** Checking required components and providers");

        handle_default_providers (keyfile, FALSE,
                                  check_required_providers_helper, &error);
        handle_required_components (keyfile, FALSE,
                                    check_required_components_helper, &error);

        g_debug ("fill: *** Done checking required components and providers");

        return !error;
}

static void
maybe_load_saved_session_apps (GsmManager *manager)
{
        GsmSystem *system;
        gboolean is_login;

        system = gsm_get_system ();
        is_login = gsm_system_is_login_session (system);
        g_object_unref (system);

        if (is_login)
                return;

        gsm_manager_add_autostart_apps_from_dir (manager, gsm_util_get_saved_session_dir ());
}

static void
append_required_providers_helper (const char *provides,
                                  const char *default_provider,
                                  const char *app_path,
                                  gpointer    user_data)
{
        GsmManager *manager = user_data;

        if (app_path == NULL)
                g_warning ("Unable to find default provider '%s' of required provider '%s'",
                           default_provider, provides);
        else
                gsm_manager_add_required_app (manager, app_path, provides);
}

static void
append_required_components_helper (const char *component,
                                   const char *app_path,
                                   gpointer    user_data)
{
        GsmManager *manager = user_data;

        if (app_path == NULL)
                g_warning ("Unable to find required component '%s'", component);
        else
                gsm_manager_add_required_app (manager, app_path, NULL);
}


static void
load_standard_apps (GsmManager *manager,
                    GKeyFile   *keyfile)
{
        g_debug ("fill: *** Adding required components");
        handle_required_components (keyfile, !gsm_manager_get_failsafe (manager),
                                    append_required_components_helper, manager);
        g_debug ("fill: *** Done adding required components");

        if (!gsm_manager_get_failsafe (manager)) {
                char **autostart_dirs;
                int    i;

                autostart_dirs = gsm_util_get_autostart_dirs ();

                maybe_load_saved_session_apps (manager);

                for (i = 0; autostart_dirs[i]; i++) {
                        gsm_manager_add_autostart_apps_from_dir (manager,
                                                                 autostart_dirs[i]);
                }

                g_strfreev (autostart_dirs);
        }

        g_debug ("fill: *** Adding default providers");
        handle_default_providers (keyfile, !gsm_manager_get_failsafe (manager),
                                  append_required_providers_helper, manager);
        g_debug ("fill: *** Done adding default providers");
}

static GKeyFile *
get_session_keyfile_if_valid (const char *path)
{
        GKeyFile  *keyfile;
        gsize      len;
        char     **list;

        g_debug ("fill: *** Looking if %s is a valid session file", path);

        keyfile = g_key_file_new ();

        if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL)) {
                g_debug ("Cannot use session '%s': non-existing or invalid file.", path);
                goto error;
        }

        if (!g_key_file_has_group (keyfile, GSM_KEYFILE_SESSION_GROUP)) {
                g_warning ("Cannot use session '%s': no '%s' group.", path, GSM_KEYFILE_SESSION_GROUP);
                goto error;
        }

        /* check that we have default providers defined for required providers */
        list = g_key_file_get_string_list (keyfile,
                                           GSM_KEYFILE_SESSION_GROUP,
                                           GSM_KEYFILE_REQUIRED_PROVIDERS_KEY,
                                           &len, NULL);
        if (list != NULL) {
                int i;
                char *key;
                char *value;

                for (i = 0; list[i] != NULL; i++) {
                        key = g_strdup_printf ("%s-%s", GSM_KEYFILE_DEFAULT_PROVIDER_PREFIX, list[i]);
                        value = g_key_file_get_string (keyfile,
                                                       GSM_KEYFILE_SESSION_GROUP, key,
                                                       NULL);
                        g_free (key);

                        if (IS_STRING_EMPTY (value)) {
                                g_free (value);
                                break;
                        }

                        g_free (value);
                }

                if (list[i] != NULL) {
                        g_warning ("Cannot use session '%s': required provider '%s' is not defined.", path, list[i]);
                        g_strfreev (list);
                        goto error;
                }

                g_strfreev (list);
        }

        /* we don't want an empty session, so if there's no required provider, check
         * that we do have some required components */
        if (len == 0) {
                list = g_key_file_get_string_list (keyfile,
                                                   GSM_KEYFILE_SESSION_GROUP,
                                                   GSM_KEYFILE_REQUIRED_COMPONENTS_KEY,
                                                   &len, NULL);
                if (list)
                        g_strfreev (list);
                if (len == 0) {
                        g_warning ("Cannot use session '%s': no component in the session.", path);
                        goto error;
                }
        }

        return keyfile;

error:
        g_key_file_free (keyfile);
        return NULL;
}

/**
 * find_valid_session_keyfile:
 * @session: name of session
 *
 * We look for the session file in XDG_CONFIG_HOME, XDG_CONFIG_DIRS and
 * XDG_DATA_DIRS. This enables users and sysadmins to override a specific
 * session that is shipped in XDG_DATA_DIRS.
 */
static GKeyFile *
find_valid_session_keyfile (const char *session)
{
        GPtrArray          *dirs;
        const char * const *system_config_dirs;
        const char * const *system_data_dirs;
        int                 i;
        GKeyFile           *keyfile;
        char               *basename;
        char               *path;

        dirs = g_ptr_array_new ();

        g_ptr_array_add (dirs, (gpointer) g_get_user_config_dir ());

        system_config_dirs = g_get_system_config_dirs ();
        for (i = 0; system_config_dirs[i]; i++)
                g_ptr_array_add (dirs, (gpointer) system_config_dirs[i]);

        system_data_dirs = g_get_system_data_dirs ();
        for (i = 0; system_data_dirs[i]; i++)
                g_ptr_array_add (dirs, (gpointer) system_data_dirs[i]);

        keyfile = NULL;
        basename = g_strdup_printf ("%s.session", session);
        path = NULL;

        for (i = 0; i < dirs->len; i++) {
                path = g_build_filename (dirs->pdata[i], "gnome-session", "sessions", basename, NULL);
                keyfile = get_session_keyfile_if_valid (path);
                if (keyfile != NULL)
                        break;
        }

        if (dirs)
                g_ptr_array_free (dirs, TRUE);
        if (basename)
                g_free (basename);
        if (path)
                g_free (path);

        return keyfile;
}

static GKeyFile *
get_session_keyfile (const char *session,
                     char      **actual_session,
                     gboolean   *is_fallback)
{
        GKeyFile *keyfile;
        gboolean  session_runnable;
        char     *value;
        GError *error = NULL;

        *actual_session = NULL;

        g_debug ("fill: *** Getting session '%s'", session);

        keyfile = find_valid_session_keyfile (session);

        if (!keyfile)
                return NULL;

        session_runnable = TRUE;

        value = g_key_file_get_string (keyfile,
                                       GSM_KEYFILE_SESSION_GROUP, GSM_KEYFILE_RUNNABLE_KEY,
                                       NULL);
        if (!IS_STRING_EMPTY (value)) {
                g_debug ("fill: *** Launching helper '%s' to know if session is runnable", value);
                session_runnable = gsm_process_helper (value, GSM_RUNNABLE_HELPER_TIMEOUT, &error);
                if (!session_runnable) {
                        g_warning ("Session '%s' runnable check failed: %s", session,
                                   error->message);
                        g_clear_error (&error);
                }
        }
        g_free (value);

        if (session_runnable) {
                session_runnable = check_required (keyfile);
        }

        if (session_runnable) {
                *actual_session = g_strdup (session);
                if (is_fallback)
                        *is_fallback = FALSE;
                return keyfile;
        }

        g_debug ("fill: *** Session is not runnable");

        /* We can't run this session, so try to use the fallback */
        value = g_key_file_get_string (keyfile,
                                       GSM_KEYFILE_SESSION_GROUP, GSM_KEYFILE_FALLBACK_KEY,
                                       NULL);

        g_key_file_free (keyfile);
        keyfile = NULL;

        if (!IS_STRING_EMPTY (value)) {
                if (is_fallback)
                        *is_fallback = TRUE;
                keyfile = get_session_keyfile (value, actual_session, NULL);
        }
        g_free (value);

        return keyfile;
}

gboolean
gsm_session_fill (GsmManager  *manager,
                  const char  *session)
{
        GKeyFile *keyfile;
        gboolean is_fallback;
        char *actual_session;

        keyfile = get_session_keyfile (session, &actual_session, &is_fallback);

        if (!keyfile)
                return FALSE;

        _gsm_manager_set_active_session (manager, actual_session, is_fallback);

        g_free (actual_session);

        load_standard_apps (manager, keyfile);

        g_key_file_free (keyfile);

        return TRUE;
}
