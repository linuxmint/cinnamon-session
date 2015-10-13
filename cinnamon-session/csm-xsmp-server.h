/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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


#ifndef __CSM_XSMP_SERVER_H
#define __CSM_XSMP_SERVER_H

#include <glib-object.h>

#include "csm-store.h"

G_BEGIN_DECLS

#define CSM_TYPE_XSMP_SERVER         (csm_xsmp_server_get_type ())
#define CSM_XSMP_SERVER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CSM_TYPE_XSMP_SERVER, CsmXsmpServer))
#define CSM_XSMP_SERVER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CSM_TYPE_XSMP_SERVER, CsmXsmpServerClass))
#define CSM_IS_XSMP_SERVER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CSM_TYPE_XSMP_SERVER))
#define CSM_IS_XSMP_SERVER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CSM_TYPE_XSMP_SERVER))
#define CSM_XSMP_SERVER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CSM_TYPE_XSMP_SERVER, CsmXsmpServerClass))

typedef struct CsmXsmpServerPrivate CsmXsmpServerPrivate;

typedef struct
{
        GObject            parent;
        CsmXsmpServerPrivate *priv;
} CsmXsmpServer;

typedef struct
{
        GObjectClass   parent_class;
} CsmXsmpServerClass;

GType               csm_xsmp_server_get_type                       (void);

CsmXsmpServer *     csm_xsmp_server_new                            (CsmStore      *client_store);
void                csm_xsmp_server_start                          (CsmXsmpServer *server);
void                csm_xsmp_server_stop_accepting_new_clients     (CsmXsmpServer *server);
void                csm_xsmp_server_start_accepting_new_clients    (CsmXsmpServer *server);

G_END_DECLS

#endif /* __CSM_XSMP_SERVER_H */
