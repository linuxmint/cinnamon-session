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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include "config.h"

#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "csm-system.h"
#include "csm-consolekit.h"
#include "csm-systemd.h"

enum {
        REQUEST_FAILED = 0,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_INTERFACE (CsmSystem, csm_system, G_TYPE_OBJECT)

static void
csm_system_default_init (CsmSystemInterface *iface)
{
        signals [REQUEST_FAILED] =
                g_signal_new ("request-failed",
                              CSM_TYPE_SYSTEM,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmSystemInterface, request_completed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1, G_TYPE_POINTER);
}

GQuark
csm_system_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0) {
                error_quark = g_quark_from_static_string ("csm-system-error");
        }

        return error_quark;
}

gboolean
csm_system_can_switch_user (CsmSystem *system)
{
        return CSM_SYSTEM_GET_IFACE (system)->can_switch_user (system);
}

gboolean
csm_system_can_stop (CsmSystem *system)
{
        return CSM_SYSTEM_GET_IFACE (system)->can_stop (system);
}

gboolean
csm_system_can_restart (CsmSystem *system)
{
        return CSM_SYSTEM_GET_IFACE (system)->can_restart (system);
}

gboolean
csm_system_can_hybrid_sleep (CsmSystem *system)
{
        return CSM_SYSTEM_GET_IFACE (system)->can_hybrid_sleep (system);
}

gboolean
csm_system_can_suspend (CsmSystem *system)
{
        return CSM_SYSTEM_GET_IFACE (system)->can_suspend (system);
}

gboolean
csm_system_can_hibernate (CsmSystem *system)
{
        return CSM_SYSTEM_GET_IFACE (system)->can_hibernate (system);
}

void
csm_system_attempt_stop (CsmSystem *system)
{
        CSM_SYSTEM_GET_IFACE (system)->attempt_stop (system);
}

void
csm_system_attempt_restart (CsmSystem *system)
{
        CSM_SYSTEM_GET_IFACE (system)->attempt_restart (system);
}

void
csm_system_hybrid_sleep (CsmSystem *system)
{
        CSM_SYSTEM_GET_IFACE (system)->hybrid_sleep (system);
}

void
csm_system_suspend (CsmSystem *system)
{
        CSM_SYSTEM_GET_IFACE (system)->suspend (system);
}

void
csm_system_hibernate (CsmSystem *system)
{
        CSM_SYSTEM_GET_IFACE (system)->hibernate (system);
}

void
csm_system_set_session_idle (CsmSystem *system,
                             gboolean   is_idle)
{
        CSM_SYSTEM_GET_IFACE (system)->set_session_idle (system, is_idle);
}

void
csm_system_add_inhibitor (CsmSystem        *system,
                          const gchar      *id,
                          CsmInhibitorFlag  flag)
{
        CSM_SYSTEM_GET_IFACE (system)->add_inhibitor (system, id, flag);
}

void
csm_system_remove_inhibitor (CsmSystem   *system,
                             const gchar *id)
{
        CSM_SYSTEM_GET_IFACE (system)->remove_inhibitor (system, id);
}

gboolean
csm_system_is_login_session (CsmSystem *system)
{
        return CSM_SYSTEM_GET_IFACE (system)->is_login_session (system);
}

gboolean
csm_system_is_last_session_for_user (CsmSystem *system)
{
        return CSM_SYSTEM_GET_IFACE (system)->is_last_session_for_user (system);
}

CsmSystem *
csm_get_system (void)
{
        static CsmSystem *system = NULL;

        if (system == NULL) {
                GSettings *session_settings = g_settings_new ("org.cinnamon.desktop.session");
                if (g_settings_get_boolean (session_settings, "session-manager-uses-logind")) {
                  // Use logind
                  system = CSM_SYSTEM (csm_systemd_new ());
                  if (system != NULL) {
                        g_debug ("Using systemd for session tracking");
                  }
                }
                else {
                  // Use consolekit
                  system = CSM_SYSTEM (csm_consolekit_new ());
                  if (system != NULL) {
                        g_debug ("Using ConsoleKit for session tracking");
                  }
                }
                g_object_unref (session_settings);
        }

        return g_object_ref (system);
}
