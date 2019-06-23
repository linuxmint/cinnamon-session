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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <gio/gio.h>

#include "csm-exported-client-private.h"
#include "csm-dbus-client.h"

#include "csm-manager.h"
#include "csm-util.h"

#define CSM_DBUS_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_DBUS_CLIENT, CsmDBusClientPrivate))


#define SM_DBUS_NAME                     "org.gnome.SessionManager"
#define SM_DBUS_CLIENT_PRIVATE_INTERFACE "org.gnome.SessionManager.ClientPrivate"

struct CsmDBusClientPrivate
{
        char                 *bus_name;
        GPid                  caller_pid;
        CsmClientRestartStyle restart_style_hint;

        GDBusConnection      *connection;
        CsmExportedClientPrivate *skeleton;
        guint                 watch_id;
};

enum {
        PROP_0,
        PROP_BUS_NAME
};

G_DEFINE_TYPE (CsmDBusClient, csm_dbus_client, CSM_TYPE_CLIENT)

static gboolean
setup_connection (CsmDBusClient *client)
{
        GError *error = NULL;

        if (client->priv->connection == NULL) {
                client->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
                if (error != NULL) {
                        g_debug ("CsmDbusClient: Couldn't connect to session bus: %s",
                                 error->message);
                        g_error_free (error);
                        return FALSE;
                }
        }

        return TRUE;
}

static gboolean
handle_end_session_response (CsmExportedClientPrivate *skeleton,
                             GDBusMethodInvocation    *invocation,
                             gboolean                  is_ok,
                             const char               *reason,
                             CsmDBusClient            *client)
{
        g_debug ("CsmDBusClient: got EndSessionResponse is-ok:%d reason=%s", is_ok, reason);
        csm_client_end_session_response (CSM_CLIENT (client),
                                         is_ok, FALSE, FALSE, reason);

        csm_exported_client_private_complete_end_session_response (skeleton, invocation);
        return TRUE;
}

static GObject *
csm_dbus_client_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
        CsmDBusClient *client;
        GError *error = NULL;
        CsmExportedClientPrivate *skeleton;

        client = CSM_DBUS_CLIENT (G_OBJECT_CLASS (csm_dbus_client_parent_class)->constructor (type,
                                                                                              n_construct_properties,
                                                                                              construct_properties));

        if (! setup_connection (client)) {
                g_object_unref (client);
                return NULL;
        }

        skeleton = csm_exported_client_private_skeleton_new ();
        client->priv->skeleton = skeleton;
        g_debug ("exporting dbus client to object path: %s", csm_client_peek_id (CSM_CLIENT (client)));
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          client->priv->connection,
                                          csm_client_peek_id (CSM_CLIENT (client)),
                                          &error);

        if (error != NULL) {
                g_critical ("error exporting client private on session bus: %s", error->message);
                g_error_free (error);
                g_object_unref (client);
                return NULL;
        }

        g_signal_connect (skeleton, "handle-end-session-response",
                          G_CALLBACK (handle_end_session_response), client);

        return G_OBJECT (client);
}

static void
csm_dbus_client_init (CsmDBusClient *client)
{
        client->priv = CSM_DBUS_CLIENT_GET_PRIVATE (client);
}

