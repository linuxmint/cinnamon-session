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

#ifndef __CSM_CLIENT_H__
#define __CSM_CLIENT_H__

#include <glib.h>
#include <glib-object.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define CSM_TYPE_CLIENT            (csm_client_get_type ())
#define CSM_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_CLIENT, CsmClient))
#define CSM_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_CLIENT, CsmClientClass))
#define CSM_IS_CLIENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_CLIENT))
#define CSM_IS_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CSM_TYPE_CLIENT))
#define CSM_CLIENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CSM_TYPE_CLIENT, CsmClientClass))

typedef struct _CsmClient        CsmClient;
typedef struct _CsmClientClass   CsmClientClass;

typedef struct CsmClientPrivate CsmClientPrivate;

typedef enum {
        CSM_CLIENT_UNREGISTERED = 0,
        CSM_CLIENT_REGISTERED,
        CSM_CLIENT_FINISHED,
        CSM_CLIENT_FAILED
} CsmClientStatus;

typedef enum {
        CSM_CLIENT_RESTART_NEVER = 0,
        CSM_CLIENT_RESTART_IF_RUNNING,
        CSM_CLIENT_RESTART_ANYWAY,
        CSM_CLIENT_RESTART_IMMEDIATELY
} CsmClientRestartStyle;

typedef enum {
        CSM_CLIENT_END_SESSION_FLAG_FORCEFUL = 1 << 0,
        CSM_CLIENT_END_SESSION_FLAG_SAVE     = 1 << 1,
        CSM_CLIENT_END_SESSION_FLAG_LAST     = 1 << 2
} CsmClientEndSessionFlag;

struct _CsmClient
{
        GObject           parent;
        CsmClientPrivate *priv;
};

struct _CsmClientClass
{
        GObjectClass parent_class;

        /* signals */
        void         (*disconnected)               (CsmClient  *client);
        void         (*end_session_response)       (CsmClient  *client,
                                                    gboolean    ok,
                                                    gboolean    do_last,
                                                    gboolean    cancel,
                                                    const char *reason);

        /* virtual methods */
        char *                (*impl_get_app_name)           (CsmClient *client);
        CsmClientRestartStyle (*impl_get_restart_style_hint) (CsmClient *client);
        guint                 (*impl_get_unix_process_id)    (CsmClient *client);
        gboolean              (*impl_query_end_session)      (CsmClient *client,
                                                              guint      flags,
                                                              GError   **error);
        gboolean              (*impl_end_session)            (CsmClient *client,
                                                              guint      flags,
                                                              GError   **error);
        gboolean              (*impl_cancel_end_session)     (CsmClient *client,
                                                              GError   **error);
        gboolean              (*impl_stop)                   (CsmClient *client,
                                                              GError   **error);
        GKeyFile *            (*impl_save)                   (CsmClient *client,
                                                              GError   **error);
};

typedef enum
{
        CSM_CLIENT_ERROR_GENERAL = 0,
        CSM_CLIENT_ERROR_NOT_REGISTERED,
        CSM_CLIENT_NUM_ERRORS
} CsmClientError;

#define CSM_CLIENT_ERROR csm_client_error_quark ()

GQuark                csm_client_error_quark                (void);

GType                 csm_client_get_type                   (void) G_GNUC_CONST;

const char           *csm_client_peek_id                    (CsmClient  *client);


const char *          csm_client_peek_startup_id            (CsmClient  *client);
const char *          csm_client_peek_app_id                (CsmClient  *client);
guint                 csm_client_peek_restart_style_hint    (CsmClient  *client);
guint                 csm_client_peek_status                (CsmClient  *client);


char                 *csm_client_get_app_name               (CsmClient  *client);
void                  csm_client_set_app_id                 (CsmClient  *client,
                                                             const char *app_id);
void                  csm_client_set_status                 (CsmClient  *client,
                                                             guint       status);

gboolean              csm_client_end_session                (CsmClient  *client,
                                                             guint       flags,
                                                             GError    **error);
gboolean              csm_client_query_end_session          (CsmClient  *client,
                                                             guint       flags,
                                                             GError    **error);
gboolean              csm_client_cancel_end_session         (CsmClient  *client,
                                                             GError    **error);

void                  csm_client_disconnected               (CsmClient  *client);

GKeyFile             *csm_client_save                       (CsmClient  *client,
                                                             GError    **error);
gboolean              csm_client_stop                       (CsmClient  *client,
                                                             GError    **error);

/* private */

void                  csm_client_end_session_response       (CsmClient  *client,
                                                             gboolean    is_ok,
                                                             gboolean    do_last,
                                                             gboolean    cancel,
                                                             const char *reason);

G_END_DECLS

#endif /* __CSM_CLIENT_H__ */
