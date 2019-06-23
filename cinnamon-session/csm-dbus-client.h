/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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

#ifndef __CSM_DBUS_CLIENT_H__
#define __CSM_DBUS_CLIENT_H__

#include "csm-client.h"

G_BEGIN_DECLS

#define CSM_TYPE_DBUS_CLIENT            (csm_dbus_client_get_type ())
#define CSM_DBUS_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_DBUS_CLIENT, CsmDBusClient))
#define CSM_DBUS_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_DBUS_CLIENT, CsmDBusClientClass))
#define CSM_IS_DBUS_CLIENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_DBUS_CLIENT))
#define CSM_IS_DBUS_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CSM_TYPE_DBUS_CLIENT))
#define CSM_DBUS_CLIENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CSM_TYPE_DBUS_CLIENT, CsmDBusClientClass))

typedef struct _CsmDBusClient        CsmDBusClient;
typedef struct _CsmDBusClientClass   CsmDBusClientClass;

typedef struct CsmDBusClientPrivate  CsmDBusClientPrivate;

struct _CsmDBusClient
{
        CsmClient             parent;
        CsmDBusClientPrivate *priv;
};

struct _CsmDBusClientClass
{
        CsmClientClass parent_class;
};

GType          csm_dbus_client_get_type           (void) G_GNUC_CONST;

CsmClient *    csm_dbus_client_new                (const char     *startup_id,
                                                   const char     *bus_name);
const char *   csm_dbus_client_get_bus_name       (CsmDBusClient  *client);

G_END_DECLS

#endif /* __CSM_DBUS_CLIENT_H__ */
