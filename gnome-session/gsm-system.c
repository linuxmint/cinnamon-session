/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <glib-object.h>
#include <glib/gi18n.h>

#include "gsm-system.h"
#include "gsm-consolekit.h"
#include "gsm-systemd.h"

enum {
        REQUEST_COMPLETED = 0,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_INTERFACE (GsmSystem, gsm_system, G_TYPE_OBJECT)

static void
gsm_system_default_init (GsmSystemInterface *iface)
{
        signals [REQUEST_COMPLETED] =
                g_signal_new ("request-completed",
                              GSM_TYPE_SYSTEM,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmSystemInterface, request_completed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1, G_TYPE_POINTER);
}

GQuark
gsm_system_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0) {
                error_quark = g_quark_from_static_string ("gsm-system-error");
        }

        return error_quark;
}

gboolean
gsm_system_can_switch_user (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_switch_user (system);
}

gboolean
gsm_system_can_stop (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_stop (system);
}

gboolean
gsm_system_can_restart (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_restart (system);
}

gboolean
gsm_system_can_suspend (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_suspend (system);
}

gboolean
gsm_system_can_hibernate (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->can_hibernate (system);
}

void
gsm_system_attempt_stop (GsmSystem *system)
{
        GSM_SYSTEM_GET_IFACE (system)->attempt_stop (system);
}

void
gsm_system_attempt_restart (GsmSystem *system)
{
        GSM_SYSTEM_GET_IFACE (system)->attempt_restart (system);
}

void
gsm_system_suspend (GsmSystem *system)
{
        GSM_SYSTEM_GET_IFACE (system)->suspend (system);
}

void
gsm_system_hibernate (GsmSystem *system)
{
        GSM_SYSTEM_GET_IFACE (system)->hibernate (system);
}

void
gsm_system_set_session_idle (GsmSystem *system,
                             gboolean   is_idle)
{
        GSM_SYSTEM_GET_IFACE (system)->set_session_idle (system, is_idle);
}

void
gsm_system_add_inhibitor (GsmSystem        *system,
                          const gchar      *id,
                          GsmInhibitorFlag  flag)
{
        GSM_SYSTEM_GET_IFACE (system)->add_inhibitor (system, id, flag);
}

void
gsm_system_remove_inhibitor (GsmSystem   *system,
                             const gchar *id)
{
        GSM_SYSTEM_GET_IFACE (system)->remove_inhibitor (system, id);
}

gboolean
gsm_system_is_login_session (GsmSystem *system)
{
        return GSM_SYSTEM_GET_IFACE (system)->is_login_session (system);
}

GsmSystem *
gsm_get_system (void)
{
        static GsmSystem *system = NULL;

        if (system == NULL) {
                system = GSM_SYSTEM (gsm_systemd_new ());
                if (system != NULL) {
                        g_debug ("Using systemd for session tracking");
                }
        }
        if (system == NULL) {
                system = GSM_SYSTEM (gsm_consolekit_new ());
                if (system != NULL) {
                        g_debug ("Using ConsoleKit for session tracking");
                }
        }

        return g_object_ref (system);
}
