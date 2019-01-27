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

#ifndef __CSM_APP_H__
#define __CSM_APP_H__

#include <glib-object.h>
#include <sys/types.h>

#include "eggdesktopfile.h"

#include "csm-manager.h"
#include "csm-client.h"

G_BEGIN_DECLS

#define CSM_TYPE_APP            (csm_app_get_type ())
#define CSM_APP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_APP, CsmApp))
#define CSM_APP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_APP, CsmAppClass))
#define CSM_IS_APP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_APP))
#define CSM_IS_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CSM_TYPE_APP))
#define CSM_APP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CSM_TYPE_APP, CsmAppClass))

typedef struct _CsmApp        CsmApp;
typedef struct _CsmAppClass   CsmAppClass;
typedef struct _CsmAppPrivate CsmAppPrivate;

struct _CsmApp
{
        GObject        parent;
        CsmAppPrivate *priv;
};

struct _CsmAppClass
{
        GObjectClass parent_class;

        /* signals */
        void        (*exited)       (CsmApp *app,
                                     guchar  exit_code);
        void        (*died)         (CsmApp *app,
                                     int     signal);
        void        (*registered)   (CsmApp *app);

        /* virtual methods */
        gboolean    (*impl_start)                     (CsmApp     *app,
                                                       GError    **error);
        gboolean    (*impl_restart)                   (CsmApp     *app,
                                                       GError    **error);
        gboolean    (*impl_stop)                      (CsmApp     *app,
                                                       GError    **error);
        int         (*impl_peek_autostart_delay)      (CsmApp     *app);
        gboolean    (*impl_provides)                  (CsmApp     *app,
                                                       const char *service);
        char **     (*impl_get_provides)              (CsmApp     *app);
        gboolean    (*impl_has_autostart_condition)   (CsmApp     *app,
                                                       const char *service);
        gboolean    (*impl_is_running)                (CsmApp     *app);

        gboolean    (*impl_get_autorestart)           (CsmApp     *app);
        const char *(*impl_get_app_id)                (CsmApp     *app);
        gboolean    (*impl_is_disabled)               (CsmApp     *app);
        gboolean    (*impl_is_conditionally_disabled) (CsmApp     *app);
};

typedef enum
{
        CSM_APP_ERROR_GENERAL = 0,
        CSM_APP_ERROR_RESTART_LIMIT,
        CSM_APP_ERROR_START,
        CSM_APP_ERROR_STOP,
        CSM_APP_NUM_ERRORS
} CsmAppError;

#define CSM_APP_ERROR csm_app_error_quark ()

GQuark           csm_app_error_quark                    (void);
GType            csm_app_get_type                       (void) G_GNUC_CONST;

gboolean         csm_app_peek_autorestart               (CsmApp     *app);

const char      *csm_app_peek_id                        (CsmApp     *app);
const char      *csm_app_peek_app_id                    (CsmApp     *app);
const char      *csm_app_peek_startup_id                (CsmApp     *app);
CsmManagerPhase  csm_app_peek_phase                     (CsmApp     *app);
gboolean         csm_app_peek_is_disabled               (CsmApp     *app);
gboolean         csm_app_peek_is_conditionally_disabled (CsmApp     *app);

gboolean         csm_app_start                          (CsmApp     *app,
                                                         GError    **error);
gboolean         csm_app_restart                        (CsmApp     *app,
                                                         GError    **error);
gboolean         csm_app_stop                           (CsmApp     *app,
                                                         GError    **error);
gboolean         csm_app_is_running                     (CsmApp     *app);

void             csm_app_exited                         (CsmApp     *app,
                                                         guchar      exit_code);
void             csm_app_died                           (CsmApp     *app,
                                                         int         signal);

gboolean         csm_app_provides                       (CsmApp     *app,
                                                         const char *service);
char           **csm_app_get_provides                   (CsmApp     *app);
gboolean         csm_app_has_autostart_condition        (CsmApp     *app,
                                                         const char *condition);
void             csm_app_registered                     (CsmApp     *app);
int              csm_app_peek_autostart_delay           (CsmApp     *app);

G_END_DECLS

#endif /* __CSM_APP_H__ */
