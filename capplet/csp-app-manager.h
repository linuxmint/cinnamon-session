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

#ifndef __CSP_APP_MANAGER_H
#define __CSP_APP_MANAGER_H

#include <glib-object.h>

#include <csp-app.h>

G_BEGIN_DECLS

#define CSP_TYPE_APP_MANAGER            (csp_app_manager_get_type ())
#define CSP_APP_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSP_TYPE_APP_MANAGER, CspAppManager))
#define CSP_APP_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CSP_TYPE_APP_MANAGER, CspAppManagerClass))
#define CSP_IS_APP_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSP_TYPE_APP_MANAGER))
#define CSP_IS_APP_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CSP_TYPE_APP_MANAGER))
#define CSP_APP_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CSP_TYPE_APP_MANAGER, CspAppManagerClass))

typedef struct _CspAppManager        CspAppManager;
typedef struct _CspAppManagerClass   CspAppManagerClass;

typedef struct _CspAppManagerPrivate CspAppManagerPrivate;

struct _CspAppManagerClass
{
        GObjectClass parent_class;

        void (* added)   (CspAppManager *manager,
                          CspApp        *app);
        void (* removed) (CspAppManager *manager,
                          CspApp        *app);
};

struct _CspAppManager
{
        GObject parent_instance;

        CspAppManagerPrivate *priv;
};

GType           csp_app_manager_get_type               (void);

CspAppManager  *csp_app_manager_get                    (void);

void            csp_app_manager_fill                   (CspAppManager *manager);

GSList         *csp_app_manager_get_apps               (CspAppManager *manager);

CspApp         *csp_app_manager_find_app_with_basename (CspAppManager *manager,
                                                        const char    *basename);

const char     *csp_app_manager_get_dir                (CspAppManager *manager,
                                                        unsigned int   index);

void            csp_app_manager_add                    (CspAppManager *manager,
                                                        CspApp        *app);

G_END_DECLS

#endif /* __CSP_APP_MANAGER_H */
