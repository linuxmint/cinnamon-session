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

#ifndef __GSM_SHELL_EXTENSIONS_H
#define __GSM_SHELL_EXTENSIONS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSM_TYPE_SHELL_EXTENSIONS            (gsm_shell_extensions_get_type ())
#define GSM_SHELL_EXTENSIONS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_SHELL_EXTENSIONS, GsmShellExtensions))
#define GSM_SHELL_EXTENSIONS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GSM_TYPE_SHELL_EXTENSIONS, GsmShellExtensionsClass))
#define GSM_IS_SHELL_EXTENSIONS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_SHELL_EXTENSIONS))
#define GSM_IS_SHELL_EXTENSIONS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GSM_TYPE_SHELL_EXTENSIONS))
#define GSM_SHELL_EXTENSIONS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GSM_TYPE_SHELL_EXTENSIONS, GsmShellExtensionsClass))

typedef struct _GsmShellExtensions        GsmShellExtensions;
typedef struct _GsmShellExtensionsClass   GsmShellExtensionsClass;
typedef struct _GsmShellExtensionsPrivate GsmShellExtensionsPrivate;

struct _GsmShellExtensions
{
    GObject parent;

    /*< private >*/
    GsmShellExtensionsPrivate *priv;
};

struct _GsmShellExtensionsClass
{
    GObjectClass parent_class;
};

GType gsm_shell_extensions_get_type                   (void) G_GNUC_CONST;

gboolean gsm_shell_extensions_disable_all             (GsmShellExtensions *self);

guint gsm_shell_extensions_n_extensions               (GsmShellExtensions *self);

G_END_DECLS

#endif /* __GSM_SHELL_EXTENSIONS_H */
