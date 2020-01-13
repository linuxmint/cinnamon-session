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

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cs-idle-monitor.h"

#include "csm-presence.h"
#include "csm-exported-presence.h"

#define CSM_PRESENCE_DBUS_PATH "/org/gnome/SessionManager/Presence"

#define CS_NAME      "org.cinnamon.ScreenSaver"
#define CS_PATH      "/org/cinnamon/ScreenSaver"
#define CS_INTERFACE "org.cinnamon.ScreenSaver"

#define DBUS_NAME      "org.freedesktop.DBus"
#define DBUS_PATH      "/org/freedesktop/DBus"
#define DBUS_INTERFACE "org.freedesktop.DBus"


#define MAX_STATUS_TEXT 140

#define CSM_PRESENCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_PRESENCE, CsmPresencePrivate))

struct CsmPresencePrivate
{
        guint            status;
        guint            saved_status;
        char            *status_text;
        gboolean         idle_enabled;
        CSIdleMonitor   *idle_monitor;
        guint            idle_watch_id;
        guint            idle_timeout;
        gboolean         screensaver_active;
        GDBusConnection *connection;
        GDBusProxy      *screensaver_proxy;
        CsmExportedPresence *skeleton;
};

enum {
        PROP_0,
        PROP_STATUS,
        PROP_STATUS_TEXT,
        PROP_IDLE_ENABLED,
        PROP_IDLE_TIMEOUT,
};


enum {
        STATUS_CHANGED,
        STATUS_TEXT_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (CsmPresence, csm_presence, G_TYPE_OBJECT)

#define CSM_PRESENCE_DBUS_IFACE "org.gnome.SessionManager.Presence"

static const GDBusErrorEntry csm_presence_error_entries[] = {
        { CSM_PRESENCE_ERROR_GENERAL, CSM_PRESENCE_DBUS_IFACE ".GeneralError" }
};

GQuark
csm_presence_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("csm_presence_error",
                                            &quark_volatile,
                                            csm_presence_error_entries,
                                            G_N_ELEMENTS (csm_presence_error_entries));
        return quark_volatile;
}

static gboolean
csm_presence_set_status_text (CsmPresence  *presence,
                              const char   *status_text,
                              GError      **error)
{
        g_return_val_if_fail (CSM_IS_PRESENCE (presence), FALSE);

        g_free (presence->priv->status_text);
        presence->priv->status_text = NULL;

        /* check length */
        if (status_text != NULL && strlen (status_text) > MAX_STATUS_TEXT) {
                g_set_error (error,
                             CSM_PRESENCE_ERROR,
                             CSM_PRESENCE_ERROR_GENERAL,
                             "Status text too long");
                return FALSE;
        }

        if (status_text != NULL) {
                presence->priv->status_text = g_strdup (status_text);
        }

        csm_exported_presence_set_status_text (presence->priv->skeleton, presence->priv->status_text);
        csm_exported_presence_emit_status_text_changed (presence->priv->skeleton, presence->priv->status_text);

        return TRUE;
}

static gboolean
csm_presence_set_status (CsmPresence  *presence,
                         guint         status)
{
        g_return_val_if_fail (CSM_IS_PRESENCE (presence), FALSE);

        if (status != presence->priv->status) {
                presence->priv->status = status;
                csm_exported_presence_set_status (presence->priv->skeleton, status);
                csm_exported_presence_emit_status_changed (presence->priv->skeleton, presence->priv->status);
                g_signal_emit (presence, signals[STATUS_CHANGED], 0, presence->priv->status);
        }
        return TRUE;
}

static void
set_session_idle (CsmPresence   *presence,
                  gboolean       is_idle)
{
        g_debug ("CsmPresence: setting idle: %d", is_idle);

        if (is_idle) {
                if (presence->priv->status == CSM_PRESENCE_STATUS_IDLE) {
                        g_debug ("CsmPresence: already idle, ignoring");
                        return;
                }

                /* save current status */
                presence->priv->saved_status = presence->priv->status;
                csm_presence_set_status (presence, CSM_PRESENCE_STATUS_IDLE);
        } else {
                if (presence->priv->status != CSM_PRESENCE_STATUS_IDLE) {
                        g_debug ("CsmPresence: already not idle, ignoring");
                        return;
                }

                /* restore saved status */
                csm_presence_set_status (presence, presence->priv->saved_status);
                g_debug ("CsmPresence: setting non-idle status %d", presence->priv->saved_status);
                presence->priv->saved_status = CSM_PRESENCE_STATUS_AVAILABLE;
        }
}

static gboolean
on_idle_timeout (CSIdleMonitor *monitor,
                 guint          id,
                 gboolean       condition,
                 CsmPresence   *presence)
{
        gboolean handled;

        handled = TRUE;
        set_session_idle (presence, condition);

        return handled;
}

