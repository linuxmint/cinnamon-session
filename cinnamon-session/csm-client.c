/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include "config.h"

#include "eggdesktopfile.h"

#include "csm-marshal.h"
#include "csm-client.h"
#include "csm-exported-client.h"

static guint32 client_serial = 1;

#define CSM_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_CLIENT, CsmClientPrivate))

struct CsmClientPrivate
{
        char              *id;
        char              *startup_id;
        char              *app_id;
        guint              status;
        CsmExportedClient *skeleton;
        GDBusConnection   *connection;
};

enum {
        PROP_0,
        PROP_ID,
        PROP_STARTUP_ID,
        PROP_APP_ID,
        PROP_STATUS
};

enum {
        DISCONNECTED,
        END_SESSION_RESPONSE,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_ABSTRACT_TYPE (CsmClient, csm_client, G_TYPE_OBJECT)

#define CSM_CLIENT_DBUS_IFACE "org.gnome.SessionManager.Client"

static const GDBusErrorEntry csm_client_error_entries[] = {
        { CSM_CLIENT_ERROR_GENERAL, CSM_CLIENT_DBUS_IFACE ".GeneralError" },
        { CSM_CLIENT_ERROR_NOT_REGISTERED, CSM_CLIENT_DBUS_IFACE ".NotRegistered" }
};

GQuark
csm_client_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("csm_client_error",
                                            &quark_volatile,
                                            csm_client_error_entries,
                                            G_N_ELEMENTS (csm_client_error_entries));
        return quark_volatile;
}

static guint32
get_next_client_serial (void)
{
        guint32 serial;

        serial = client_serial++;

        if ((gint32)client_serial < 0) {
                client_serial = 1;
        }

        return serial;
}

static gboolean
csm_client_get_startup_id (CsmExportedClient     *skeleton,
                           GDBusMethodInvocation *invocation,
                           CsmClient             *client)
{
        csm_exported_client_complete_get_startup_id (skeleton,
                                                     invocation,
                                                     client->priv->startup_id);
        return TRUE;
}

static gboolean
csm_client_get_app_id (CsmExportedClient     *skeleton,
                       GDBusMethodInvocation *invocation,
                       CsmClient             *client)
{
        csm_exported_client_complete_get_app_id (skeleton,
                                                 invocation,
                                                 client->priv->app_id);
        return TRUE;
}

static gboolean
csm_client_get_restart_style_hint (CsmExportedClient     *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   CsmClient             *client)
{
        guint hint;

        hint = CSM_CLIENT_GET_CLASS (client)->impl_get_restart_style_hint (client);
        csm_exported_client_complete_get_restart_style_hint (skeleton,
                                                             invocation,
                                                             hint);
        return TRUE;
}

static gboolean
csm_client_get_status (CsmExportedClient     *skeleton,
                       GDBusMethodInvocation *invocation,
                       CsmClient             *client)
{
        csm_exported_client_complete_get_status (skeleton,
                                                 invocation,
                                                 client->priv->status);
        return TRUE;
}

static gboolean
csm_client_get_unix_process_id (CsmExportedClient     *skeleton,
                                GDBusMethodInvocation *invocation,
                                CsmClient             *client)
{
        guint pid;

        pid = CSM_CLIENT_GET_CLASS (client)->impl_get_unix_process_id (client);
        csm_exported_client_complete_get_unix_process_id (skeleton,
                                                          invocation,
                                                          pid);
        return TRUE;
}

static gboolean
csm_client_stop_dbus (CsmExportedClient     *skeleton,
                      GDBusMethodInvocation *invocation,
                      CsmClient             *client)
{
        GError *error = NULL;
        csm_client_stop (client, &error);

        if (error != NULL) {
                g_dbus_method_invocation_take_error (invocation, error);
        } else {
                csm_exported_client_complete_stop (skeleton, invocation);
        }

        return TRUE;
}

