/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Jon McCann <jmccann@redhat.com>
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
 *	Jon McCann <jmccann@redhat.com>
 */

#ifndef __CSM_CONSOLEKIT_H__
#define __CSM_CONSOLEKIT_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CSM_TYPE_CONSOLEKIT             (csm_consolekit_get_type ())
#define CSM_CONSOLEKIT(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_CONSOLEKIT, CsmConsolekit))
#define CSM_CONSOLEKIT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_CONSOLEKIT, CsmConsolekitClass))
#define CSM_IS_CONSOLEKIT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_CONSOLEKIT))
#define CSM_IS_CONSOLEKIT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), CSM_TYPE_CONSOLEKIT))
#define CSM_CONSOLEKIT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), CSM_TYPE_CONSOLEKIT, CsmConsolekitClass))

typedef struct _CsmConsolekit        CsmConsolekit;
typedef struct _CsmConsolekitClass   CsmConsolekitClass;
typedef struct _CsmConsolekitPrivate CsmConsolekitPrivate;

struct _CsmConsolekit
{
        GObject               parent;

        CsmConsolekitPrivate *priv;
};

struct _CsmConsolekitClass
{
        GObjectClass parent_class;
};

GType            csm_consolekit_get_type        (void);

CsmConsolekit   *csm_consolekit_new             (void) G_GNUC_MALLOC;

G_END_DECLS

#endif /* __CSM_CONSOLEKIT_H__ */