static void
reset_idle_watch (CsmPresence  *presence)
{
        if (presence->priv->idle_monitor == NULL) {
                return;
        }

        if (presence->priv->idle_watch_id > 0) {
                g_debug ("CsmPresence: removing idle watch (%i)", presence->priv->idle_watch_id);
                cs_idle_monitor_remove_watch (presence->priv->idle_monitor,
                                              presence->priv->idle_watch_id);
                presence->priv->idle_watch_id = 0;
        }

        if (! presence->priv->screensaver_active
            && presence->priv->idle_enabled
            && presence->priv->idle_timeout > 0) {
                presence->priv->idle_watch_id = cs_idle_monitor_add_watch (presence->priv->idle_monitor,
                                                                           presence->priv->idle_timeout,
                                                                           (CSIdleMonitorWatchFunc)on_idle_timeout,
                                                                           presence);
                g_debug ("CsmPresence: adding idle watch (%i) for %d secs",
                         presence->priv->idle_watch_id,
                         presence->priv->idle_timeout / 1000);
        }
}

static void
on_screensaver_g_signal (GDBusProxy  *proxy,
                         gchar       *sender_name,
                         gchar       *signal_name,
                         GVariant    *parameters,
                         CsmPresence *presence)
{
        GError *error;
        gboolean is_active;

        if (signal_name == NULL || g_strcmp0 (signal_name, "ActiveChanged") != 0) {
                return;
        }

        g_variant_get (parameters,
                       "(b)", &is_active);

        g_debug ("screensaver status changed: %d", is_active);

        if (presence->priv->screensaver_active != is_active) {
                presence->priv->screensaver_active = is_active;
                reset_idle_watch (presence);
                set_session_idle (presence, is_active);
        }
}

static void
on_screensaver_name_owner_changed (GDBusProxy *proxy,
                                   GParamSpec *pspec,
                                   gpointer    user_data)
{
        CsmPresence *presence;
        gchar *name_owner;

        presence = CSM_PRESENCE (user_data);
        name_owner = g_dbus_proxy_get_name_owner (proxy);

        if (name_owner && g_strcmp0 (name_owner, CS_NAME)) {
                g_warning ("Detected that screensaver has appeared on the bus");
        } else {
                g_warning ("Detected that screensaver has left the bus");
                set_session_idle (presence, FALSE);
                reset_idle_watch (presence);
        }
}

static gboolean
csm_presence_set_status_text_dbus (CsmExportedPresence   *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   gchar                 *status_text,
                                   CsmPresence           *presence)
{
        GError *error = NULL;

        if (csm_presence_set_status_text (presence, status_text, &error)) {
                csm_exported_presence_complete_set_status_text (skeleton, invocation);
        } else {
                g_dbus_method_invocation_take_error (invocation, error);
        }

        return TRUE;
}

static gboolean
csm_presence_set_status_dbus (CsmExportedPresence   *skeleton,
                              GDBusMethodInvocation *invocation,
                              guint                  status,
                              CsmPresence           *presence)
{
        csm_presence_set_status (presence, status);
        csm_exported_presence_complete_set_status (skeleton, invocation);
        return TRUE;
}

static gboolean
register_presence (CsmPresence *presence)
{
        CsmExportedPresence *skeleton;
        GError *error;

        error = NULL;
        presence->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (presence->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        skeleton = csm_exported_presence_skeleton_new ();
        presence->priv->skeleton = skeleton;

        g_debug ("exporting presence proxy");

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          presence->priv->connection,
                                          CSM_PRESENCE_DBUS_PATH,
                                          &error);

        if (error != NULL) {
                g_critical ("error exporting presence: %s", error->message);
                g_error_free (error);

                return FALSE;
        }

        g_signal_connect (skeleton,
                          "handle-set-status",
                          G_CALLBACK (csm_presence_set_status_dbus),
                          presence);

        g_signal_connect (skeleton,
                          "handle-set-status-text",
                          G_CALLBACK (csm_presence_set_status_text_dbus),
                          presence);

        return TRUE;
}

