/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 1999 Free Software Foundation, Inc.
 * Copyright (C) 2007, 2009 Vincent Untz.
 * Copyright (C) 2008 Lucas Rocha.
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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

#ifndef __CSP_APP_H
#define __CSP_APP_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define CSP_TYPE_APP            (csp_app_get_type ())
#define CSP_APP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSP_TYPE_APP, CspApp))
#define CSP_APP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CSP_TYPE_APP, CspAppClass))
#define CSP_IS_APP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSP_TYPE_APP))
#define CSP_IS_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CSP_TYPE_APP))
#define CSP_APP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CSP_TYPE_APP, CspAppClass))

typedef struct _CspApp        CspApp;
typedef struct _CspAppClass   CspAppClass;

typedef struct _CspAppPrivate CspAppPrivate;

struct _CspAppClass
{
        GObjectClass parent_class;

        void (* changed) (CspApp *app);
        void (* removed) (CspApp *app);
};

struct _CspApp
{
        GObject parent_instance;

        CspAppPrivate *priv;
};

GType            csp_app_get_type          (void);

void             csp_app_create            (const char   *name,
                                            const char   *comment,
                                            const char   *exec);
void             csp_app_update            (CspApp       *app,
                                            const char   *name,
                                            const char   *comment,
                                            const char   *exec);

gboolean         csp_app_copy_desktop_file (const char   *uri);

void             csp_app_delete            (CspApp       *app);

const char      *csp_app_get_basename      (CspApp       *app);
const char      *csp_app_get_path          (CspApp       *app);

gboolean         csp_app_get_hidden        (CspApp       *app);
gboolean         csp_app_get_display       (CspApp       *app);

gboolean         csp_app_get_enabled       (CspApp       *app);
void             csp_app_set_enabled       (CspApp       *app,
                                            gboolean      enabled);

gboolean         csp_app_get_shown         (CspApp       *app);

const char      *csp_app_get_name          (CspApp       *app);
const char      *csp_app_get_exec          (CspApp       *app);
const char      *csp_app_get_comment       (CspApp       *app);

const char      *csp_app_get_description   (CspApp       *app);
GIcon           *csp_app_get_icon          (CspApp       *app);

/* private interface for CspAppManager only */

CspApp          *csp_app_new                      (const char   *path,
                                                   unsigned int  xdg_position);

void             csp_app_reload_at                (CspApp       *app,
                                                   const char   *path,
                                                   unsigned int  xdg_position);

unsigned int     csp_app_get_xdg_position         (CspApp       *app);
unsigned int     csp_app_get_xdg_system_position  (CspApp       *app);
void             csp_app_set_xdg_system_position  (CspApp       *app,
                                                   unsigned int  position);

G_END_DECLS

#endif /* __CSP_APP_H */
