/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
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

#ifndef __CSM_XSMP_CLIENT_H__
#define __CSM_XSMP_CLIENT_H__

#include "csm-client.h"

#include <X11/SM/SMlib.h>

G_BEGIN_DECLS

#define CSM_TYPE_XSMP_CLIENT            (csm_xsmp_client_get_type ())
#define CSM_XSMP_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_XSMP_CLIENT, CsmXSMPClient))
#define CSM_XSMP_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_XSMP_CLIENT, CsmXSMPClientClass))
#define CSM_IS_XSMP_CLIENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_XSMP_CLIENT))
#define CSM_IS_XSMP_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CSM_TYPE_XSMP_CLIENT))
#define CSM_XSMP_CLIENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CSM_TYPE_XSMP_CLIENT, CsmXSMPClientClass))

typedef struct _CsmXSMPClient        CsmXSMPClient;
typedef struct _CsmXSMPClientClass   CsmXSMPClientClass;

typedef struct CsmXSMPClientPrivate  CsmXSMPClientPrivate;

struct _CsmXSMPClient
{
        CsmClient             parent;
        CsmXSMPClientPrivate *priv;
};

struct _CsmXSMPClientClass
{
        CsmClientClass parent_class;

        /* signals */
        gboolean (*register_request)     (CsmXSMPClient  *client,
                                          char          **client_id);
        gboolean (*logout_request)       (CsmXSMPClient  *client,
                                          gboolean        prompt);


        void     (*saved_state)          (CsmXSMPClient  *client);

        void     (*request_phase2)       (CsmXSMPClient  *client);

        void     (*request_interaction)  (CsmXSMPClient  *client);
        void     (*interaction_done)     (CsmXSMPClient  *client,
                                          gboolean        cancel_shutdown);

        void     (*save_yourself_done)   (CsmXSMPClient  *client);

};

GType       csm_xsmp_client_get_type             (void) G_GNUC_CONST;

CsmClient  *csm_xsmp_client_new                  (IceConn         ice_conn);

void        csm_xsmp_client_connect              (CsmXSMPClient  *client,
                                                  SmsConn         conn,
                                                  unsigned long  *mask_ret,
                                                  SmsCallbacks   *callbacks_ret);

void        csm_xsmp_client_save_state           (CsmXSMPClient  *client);
void        csm_xsmp_client_save_yourself        (CsmXSMPClient  *client,
                                                  gboolean        save_state);
void        csm_xsmp_client_save_yourself_phase2 (CsmXSMPClient  *client);
void        csm_xsmp_client_interact             (CsmXSMPClient  *client);
void        csm_xsmp_client_shutdown_cancelled   (CsmXSMPClient  *client);

G_END_DECLS

#endif /* __CSM_XSMP_CLIENT_H__ */