static GObject *
csm_presence_constructor (GType                  type,
                          guint                  n_construct_properties,
                          GObjectConstructParam *construct_properties)
{
        CsmPresence *presence;
        GError *error;
        gboolean res;


        presence = CSM_PRESENCE (G_OBJECT_CLASS (csm_presence_parent_class)->constructor (type,
                                                                                             n_construct_properties,
                                                                                             construct_properties));

        res = register_presence (presence);
        if (! res) {
                g_warning ("Unable to register presence with session bus");
        }

        presence->priv->screensaver_proxy = g_dbus_proxy_new_sync (presence->priv->connection,
                                                                   G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                                   NULL,
                                                                   CS_NAME,
                                                                   CS_PATH,
                                                                   CS_INTERFACE,
                                                                   NULL,
                                                                   &error);

        if (presence->priv->screensaver_proxy != NULL) {
                g_signal_connect (presence->priv->screensaver_proxy,
                                  "g-signal",
                                  G_CALLBACK (on_screensaver_g_signal),
                                  presence);

                g_signal_connect (presence->priv->screensaver_proxy,
                                  "notify::g-name-owner",
                                  G_CALLBACK (on_screensaver_name_owner_changed),
                                  presence);
        } else {
                g_warning ("Unable to get screensaver proxy: %s", error->message);
                g_error_free (error);
        }

        return G_OBJECT (presence);
}

static void
csm_presence_init (CsmPresence *presence)
{
        presence->priv = CSM_PRESENCE_GET_PRIVATE (presence);

        presence->priv->idle_monitor = cs_idle_monitor_new ();
}

void
csm_presence_set_idle_enabled (CsmPresence  *presence,
                               gboolean      enabled)
{
        g_return_if_fail (CSM_IS_PRESENCE (presence));

        if (presence->priv->idle_enabled != enabled) {
                presence->priv->idle_enabled = enabled;
                reset_idle_watch (presence);
                g_object_notify (G_OBJECT (presence), "idle-enabled");

        }
}

void
csm_presence_set_idle_timeout (CsmPresence  *presence,
                               guint         timeout)
{
        g_return_if_fail (CSM_IS_PRESENCE (presence));

        if (timeout != presence->priv->idle_timeout) {
                presence->priv->idle_timeout = timeout;
                reset_idle_watch (presence);
                g_object_notify (G_OBJECT (presence), "idle-timeout");
        }
}

static void
csm_presence_set_property (GObject       *object,
                           guint          prop_id,
                           const GValue  *value,
                           GParamSpec    *pspec)
{
        CsmPresence *self;

        self = CSM_PRESENCE (object);

        switch (prop_id) {
        case PROP_STATUS:
                csm_presence_set_status (self, g_value_get_uint (value));
                break;
        case PROP_STATUS_TEXT:
                csm_presence_set_status_text (self, g_value_get_string (value), NULL);
                break;
        case PROP_IDLE_ENABLED:
                csm_presence_set_idle_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_IDLE_TIMEOUT:
                csm_presence_set_idle_timeout (self, g_value_get_uint (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_presence_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
        CsmPresence *self;

        self = CSM_PRESENCE (object);

        switch (prop_id) {
        case PROP_STATUS:
                g_value_set_uint (value, self->priv->status);
                break;
        case PROP_STATUS_TEXT:
                g_value_set_string (value, self->priv->status_text ? self->priv->status_text : "");
                break;
        case PROP_IDLE_ENABLED:
                g_value_set_boolean (value, self->priv->idle_enabled);
                break;
        case PROP_IDLE_TIMEOUT:
                g_value_set_uint (value, self->priv->idle_timeout);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_presence_finalize (GObject *object)
{
        CsmPresence *presence = (CsmPresence *) object;

        if (presence->priv->idle_watch_id > 0) {
                cs_idle_monitor_remove_watch (presence->priv->idle_monitor,
                                              presence->priv->idle_watch_id);
                presence->priv->idle_watch_id = 0;
        }

        if (presence->priv->status_text != NULL) {
                g_free (presence->priv->status_text);
                presence->priv->status_text = NULL;
        }

        if (presence->priv->idle_monitor != NULL) {
                g_object_unref (presence->priv->idle_monitor);
                presence->priv->idle_monitor = NULL;
        }

        G_OBJECT_CLASS (csm_presence_parent_class)->finalize (object);
}

static void
csm_presence_class_init (CsmPresenceClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize             = csm_presence_finalize;
        object_class->constructor          = csm_presence_constructor;
        object_class->get_property         = csm_presence_get_property;
        object_class->set_property         = csm_presence_set_property;

        signals [STATUS_CHANGED] =
                g_signal_new ("status-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmPresenceClass, status_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__UINT,
                              G_TYPE_NONE,
                              1, G_TYPE_UINT);

        g_object_class_install_property (object_class,
                                         PROP_IDLE_ENABLED,
                                         g_param_spec_boolean ("idle-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_IDLE_TIMEOUT,
                                         g_param_spec_uint ("idle-timeout",
                                                            "idle timeout",
                                                            "idle timeout",
                                                            0,
                                                            G_MAXINT,
                                                            300000,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (CsmPresencePrivate));
}

CsmPresence *
csm_presence_new (void)
{
        CsmPresence *presence;

        presence = g_object_new (CSM_TYPE_PRESENCE,
                                 NULL);

        return presence;
}
