/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Red Hat, Inc.
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

#ifndef __CSM_PRESENCE_H__
#define __CSM_PRESENCE_H__

#include <glib-object.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define CSM_TYPE_PRESENCE            (csm_presence_get_type ())
#define CSM_PRESENCE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_PRESENCE, CsmPresence))
#define CSM_PRESENCE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_PRESENCE, CsmPresenceClass))
#define CSM_IS_PRESENCE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_PRESENCE))
#define CSM_IS_PRESENCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CSM_TYPE_PRESENCE))
#define CSM_PRESENCE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CSM_TYPE_PRESENCE, CsmPresenceClass))

typedef struct _CsmPresence        CsmPresence;
typedef struct _CsmPresenceClass   CsmPresenceClass;

typedef struct CsmPresencePrivate CsmPresencePrivate;

struct _CsmPresence
{
        GObject             parent;
        CsmPresencePrivate *priv;
};

struct _CsmPresenceClass
{
        GObjectClass parent_class;

        void          (* status_changed)        (CsmPresence     *presence,
                                                 guint            status);
        void          (* status_text_changed)   (CsmPresence     *presence,
                                                 const char      *status_text);

};

typedef enum {
        CSM_PRESENCE_STATUS_AVAILABLE = 0,
        CSM_PRESENCE_STATUS_INVISIBLE,
        CSM_PRESENCE_STATUS_BUSY,
        CSM_PRESENCE_STATUS_IDLE,
} CsmPresenceStatus;

typedef enum
{
        CSM_PRESENCE_ERROR_GENERAL = 0,
        CSM_PRESENCE_NUM_ERRORS
} CsmPresenceError;

#define CSM_PRESENCE_ERROR csm_presence_error_quark ()
GType          csm_presence_error_get_type       (void);
#define CSM_PRESENCE_TYPE_ERROR (csm_presence_error_get_type ())

GQuark         csm_presence_error_quark          (void);

GType          csm_presence_get_type             (void) G_GNUC_CONST;

CsmPresence *  csm_presence_new                  (void);

void           csm_presence_set_idle_enabled     (CsmPresence  *presence,
                                                  gboolean      enabled);
void           csm_presence_set_idle_timeout     (CsmPresence  *presence,
                                                  guint         n_seconds);

G_END_DECLS

#endif /* __CSM_PRESENCE_H__ */
