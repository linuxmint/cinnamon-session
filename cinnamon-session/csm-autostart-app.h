/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#ifndef __CSM_AUTOSTART_APP_H__
#define __CSM_AUTOSTART_APP_H__

#include "csm-app.h"

#include <X11/SM/SMlib.h>

G_BEGIN_DECLS

#define CSM_TYPE_AUTOSTART_APP            (csm_autostart_app_get_type ())
#define CSM_AUTOSTART_APP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_AUTOSTART_APP, CsmAutostartApp))
#define CSM_AUTOSTART_APP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_AUTOSTART_APP, CsmAutostartAppClass))
#define CSM_IS_AUTOSTART_APP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_AUTOSTART_APP))
#define CSM_IS_AUTOSTART_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CSM_TYPE_AUTOSTART_APP))
#define CSM_AUTOSTART_APP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CSM_TYPE_AUTOSTART_APP, CsmAutostartAppClass))

typedef struct _CsmAutostartApp        CsmAutostartApp;
typedef struct _CsmAutostartAppClass   CsmAutostartAppClass;
typedef struct _CsmAutostartAppPrivate CsmAutostartAppPrivate;

struct _CsmAutostartApp
{
        CsmApp parent;

        CsmAutostartAppPrivate *priv;
};

struct _CsmAutostartAppClass
{
        CsmAppClass parent_class;

        /* signals */
        void     (*condition_changed)  (CsmApp  *app,
                                        gboolean condition);
};

GType   csm_autostart_app_get_type           (void) G_GNUC_CONST;

CsmApp *csm_autostart_app_new                (const char *desktop_file);

void    csm_autostart_app_add_provides       (CsmAutostartApp *aapp,
                                              const char      *provides);

#define CSM_AUTOSTART_APP_ENABLED_KEY     "X-GNOME-Autostart-enabled"
#define CSM_AUTOSTART_APP_PHASE_KEY       "X-GNOME-Autostart-Phase"
#define CSM_AUTOSTART_APP_PROVIDES_KEY    "X-GNOME-Provides"
#define CSM_AUTOSTART_APP_STARTUP_ID_KEY  "X-GNOME-Autostart-startup-id"
#define CSM_AUTOSTART_APP_AUTORESTART_KEY "X-GNOME-AutoRestart"
#define CSM_AUTOSTART_APP_DBUS_NAME_KEY   "X-GNOME-DBus-Name"
#define CSM_AUTOSTART_APP_DBUS_PATH_KEY   "X-GNOME-DBus-Path"
#define CSM_AUTOSTART_APP_DBUS_ARGS_KEY   "X-GNOME-DBus-Start-Arguments"
#define CSM_AUTOSTART_APP_DISCARD_KEY     "X-GNOME-Autostart-discard-exec"
#define CSM_AUTOSTART_APP_DELAY_KEY       "X-GNOME-Autostart-Delay"

G_END_DECLS

#endif /* __CSM_AUTOSTART_APP_H__ */
