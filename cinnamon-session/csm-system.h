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

#ifndef __CSM_SYSTEM_H__
#define __CSM_SYSTEM_H__

#include <glib.h>
#include <glib-object.h>

#include "csm-inhibitor.h"

G_BEGIN_DECLS

#define CSM_TYPE_SYSTEM             (csm_system_get_type ())
#define CSM_SYSTEM(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_SYSTEM, CsmSystem))
#define CSM_SYSTEM_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_SYSTEM, CsmSystemInterface))
#define CSM_IS_SYSTEM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_SYSTEM))
#define CSM_SYSTEM_GET_IFACE(obj)   (G_TYPE_INSTANCE_GET_INTERFACE((obj), CSM_TYPE_SYSTEM, CsmSystemInterface))
#define CSM_SYSTEM_ERROR            (csm_system_error_quark ())

typedef struct _CsmSystem          CsmSystem;
typedef struct _CsmSystemInterface CsmSystemInterface;
typedef enum   _CsmSystemError     CsmSystemError;

struct _CsmSystemInterface
{
        GTypeInterface base_interface;

        void (* request_completed)    (CsmSystem *system,
                                       GError    *error);

        gboolean (* can_switch_user)  (CsmSystem *system);
        gboolean (* can_stop)         (CsmSystem *system);
        gboolean (* can_restart)      (CsmSystem *system);
        gboolean (* can_hybrid_sleep) (CsmSystem *system);
        gboolean (* can_suspend)      (CsmSystem *system);
        gboolean (* can_hibernate)    (CsmSystem *system);
        void     (* attempt_stop)     (CsmSystem *system);
        void     (* attempt_restart)  (CsmSystem *system);
        void     (* hybrid_sleep)     (CsmSystem *system);
        void     (* suspend)          (CsmSystem *system);
        void     (* hibernate)        (CsmSystem *system);
        void     (* set_session_idle) (CsmSystem *system,
                                       gboolean   is_idle);
        gboolean (* is_login_session) (CsmSystem *system);
        void     (* add_inhibitor)    (CsmSystem        *system,
                                       const gchar      *id,
                                       CsmInhibitorFlag  flags);
        void     (* remove_inhibitor) (CsmSystem        *system,
                                       const gchar      *id);
        gboolean (* is_last_session_for_user) (CsmSystem *system);
};

enum _CsmSystemError {
        CSM_SYSTEM_ERROR_RESTARTING = 0,
        CSM_SYSTEM_ERROR_STOPPING
};

GType      csm_system_get_type         (void);

GQuark     csm_system_error_quark      (void);

CsmSystem *csm_get_system              (void);

gboolean   csm_system_can_switch_user  (CsmSystem *system);

gboolean   csm_system_can_stop         (CsmSystem *system);

gboolean   csm_system_can_restart      (CsmSystem *system);

gboolean   csm_system_can_hybrid_sleep (CsmSystem *system);

gboolean   csm_system_can_suspend      (CsmSystem *system);

gboolean   csm_system_can_hibernate    (CsmSystem *system);

void       csm_system_attempt_stop     (CsmSystem *system);

void       csm_system_attempt_restart  (CsmSystem *system);

void       csm_system_hybrid_sleep     (CsmSystem *system);

void       csm_system_suspend          (CsmSystem *system);

void       csm_system_hibernate        (CsmSystem *system);

void       csm_system_set_session_idle (CsmSystem *system,
                                        gboolean   is_idle);

gboolean   csm_system_is_login_session (CsmSystem *system);

gboolean   csm_system_is_last_session_for_user (CsmSystem *system);

void       csm_system_add_inhibitor    (CsmSystem        *system,
                                        const gchar      *id,
                                        CsmInhibitorFlag  flags);

void       csm_system_remove_inhibitor (CsmSystem        *system,
                                        const gchar      *id);

G_END_DECLS

#endif /* __CSM_SYSTEM_H__ */
