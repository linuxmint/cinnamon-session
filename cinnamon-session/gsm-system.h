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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *	Jon McCann <jmccann@redhat.com>
 */

#ifndef __GSM_SYSTEM_H__
#define __GSM_SYSTEM_H__

#include <glib.h>
#include <glib-object.h>

#include "gsm-inhibitor.h"

G_BEGIN_DECLS

#define GSM_TYPE_SYSTEM             (gsm_system_get_type ())
#define GSM_SYSTEM(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_SYSTEM, GsmSystem))
#define GSM_SYSTEM_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_SYSTEM, GsmSystemInterface))
#define GSM_IS_SYSTEM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_SYSTEM))
#define GSM_SYSTEM_GET_IFACE(obj)   (G_TYPE_INSTANCE_GET_INTERFACE((obj), GSM_TYPE_SYSTEM, GsmSystemInterface))
#define GSM_SYSTEM_ERROR            (gsm_system_error_quark ())

typedef struct _GsmSystem          GsmSystem;
typedef struct _GsmSystemInterface GsmSystemInterface;
typedef enum   _GsmSystemError     GsmSystemError;

struct _GsmSystemInterface
{
        GTypeInterface base_interface;

        void (* request_completed)    (GsmSystem *system,
                                       GError    *error);

        gboolean (* can_switch_user)  (GsmSystem *system);
        gboolean (* can_stop)         (GsmSystem *system);
        gboolean (* can_restart)      (GsmSystem *system);
        gboolean (* can_suspend)      (GsmSystem *system);
        gboolean (* can_hibernate)    (GsmSystem *system);
        void     (* attempt_stop)     (GsmSystem *system);
        void     (* attempt_restart)  (GsmSystem *system);
        void     (* suspend)          (GsmSystem *system);
        void     (* hibernate)        (GsmSystem *system);
        void     (* set_session_idle) (GsmSystem *system,
                                       gboolean   is_idle);
        gboolean (* is_login_session) (GsmSystem *system);
        void     (* add_inhibitor)    (GsmSystem        *system,
                                       const gchar      *id,
                                       GsmInhibitorFlag  flags);
        void     (* remove_inhibitor) (GsmSystem        *system,
                                       const gchar      *id);
};

enum _GsmSystemError {
        GSM_SYSTEM_ERROR_RESTARTING = 0,
        GSM_SYSTEM_ERROR_STOPPING
};

GType      gsm_system_get_type         (void);

GQuark     gsm_system_error_quark      (void);

GsmSystem *gsm_get_system              (void);

gboolean   gsm_system_can_switch_user  (GsmSystem *system);

gboolean   gsm_system_can_stop         (GsmSystem *system);

gboolean   gsm_system_can_restart      (GsmSystem *system);

gboolean   gsm_system_can_suspend      (GsmSystem *system);

gboolean   gsm_system_can_hibernate    (GsmSystem *system);

void       gsm_system_attempt_stop     (GsmSystem *system);

void       gsm_system_attempt_restart  (GsmSystem *system);

void       gsm_system_suspend          (GsmSystem *system);

void       gsm_system_hibernate        (GsmSystem *system);

void       gsm_system_set_session_idle (GsmSystem *system,
                                        gboolean   is_idle);

gboolean   gsm_system_is_login_session (GsmSystem *system);

void       gsm_system_add_inhibitor    (GsmSystem        *system,
                                        const gchar      *id,
                                        GsmInhibitorFlag  flags);

void       gsm_system_remove_inhibitor (GsmSystem        *system,
                                        const gchar      *id);

G_END_DECLS

#endif /* __GSM_SYSTEM_H__ */