static gboolean
register_client (CsmClient *client)
{
        CsmExportedClient *skeleton;
        GError *error;

        error = NULL;
        client->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (client->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        skeleton = csm_exported_client_skeleton_new ();
        client->priv->skeleton = skeleton;

        g_debug ("exporting client to object path: %s", client->priv->id);
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          client->priv->connection,
                                          client->priv->id, &error);

        if (error != NULL) {
                g_critical ("error exporting client on session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_signal_connect (skeleton, "handle-get-app-id",
                          G_CALLBACK (csm_client_get_app_id), client);
        g_signal_connect (skeleton, "handle-get-restart-style-hint",
                          G_CALLBACK (csm_client_get_restart_style_hint), client);
        g_signal_connect (skeleton, "handle-get-startup-id",
                          G_CALLBACK (csm_client_get_startup_id), client);
        g_signal_connect (skeleton, "handle-get-status",
                          G_CALLBACK (csm_client_get_status), client);
        g_signal_connect (skeleton, "handle-get-unix-process-id",
                          G_CALLBACK (csm_client_get_unix_process_id), client);
        g_signal_connect (skeleton, "handle-stop",
                          G_CALLBACK (csm_client_stop_dbus), client);

        return TRUE;
}

static GObject *
csm_client_constructor (GType                  type,
                        guint                  n_construct_properties,
                        GObjectConstructParam *construct_properties)
{
        CsmClient *client;
        gboolean   res;

        client = CSM_CLIENT (G_OBJECT_CLASS (csm_client_parent_class)->constructor (type,
                                                                                    n_construct_properties,
                                                                                    construct_properties));

        g_free (client->priv->id);
        client->priv->id = g_strdup_printf ("/org/gnome/SessionManager/Client%u", get_next_client_serial ());

        res = register_client (client);
        if (! res) {
                g_warning ("Unable to register client with session bus");
        }

        return G_OBJECT (client);
}

static void
csm_client_init (CsmClient *client)
{
        client->priv = CSM_CLIENT_GET_PRIVATE (client);
}

static void
csm_client_finalize (GObject *object)
{
        CsmClient *client;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CSM_IS_CLIENT (object));

        client = CSM_CLIENT (object);

        g_return_if_fail (client->priv != NULL);

        g_free (client->priv->id);
        g_free (client->priv->startup_id);
        g_free (client->priv->app_id);

        if (client->priv->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (client->priv->skeleton),
                                                                    client->priv->connection);
                g_clear_object (&client->priv->skeleton);
        }

        g_clear_object (&client->priv->connection);

        G_OBJECT_CLASS (csm_client_parent_class)->finalize (object);
}

void
csm_client_set_status (CsmClient *client,
                       guint      status)
{
        g_return_if_fail (CSM_IS_CLIENT (client));
        if (client->priv->status != status) {
                client->priv->status = status;
                g_object_notify (G_OBJECT (client), "status");
        }
}

static void
csm_client_set_startup_id (CsmClient  *client,
                           const char *startup_id)
{
        g_return_if_fail (CSM_IS_CLIENT (client));

        g_free (client->priv->startup_id);

        if (startup_id != NULL) {
                client->priv->startup_id = g_strdup (startup_id);
        } else {
                client->priv->startup_id = g_strdup ("");
        }
        g_object_notify (G_OBJECT (client), "startup-id");
}

void
csm_client_set_app_id (CsmClient  *client,
                       const char *app_id)
{
        g_return_if_fail (CSM_IS_CLIENT (client));

        g_free (client->priv->app_id);

        if (app_id != NULL) {
                client->priv->app_id = g_strdup (app_id);
        } else {
                client->priv->app_id = g_strdup ("");
        }
        g_object_notify (G_OBJECT (client), "app-id");
}

