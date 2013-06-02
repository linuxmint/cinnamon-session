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

#ifndef __CSM_SHELL_EXTENSIONS_H
#define __CSM_SHELL_EXTENSIONS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CSM_TYPE_SHELL_EXTENSIONS            (csm_shell_extensions_get_type ())
#define CSM_SHELL_EXTENSIONS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_SHELL_EXTENSIONS, CsmShellExtensions))
#define CSM_SHELL_EXTENSIONS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  CSM_TYPE_SHELL_EXTENSIONS, CsmShellExtensionsClass))
#define CSM_IS_SHELL_EXTENSIONS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_SHELL_EXTENSIONS))
#define CSM_IS_SHELL_EXTENSIONS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  CSM_TYPE_SHELL_EXTENSIONS))
#define CSM_SHELL_EXTENSIONS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  CSM_TYPE_SHELL_EXTENSIONS, CsmShellExtensionsClass))

typedef struct _CsmShellExtensions        CsmShellExtensions;
typedef struct _CsmShellExtensionsClass   CsmShellExtensionsClass;
typedef struct _CsmShellExtensionsPrivate CsmShellExtensionsPrivate;

struct _CsmShellExtensions
{
    GObject parent;

    /*< private >*/
    CsmShellExtensionsPrivate *priv;
};

struct _CsmShellExtensionsClass
{
    GObjectClass parent_class;
};

GType csm_shell_extensions_get_type                   (void) G_GNUC_CONST;

gboolean csm_shell_extensions_disable_all             (CsmShellExtensions *self);

guint csm_shell_extensions_n_extensions               (CsmShellExtensions *self);

G_END_DECLS

#endif /* __CSM_SHELL_EXTENSIONS_H */
