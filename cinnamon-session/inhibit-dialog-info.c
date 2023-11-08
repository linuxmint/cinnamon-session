/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <mccann@jhu.edu>
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
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gdesktopappinfo.h>

#include "csm-store.h"
#include "csm-client.h"
#include "csm-inhibitor.h"
#include "csm-util.h"
#include "inhibit-dialog-info.h"

static void
add_inhibitor (InhibitDialogInfo *info,
               CsmInhibitor     *inhibitor)
{
        const char     *name;
        const char     *app_id;
        char           *desktop_filename;
        GDesktopAppInfo *app_info;
        GKeyFile        *keyfile;
        GIcon           *gicon;
        char          **search_dirs;
        char           *freeme;

        app_info = NULL;
        name = NULL;
        freeme = NULL;
        gicon = NULL;

        app_id = csm_inhibitor_peek_app_id (inhibitor);

        if (IS_STRING_EMPTY (app_id)) {
                desktop_filename = NULL;
        } else if (! g_str_has_suffix (app_id, ".desktop")) {
                desktop_filename = g_strdup_printf ("%s.desktop", app_id);
        } else {
                desktop_filename = g_strdup (app_id);
        }

        if (desktop_filename != NULL) {
                search_dirs = csm_util_get_desktop_dirs (TRUE, FALSE);

                if (g_path_is_absolute (desktop_filename)) {
                        char *basename;

                        app_info = g_desktop_app_info_new_from_filename (desktop_filename);
                        if (app_info == NULL) {
                                g_warning ("Unable to load desktop file '%s'",
                                            desktop_filename);

                                basename = g_path_get_basename (desktop_filename);
                                g_free (desktop_filename);
                                desktop_filename = basename;
                        }
                }

                if (app_info == NULL) {
                        keyfile = g_key_file_new ();
                        if (g_key_file_load_from_dirs (keyfile, desktop_filename, (const gchar **)search_dirs, NULL, 0, NULL))
                                app_info = g_desktop_app_info_new_from_keyfile (keyfile);
                        g_key_file_free (keyfile);
                }

                /* look for a file with a vendor prefix */
                if (app_info == NULL) {
                        g_warning ("Unable to find desktop file '%s'",
                                   desktop_filename);
                        g_free (desktop_filename);
                        desktop_filename = g_strdup_printf ("gnome-%s.desktop", app_id);
                        keyfile = g_key_file_new ();
                        if (g_key_file_load_from_dirs (keyfile, desktop_filename, (const gchar **)search_dirs, NULL, 0, NULL))
                                app_info = g_desktop_app_info_new_from_keyfile (keyfile);
                        g_key_file_free (keyfile);
                }
                g_strfreev (search_dirs);

                if (app_info == NULL) {
                        g_warning ("Unable to find desktop file '%s'",
                                   desktop_filename);
                } else {
                        name = g_app_info_get_name (G_APP_INFO (app_info));
                        gicon = g_app_info_get_icon (G_APP_INFO (app_info));
                }
        }

        /* try client info */
        if (name == NULL) {
                const char *client_id;
                client_id = csm_inhibitor_peek_client_id (inhibitor);
                if (! IS_STRING_EMPTY (client_id)) {
                        CsmClient *client;
                        client = CSM_CLIENT (csm_store_lookup (info->clients, client_id));
                        if (client != NULL) {
                                freeme = csm_client_get_app_name (client);
                                name = freeme;
                        }
                }
        }

        if (name == NULL) {
                if (! IS_STRING_EMPTY (app_id)) {
                        name = app_id;
                } else {
                        name = _("Unknown");
                }
        }

        gchar *gicon_string = NULL;

        if (gicon != NULL) {
            gicon_string = g_icon_to_string (gicon);
        }
        
        // g_variant_builder_open (info->builder, G_VARIANT_TYPE_ARRAY);

        GVariant *item = g_variant_new ("(ssss)",
                                        name ? name : "none",
                                        gicon_string ? gicon_string : "none",
                                        csm_inhibitor_peek_reason (inhibitor) ? csm_inhibitor_peek_reason (inhibitor) : "none",
                                        csm_inhibitor_peek_id (inhibitor) ? csm_inhibitor_peek_id (inhibitor) : "none");

        g_variant_builder_add_value (info->builder, item);
        // g_variant_builder_close (info->builder);
        info->count++;

        g_free (gicon_string);
        g_free (desktop_filename);
        g_free (freeme);
        g_clear_object(&app_info);
}

static gboolean
add_to_builder (const char        *id,
                CsmInhibitor      *inhibitor,
                InhibitDialogInfo *info)
{
        add_inhibitor (info, inhibitor);
        return FALSE;
}

static void
populate_builder (InhibitDialogInfo *info)
{
        csm_store_foreach_remove (info->inhibitors,
                                  (CsmStoreFunc) add_to_builder,
                                  info);
}

GVariant *
build_inhibitor_list_for_dialog (CsmStore *inhibitors,
                                 CsmStore *clients,
                                 int       action)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(ssss)"));

    InhibitDialogInfo *info = g_new0 (InhibitDialogInfo, 1);
    info->action = action;
    info->builder = &builder;

    if (clients != NULL) {
        info->clients = g_object_ref (clients);
    }

    if (inhibitors != NULL) {
        info->inhibitors = g_object_ref (inhibitors);
    }

    populate_builder (info);

    g_variant_builder_close (&builder);

    g_object_unref (info->clients);
    g_object_unref (info->inhibitors);

    g_free (info);
    return g_variant_builder_end (&builder);
}
