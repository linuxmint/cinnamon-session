/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * csm-session-save.c
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "csm-util.h"
#include "csm-autostart-app.h"
#include "csm-client.h"

#include "csm-session-save.h"

static gboolean csm_session_clear_saved_session (const char *directory,
                                                 GHashTable *discard_hash);

typedef struct {
        char        *dir;
        GHashTable  *discard_hash;
        GError     **error;
} SessionSaveData;

static gboolean
save_one_client (char            *id,
                 GObject         *object,
                 SessionSaveData *data)
{
        CsmClient  *client;
        GKeyFile   *keyfile;
        const char *app_id;
        char       *path = NULL;
        char       *filename = NULL;
        char       *contents = NULL;
        gsize       length = 0;
        char       *discard_exec;
        GError     *local_error;

        client = CSM_CLIENT (object);

        local_error = NULL;

        keyfile = csm_client_save (client, &local_error);

        if (keyfile == NULL || local_error) {
                goto out;
        }

        contents = g_key_file_to_data (keyfile, &length, &local_error);

        if (local_error) {
                goto out;
        }

        app_id = csm_client_peek_app_id (client);
        if (!IS_STRING_EMPTY (app_id)) {
                if (g_str_has_suffix (app_id, ".desktop"))
                        filename = g_strdup (app_id);
                else
                        filename = g_strdup_printf ("%s.desktop", app_id);

                path = g_build_filename (data->dir, filename, NULL);
        }

        if (!path || g_file_test (path, G_FILE_TEST_EXISTS)) {
                if (filename)
                        g_free (filename);
                if (path)
                        g_free (path);

                filename = g_strdup_printf ("%s.desktop",
                                            csm_client_peek_startup_id (client));
                path = g_build_filename (data->dir, filename, NULL);
        }

        g_file_set_contents (path,
                             contents,
                             length,
                             &local_error);

        if (local_error) {
                goto out;
        }

        discard_exec = g_key_file_get_string (keyfile,
                                              G_KEY_FILE_DESKTOP_GROUP,
                                              CSM_AUTOSTART_APP_DISCARD_KEY,
                                              NULL);
        if (discard_exec) {
                g_hash_table_insert (data->discard_hash,
                                     discard_exec, discard_exec);
        }

        g_debug ("CsmSessionSave: saved client %s to %s", id, filename);

out:
        if (keyfile != NULL) {
                g_key_file_free (keyfile);
        }

        g_free (contents);
        g_free (filename);
        g_free (path);

        /* in case of any error, stop saving session */
        if (local_error) {
                g_propagate_error (data->error, local_error);
                g_error_free (local_error);

                return TRUE;
        }

        return FALSE;
}

void
csm_session_save (CsmStore  *client_store,
                  GError   **error)
{
        const char      *save_dir;
        char            *tmp_dir;
        SessionSaveData  data;

        g_debug ("CsmSessionSave: Saving session");

        save_dir = csm_util_get_saved_session_dir ();
        if (save_dir == NULL) {
                g_warning ("CsmSessionSave: cannot create saved session directory");
                return;
        }

        tmp_dir = csm_util_get_empty_tmp_session_dir ();
        if (tmp_dir == NULL) {
                g_warning ("CsmSessionSave: cannot create new saved session directory");
                return;
        }

        /* save the session in a temp directory, and remember the discard
         * commands */
        data.dir = tmp_dir;
        data.discard_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, NULL);
        data.error = error;

        csm_store_foreach (client_store,
                           (CsmStoreFunc) save_one_client,
                           &data);

        if (!*error) {
                /* remove the old saved session */
                csm_session_clear_saved_session (save_dir, data.discard_hash);

                /* rename the temp session dir */
                if (g_file_test (save_dir, G_FILE_TEST_IS_DIR))
                        g_rmdir (save_dir);
                g_rename (tmp_dir, save_dir);
        } else {
                g_warning ("CsmSessionSave: error saving session: %s", (*error)->message);
                /* FIXME: we should create a hash table filled with the discard
                 * commands that are in desktop files from save_dir. */
                csm_session_clear_saved_session (tmp_dir, NULL);
                g_rmdir (tmp_dir);
        }

        g_hash_table_destroy (data.discard_hash);
        g_free (tmp_dir);
}

static gboolean
csm_session_clear_one_client (const char *filename,
                              GHashTable *discard_hash)
{
        gboolean  result = TRUE;
        GKeyFile *key_file = NULL;
        char     *discard_exec = NULL;

        g_debug ("CsmSessionSave: removing '%s' from saved session", filename);

        key_file = g_key_file_new ();
        if (g_key_file_load_from_file (key_file, filename,
                                       G_KEY_FILE_NONE, NULL)) {
                char **argv;
                int    argc;

                discard_exec = g_key_file_get_string (key_file,
                                                      G_KEY_FILE_DESKTOP_GROUP,
                                                      CSM_AUTOSTART_APP_DISCARD_KEY,
                                                      NULL);
                if (!discard_exec)
                        goto out;

                if (discard_hash && g_hash_table_lookup (discard_hash, discard_exec))
                        goto out;

                if (!g_shell_parse_argv (discard_exec, &argc, &argv, NULL))
                        goto out;

                result = g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                                        NULL, NULL, NULL, NULL) && result;

                g_strfreev (argv);
        } else {
                result = FALSE;
        }

out:
        if (key_file)
                g_key_file_free (key_file);
        if (discard_exec)
                g_free (discard_exec);

        result = (g_unlink (filename) == 0) && result;

        return result;
}

static gboolean
csm_session_clear_saved_session (const char *directory,
                                 GHashTable *discard_hash)
{
        GDir       *dir;
        const char *filename;
        gboolean    result = TRUE;
        GError     *error;

        g_debug ("CsmSessionSave: clearing currently saved session at %s",
                 directory);

        if (directory == NULL) {
                return FALSE;
        }

        error = NULL;
        dir = g_dir_open (directory, 0, &error);
        if (error) {
                g_warning ("CsmSessionSave: error loading saved session directory: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        while ((filename = g_dir_read_name (dir))) {
                char *path = g_build_filename (directory,
                                               filename, NULL);

                result = csm_session_clear_one_client (path, discard_hash)
                         && result;

                g_free (path);
        }

        g_dir_close (dir);

        return result;
}

void
csm_session_save_clear (void)
{
        const char *save_dir;

        g_debug ("CsmSessionSave: Clearing saved session");

        save_dir = csm_util_get_saved_session_dir ();
        if (save_dir == NULL) {
                g_warning ("CsmSessionSave: cannot create saved session directory");
                return;
        }

	csm_session_clear_saved_session (save_dir, NULL);
}
