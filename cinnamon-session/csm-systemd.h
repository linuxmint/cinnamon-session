/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 *
 * Authors:
 *	Matthias Clasen <mclasen@redhat.com>
 */

#ifndef __CSM_SYSTEMD_H__
#define __CSM_SYSTEMD_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CSM_TYPE_SYSTEMD             (csm_systemd_get_type ())
#define CSM_SYSTEMD(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_SYSTEMD, CsmSystemd))
#define CSM_SYSTEMD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_SYSTEMD, CsmSystemdClass))
#define CSM_IS_SYSTEMD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_SYSTEMD))
#define CSM_IS_SYSTEMD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), CSM_TYPE_SYSTEMD))
#define CSM_SYSTEMD_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), CSM_TYPE_SYSTEMD, CsmSystemdClass))

typedef struct _CsmSystemd        CsmSystemd;
typedef struct _CsmSystemdClass   CsmSystemdClass;
typedef struct _CsmSystemdPrivate CsmSystemdPrivate;

struct _CsmSystemd
{
        GObject            parent;

        CsmSystemdPrivate *priv;
};

struct _CsmSystemdClass
{
        GObjectClass parent_class;
};

GType         csm_systemd_get_type (void);

CsmSystemd   *csm_systemd_new      (void) G_GNUC_MALLOC;

G_END_DECLS

#endif /* __CSM_SYSTEMD_H__ */