static void
csm_client_set_property (GObject       *object,
                         guint          prop_id,
                         const GValue  *value,
                         GParamSpec    *pspec)
{
        CsmClient *self;

        self = CSM_CLIENT (object);

        switch (prop_id) {
        case PROP_STARTUP_ID:
                csm_client_set_startup_id (self, g_value_get_string (value));
                break;
        case PROP_APP_ID:
                csm_client_set_app_id (self, g_value_get_string (value));
                break;
        case PROP_STATUS:
                csm_client_set_status (self, g_value_get_uint (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_client_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
        CsmClient *self;

        self = CSM_CLIENT (object);

        switch (prop_id) {
        case PROP_STARTUP_ID:
                g_value_set_string (value, self->priv->startup_id);
                break;
        case PROP_APP_ID:
                g_value_set_string (value, self->priv->app_id);
                break;
        case PROP_STATUS:
                g_value_set_uint (value, self->priv->status);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
default_stop (CsmClient *client,
              GError   **error)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), FALSE);

        g_warning ("Stop not implemented");

        return TRUE;
}

static void
csm_client_dispose (GObject *object)
{
        CsmClient *client;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CSM_IS_CLIENT (object));

        client = CSM_CLIENT (object);

        g_debug ("CsmClient: disposing %s", client->priv->id);

        G_OBJECT_CLASS (csm_client_parent_class)->dispose (object);
}

static void
csm_client_class_init (CsmClientClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = csm_client_get_property;
        object_class->set_property = csm_client_set_property;
        object_class->constructor = csm_client_constructor;
        object_class->finalize = csm_client_finalize;
        object_class->dispose = csm_client_dispose;

        klass->impl_stop = default_stop;

        signals[DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmClientClass, disconnected),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              0);
        signals[END_SESSION_RESPONSE] =
                g_signal_new ("end-session-response",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmClientClass, end_session_response),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              4, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_STARTUP_ID,
                                         g_param_spec_string ("startup-id",
                                                              "startup-id",
                                                              "startup-id",
                                                              "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_APP_ID,
                                         g_param_spec_string ("app-id",
                                                              "app-id",
                                                              "app-id",
                                                              "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_STATUS,
                                         g_param_spec_uint ("status",
                                                            "status",
                                                            "status",
                                                            0,
                                                            G_MAXINT,
                                                            CSM_CLIENT_UNREGISTERED,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (CsmClientPrivate));
}

const char *
csm_client_peek_id (CsmClient *client)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), NULL);

        return client->priv->id;
}

/**
 * csm_client_peek_app_id:
 * @client: a #CsmClient.
 *
 * Note that the application ID might not be known; this happens when for XSMP
 * clients that we did not start ourselves, for instance.
 *
 * Returns: the application ID of the client, or %NULL if no such ID is
 * known. The string is owned by @client.
 **/
const char *
csm_client_peek_app_id (CsmClient *client)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), NULL);

        return client->priv->app_id;
}

const char *
csm_client_peek_startup_id (CsmClient *client)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), NULL);

        return client->priv->startup_id;
}

guint
csm_client_peek_status (CsmClient *client)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), CSM_CLIENT_UNREGISTERED);

        return client->priv->status;
}

guint
csm_client_peek_restart_style_hint (CsmClient *client)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), CSM_CLIENT_RESTART_NEVER);

        return CSM_CLIENT_GET_CLASS (client)->impl_get_restart_style_hint (client);
}

/**
 * csm_client_get_app_name:
 * @client: a #CsmClient.
 *
 * Returns: a copy of the application name of the client, or %NULL if no such
 * name is known.
 **/
char *
csm_client_get_app_name (CsmClient *client)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), NULL);

        return CSM_CLIENT_GET_CLASS (client)->impl_get_app_name (client);
}

gboolean
csm_client_cancel_end_session (CsmClient *client,
                               GError   **error)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), FALSE);

        return CSM_CLIENT_GET_CLASS (client)->impl_cancel_end_session (client, error);
}


gboolean
csm_client_query_end_session (CsmClient *client,
                              guint      flags,
                              GError   **error)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), FALSE);

        return CSM_CLIENT_GET_CLASS (client)->impl_query_end_session (client, flags, error);
}

gboolean
csm_client_end_session (CsmClient *client,
                        guint      flags,
                        GError   **error)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), FALSE);

        return CSM_CLIENT_GET_CLASS (client)->impl_end_session (client, flags, error);
}

gboolean
csm_client_stop (CsmClient *client,
                 GError   **error)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), FALSE);

        return CSM_CLIENT_GET_CLASS (client)->impl_stop (client, error);
}

void
csm_client_disconnected (CsmClient *client)
{
        g_signal_emit (client, signals[DISCONNECTED], 0);
}

GKeyFile *
csm_client_save (CsmClient *client,
                 GError   **error)
{
        g_return_val_if_fail (CSM_IS_CLIENT (client), FALSE);

        return CSM_CLIENT_GET_CLASS (client)->impl_save (client, error);
}

void
csm_client_end_session_response (CsmClient  *client,
                                 gboolean    is_ok,
                                 gboolean    do_last,
                                 gboolean    cancel,
                                 const char *reason)
{
        g_signal_emit (client, signals[END_SESSION_RESPONSE], 0,
                       is_ok, do_last, cancel, reason);
}
