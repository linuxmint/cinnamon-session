/*
 * csp-keyfile.h: GKeyFile extensions
 *
 * Copyright (C) 2008, 2009 Novell, Inc.
 *
 * Based on code from panel-keyfile.h (from gnome-panel)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 *
 * Authors:
 *        Vincent Untz <vuntz@gnome.org>
 */

#ifndef CSP_KEYFILE_H
#define CSP_KEYFILE_H

#include "glib.h"

G_BEGIN_DECLS

#define CSP_KEY_FILE_DESKTOP_KEY_AUTOSTART_ENABLED "X-GNOME-Autostart-enabled"

void      csp_key_file_populate        (GKeyFile *keyfile);

gboolean  csp_key_file_to_file         (GKeyFile       *keyfile,
                                        const gchar    *path,
                                        GError        **error);

gboolean csp_key_file_get_boolean      (GKeyFile       *keyfile,
                                        const gchar    *key,
                                        gboolean        default_value);
gboolean csp_key_file_get_shown        (GKeyFile       *keyfile,
                                        const char     *current_desktop);
#define csp_key_file_get_string(key_file, key) \
         g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP, key, NULL)
#define csp_key_file_get_locale_string(key_file, key) \
         g_key_file_get_locale_string(key_file, G_KEY_FILE_DESKTOP_GROUP, key, NULL, NULL)

#define csp_key_file_set_boolean(key_file, key, value) \
         g_key_file_set_boolean (key_file, G_KEY_FILE_DESKTOP_GROUP, key, value)
#define csp_key_file_set_string(key_file, key, value) \
         g_key_file_set_string (key_file, G_KEY_FILE_DESKTOP_GROUP, key, value)
void    csp_key_file_set_locale_string (GKeyFile    *keyfile,
                                        const gchar *key,
                                        const gchar *value);

void csp_key_file_ensure_C_key         (GKeyFile   *keyfile,
                                        const char *key);

G_END_DECLS

#endif /* CSP_KEYFILE_H */
