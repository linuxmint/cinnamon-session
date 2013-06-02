/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <config.h>

#include <glib.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "gsm-shell-extensions.h"

#define SHELL_SCHEMA "org.gnome.shell"
#define ENABLED_EXTENSIONS_KEY "enabled-extensions"

#define SHELL_EXTENSIONS_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_SHELL_EXTENSIONS, GsmShellExtensionsPrivate))

struct _GsmShellExtensionsPrivate
{
  GSettings *settings;
  guint num_extensions;
};

G_DEFINE_TYPE (GsmShellExtensions, gsm_shell_extensions, G_TYPE_OBJECT);

/**
 * gsm_shell_extensions_finalize:
 * @object: (in): A #GsmShellExtensions.
 *
 * Finalizer for a #GsmShellExtensions instance.  Frees any resources held by
 * the instance.
 */
static void
gsm_shell_extensions_finalize (GObject *object)
{
  GsmShellExtensions *extensions = GSM_SHELL_EXTENSIONS (object);
  GsmShellExtensionsPrivate *priv = extensions->priv;

  if (priv->settings != NULL)
    {
      g_object_unref (priv->settings);
      priv->settings = NULL;
    }

  G_OBJECT_CLASS (gsm_shell_extensions_parent_class)->finalize (object);
}

/**
 * gsm_shell_extensions_class_init:
 * @klass: (in): A #GsmShellExtensionsClass.
 *
 * Initializes the #GsmShellExtensionsClass and prepares the vtable.
 */
static void
gsm_shell_extensions_class_init (GsmShellExtensionsClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gsm_shell_extensions_finalize;
  g_type_class_add_private (object_class, sizeof (GsmShellExtensionsPrivate));
}

static void
gsm_shell_extensions_scan_dir (GsmShellExtensions *self,
                               GFile              *dir)
{
  GFileEnumerator *enumerator;
  GFileInfo *info;
  JsonParser *metadata_parser;

  metadata_parser = json_parser_new ();

  enumerator = g_file_enumerate_children (dir,
                                          "standard::*",
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          NULL);

  if (enumerator == NULL)
    return;

  while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
      gchar *metadata_filename;
      const gchar *metadata_uuid;
      gchar *dir_uuid;
      JsonObject *metadata_root;

      dir_uuid = (char *) g_file_info_get_name (info);

      metadata_filename = g_build_filename (g_file_get_path (dir),
                                            dir_uuid,
                                            "metadata.json",
                                            NULL);

      if (!json_parser_load_from_file (metadata_parser, metadata_filename, NULL))
        continue;

      g_free (metadata_filename);

      metadata_root = json_node_get_object (json_parser_get_root (metadata_parser));

      metadata_uuid = json_object_get_string_member (metadata_root, "uuid");
      if (!g_str_equal (metadata_uuid, dir_uuid))
        {
          g_warning ("Extension with dirname '%s' does not match metadata's UUID of '%s'. Skipping.",
                     dir_uuid, metadata_uuid);
          continue;
        }

      self->priv->num_extensions++;
    }
}

static void
gsm_shell_extensions_scan (GsmShellExtensions *self)
{
  gchar *dirname;
  GFile *dir;
  const gchar * const * system_data_dirs;

  /* User data dir first. */
  dirname = g_build_filename (g_get_user_data_dir (), "gnome-shell", "extensions", NULL);
  dir = g_file_new_for_path (dirname);
  g_free (dirname);

  gsm_shell_extensions_scan_dir (self, dir);
  g_object_unref (dir);

  system_data_dirs = g_get_system_data_dirs ();
  while ((*system_data_dirs) != '\0')
    {
      dirname = g_build_filename (*system_data_dirs, "gnome-shell", "extensions", NULL);
      dir = g_file_new_for_path (dirname);
      g_free (dirname);

      gsm_shell_extensions_scan_dir (self, dir);
      g_object_unref (dir);
      system_data_dirs ++;
    }
}

/**
 * gsm_shell_extensions_init:
 * @self: (in): A #GsmShellExtensions.
 *
 * Initializes the newly created #GsmShellExtensions instance.
 */
static void
gsm_shell_extensions_init (GsmShellExtensions *self)
{
  const gchar * const * schemas;

  self->priv = SHELL_EXTENSIONS_PRIVATE (self);

  /* Unfortunately, gsettings does not have a way to test
   * for the existance of a schema, so hack around it. */
  schemas = g_settings_list_schemas ();
  while (schemas != NULL)
    {
      if (g_str_equal (*schemas, SHELL_SCHEMA))
        {
          self->priv->settings = g_settings_new (SHELL_SCHEMA);
          break;
        }

      schemas ++;
    }

  if (self->priv->settings != NULL)
    gsm_shell_extensions_scan (self);
}

gboolean
gsm_shell_extensions_disable_all (GsmShellExtensions *self)
{
  return g_settings_set_strv (self->priv->settings,
                              ENABLED_EXTENSIONS_KEY,
                              NULL);
}

guint
gsm_shell_extensions_n_extensions (GsmShellExtensions *self)
{
  if (self->priv->settings == NULL)
    return 0;

  return self->priv->num_extensions;
}