/* adapted from PolicyKit */
static gboolean
get_caller_info (CsmDBusClient *client,
                 const char    *sender,
                 uid_t         *calling_uid_out,
                 pid_t         *calling_pid_out)
{
        GDBusConnection *connection;
        gboolean         retval;
        GError          *error;
        GVariant        *uid_variant, *pid_variant;
        uid_t            uid;
        pid_t            pid;

        retval = FALSE;
        connection = NULL;
        uid_variant = pid_variant = NULL;

        if (sender == NULL) {
                goto out;
        }

        error = NULL;
        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (error != NULL) {
                g_warning ("error getting session bus: %s", error->message);
                g_error_free (error);
                goto out;
        }

        uid_variant = g_dbus_connection_call_sync (connection,
                                                   "org.freedesktop.DBus",
                                                   "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus",
                                                   "GetConnectionUnixUser",
                                                   g_variant_new ("(s)", sender),
                                                   G_VARIANT_TYPE ("(u)"),
                                                   G_DBUS_CALL_FLAGS_NONE,
                                                   -1, NULL, &error);

        if (error != NULL) {
                g_debug ("GetConnectionUnixUser() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        pid_variant = g_dbus_connection_call_sync (connection,
                                                   "org.freedesktop.DBus",
                                                   "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus",
                                                   "GetConnectionUnixProcessID",
                                                   g_variant_new ("(s)", sender),
                                                   G_VARIANT_TYPE ("(u)"),
                                                   G_DBUS_CALL_FLAGS_NONE,
                                                   -1, NULL, &error);

        if (error != NULL) {
                g_debug ("GetConnectionUnixProcessID() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        g_variant_get (uid_variant, "(u)", &uid);
        g_variant_get (pid_variant, "(u)", &pid);

        if (calling_uid_out != NULL) {
                *calling_uid_out = uid;
        }
        if (calling_pid_out != NULL) {
                *calling_pid_out = pid;
        }

        retval = TRUE;

        g_debug ("uid = %d", uid);
        g_debug ("pid = %d", pid);

out:
        g_clear_pointer (&uid_variant, (GDestroyNotify) g_variant_unref);
        g_clear_pointer (&pid_variant, (GDestroyNotify) g_variant_unref);
        g_clear_object (&connection);

        return retval;
}

static void
on_client_vanished (GDBusConnection *connection,
                    const char      *name,
                    gpointer         user_data)
{
        CsmDBusClient  *client = user_data;

        g_bus_unwatch_name (client->priv->watch_id);
        client->priv->watch_id = 0;

        csm_client_disconnected (CSM_CLIENT (client));
}

static void
csm_dbus_client_set_bus_name (CsmDBusClient  *client,
                              const char     *bus_name)
{
        g_return_if_fail (CSM_IS_DBUS_CLIENT (client));

        g_free (client->priv->bus_name);

        client->priv->bus_name = g_strdup (bus_name);
        g_object_notify (G_OBJECT (client), "bus-name");

        if (!get_caller_info (client, bus_name, NULL, &client->priv->caller_pid)) {
                client->priv->caller_pid = 0;
        }

        client->priv->watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                                   bus_name,
                                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                   NULL,
                                                   on_client_vanished,
                                                   client,
                                                   NULL);
}

const char *
csm_dbus_client_get_bus_name (CsmDBusClient  *client)
{
        g_return_val_if_fail (CSM_IS_DBUS_CLIENT (client), NULL);

        return client->priv->bus_name;
}

static void
csm_dbus_client_set_property (GObject       *object,
                              guint          prop_id,
                              const GValue  *value,
                              GParamSpec    *pspec)
{
        CsmDBusClient *self;

        self = CSM_DBUS_CLIENT (object);

        switch (prop_id) {
        case PROP_BUS_NAME:
                csm_dbus_client_set_bus_name (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_dbus_client_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
        CsmDBusClient *self;

        self = CSM_DBUS_CLIENT (object);

        switch (prop_id) {
        case PROP_BUS_NAME:
                g_value_set_string (value, self->priv->bus_name);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_dbus_client_finalize (GObject *object)
{
        CsmDBusClient *client = (CsmDBusClient *) object;

        g_free (client->priv->bus_name);

        if (client->priv->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (client->priv->skeleton),
                                                                    client->priv->connection);
                g_clear_object (&client->priv->skeleton);
        }

        g_clear_object (&client->priv->connection);

        if (client->priv->watch_id != 0)
                g_bus_unwatch_name (client->priv->watch_id);

        G_OBJECT_CLASS (csm_dbus_client_parent_class)->finalize (object);
}

static GKeyFile *
dbus_client_save (CsmClient *client,
                  GError   **error)
{
        g_debug ("CsmDBusClient: saving client with id %s",
                 csm_client_peek_id (client));

        /* FIXME: We still don't support client saving for D-Bus
         * session clients */

        return NULL;
}

static gboolean
dbus_client_stop (CsmClient *client,
                  GError   **error)
{
        CsmDBusClient  *dbus_client = (CsmDBusClient *) client;
        csm_exported_client_private_emit_stop (dbus_client->priv->skeleton);
        return TRUE;
}

static char *
dbus_client_get_app_name (CsmClient *client)
{
        /* Always use app-id instead */
        return NULL;
}

static CsmClientRestartStyle
dbus_client_get_restart_style_hint (CsmClient *client)
{
        return (CSM_DBUS_CLIENT (client)->priv->restart_style_hint);
}

static guint
dbus_client_get_unix_process_id (CsmClient *client)
{
        return (CSM_DBUS_CLIENT (client)->priv->caller_pid);
}

static gboolean
dbus_client_query_end_session (CsmClient                *client,
                               CsmClientEndSessionFlag   flags,
                               GError                  **error)
{
        CsmDBusClient  *dbus_client = (CsmDBusClient *) client;

        if (dbus_client->priv->bus_name == NULL) {
                g_set_error (error,
                             CSM_CLIENT_ERROR,
                             CSM_CLIENT_ERROR_NOT_REGISTERED,
                             "Client is not registered");
                return FALSE;
        }

        g_debug ("CsmDBusClient: sending QueryEndSession signal to %s", dbus_client->priv->bus_name);

        csm_exported_client_private_emit_query_end_session (dbus_client->priv->skeleton, flags);
        return TRUE;
}

static gboolean
dbus_client_end_session (CsmClient                *client,
                         CsmClientEndSessionFlag   flags,
                         GError                  **error)
{
        CsmDBusClient  *dbus_client = (CsmDBusClient *) client;

        csm_exported_client_private_emit_end_session (dbus_client->priv->skeleton, flags);
        return TRUE;
}

static gboolean
dbus_client_cancel_end_session (CsmClient *client,
                                GError   **error)
{
        CsmDBusClient  *dbus_client = (CsmDBusClient *) client;
        csm_exported_client_private_emit_cancel_end_session (dbus_client->priv->skeleton);
        return TRUE;
}

static void
csm_dbus_client_class_init (CsmDBusClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        CsmClientClass *client_class = CSM_CLIENT_CLASS (klass);

        object_class->finalize             = csm_dbus_client_finalize;
        object_class->constructor          = csm_dbus_client_constructor;
        object_class->get_property         = csm_dbus_client_get_property;
        object_class->set_property         = csm_dbus_client_set_property;

        client_class->impl_save                   = dbus_client_save;
        client_class->impl_stop                   = dbus_client_stop;
        client_class->impl_query_end_session      = dbus_client_query_end_session;
        client_class->impl_end_session            = dbus_client_end_session;
        client_class->impl_cancel_end_session     = dbus_client_cancel_end_session;
        client_class->impl_get_app_name           = dbus_client_get_app_name;
        client_class->impl_get_restart_style_hint = dbus_client_get_restart_style_hint;
        client_class->impl_get_unix_process_id    = dbus_client_get_unix_process_id;

        g_object_class_install_property (object_class,
                                         PROP_BUS_NAME,
                                         g_param_spec_string ("bus-name",
                                                              "bus-name",
                                                              "bus-name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (CsmDBusClientPrivate));
}

CsmClient *
csm_dbus_client_new (const char *startup_id,
                     const char *bus_name)
{
        CsmDBusClient *client;

        client = g_object_new (CSM_TYPE_DBUS_CLIENT,
                               "startup-id", startup_id,
                               "bus-name", bus_name,
                               NULL);

        return CSM_CLIENT (client);
}
