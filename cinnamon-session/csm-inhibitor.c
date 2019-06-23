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

#include "csm-inhibitor.h"
#include "csm-exported-inhibitor.h"

#include "csm-util.h"

static guint32 inhibitor_serial = 1;

#define CSM_INHIBITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_INHIBITOR, CsmInhibitorPrivate))

struct CsmInhibitorPrivate
{
        char *id;
        char *bus_name;
        char *app_id;
        char *client_id;
        char *reason;
        guint flags;
        guint toplevel_xid;
        guint cookie;
        GDBusConnection *connection;
        CsmExportedInhibitor *skeleton;
};

enum {
        PROP_0,
        PROP_BUS_NAME,
        PROP_REASON,
        PROP_APP_ID,
        PROP_CLIENT_ID,
        PROP_FLAGS,
        PROP_TOPLEVEL_XID,
        PROP_COOKIE
};

G_DEFINE_TYPE (CsmInhibitor, csm_inhibitor, G_TYPE_OBJECT)

#define CSM_INHIBITOR_DBUS_IFACE "org.gnome.SessionManager.Inhibitor"

static const GDBusErrorEntry csm_inhibitor_error_entries[] = {
        { CSM_INHIBITOR_ERROR_GENERAL, CSM_INHIBITOR_DBUS_IFACE ".GeneralError" },
        { CSM_INHIBITOR_ERROR_NOT_SET, CSM_INHIBITOR_DBUS_IFACE ".NotSet" }
};

GQuark
csm_inhibitor_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("csm_inhibitor_error",
                                            &quark_volatile,
                                            csm_inhibitor_error_entries,
                                            G_N_ELEMENTS (csm_inhibitor_error_entries));

        return quark_volatile;
}

static gboolean
csm_inhibitor_get_app_id (CsmExportedInhibitor  *skeleton,
                          GDBusMethodInvocation *invocation,
                          CsmInhibitor          *inhibitor)
{
        const gchar *id;

        if (inhibitor->priv->app_id != NULL) {
                id = inhibitor->priv->app_id;
        } else {
                id = "";
        }

        csm_exported_inhibitor_complete_get_app_id (skeleton, invocation, id);

        return TRUE;
}

static gboolean
csm_inhibitor_get_client_id (CsmExportedInhibitor  *skeleton,
                             GDBusMethodInvocation *invocation,
                             CsmInhibitor          *inhibitor)
{
        /* object paths are not allowed to be NULL or blank */
        if (IS_STRING_EMPTY (inhibitor->priv->client_id)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_INHIBITOR_ERROR,
                                                       CSM_INHIBITOR_ERROR_NOT_SET,
                                                       "Value is not set");

                return TRUE;
        }

        csm_exported_inhibitor_complete_get_client_id (skeleton, invocation, inhibitor->priv->client_id);

        g_debug ("CsmInhibitor: getting client-id = '%s'", inhibitor->priv->client_id);

        return TRUE;
}

static gboolean
csm_inhibitor_get_reason (CsmExportedInhibitor  *skeleton,
                          GDBusMethodInvocation *invocation,
                          CsmInhibitor          *inhibitor)
{
        const gchar *reason;

        if (inhibitor->priv->reason != NULL) {
                reason = inhibitor->priv->reason;
        } else {
                reason = "";
        }

        csm_exported_inhibitor_complete_get_reason (skeleton, invocation, reason);

        return TRUE;
}

static gboolean
csm_inhibitor_get_flags (CsmExportedInhibitor  *skeleton,
                         GDBusMethodInvocation *invocation,
                         CsmInhibitor          *inhibitor)
{
        csm_exported_inhibitor_complete_get_flags (skeleton, invocation, inhibitor->priv->flags);

        return TRUE;
}

static gboolean
csm_inhibitor_get_toplevel_xid (CsmExportedInhibitor  *skeleton,
                                GDBusMethodInvocation *invocation,
                                CsmInhibitor          *inhibitor)
{

        csm_exported_inhibitor_complete_get_toplevel_xid (skeleton, invocation, inhibitor->priv->toplevel_xid);

        return TRUE;
}

static guint32
get_next_inhibitor_serial (void)
{
        guint32 serial;

        serial = inhibitor_serial++;

        if ((gint32)inhibitor_serial < 0) {
                inhibitor_serial = 1;
        }

        return serial;
}

static gboolean
register_inhibitor (CsmInhibitor *inhibitor)
{
        GError *error;
        CsmExportedInhibitor *skeleton;

        error = NULL;
        inhibitor->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (error != NULL) {
                g_critical ("error getting session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        skeleton = csm_exported_inhibitor_skeleton_new ();
        inhibitor->priv->skeleton = skeleton;
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          inhibitor->priv->connection,
                                          inhibitor->priv->id, &error);

        if (error != NULL) {
                g_critical ("error exporting inhibitor on session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_signal_connect (skeleton, "handle-get-app-id",
                          G_CALLBACK (csm_inhibitor_get_app_id), inhibitor);
        g_signal_connect (skeleton, "handle-get-client-id",
                          G_CALLBACK (csm_inhibitor_get_client_id), inhibitor);
        g_signal_connect (skeleton, "handle-get-flags",
                          G_CALLBACK (csm_inhibitor_get_flags), inhibitor);
        g_signal_connect (skeleton, "handle-get-reason",
                          G_CALLBACK (csm_inhibitor_get_reason), inhibitor);
        g_signal_connect (skeleton, "handle-get-toplevel-xid",
                          G_CALLBACK (csm_inhibitor_get_toplevel_xid), inhibitor);

        return TRUE;
}

static GObject *
csm_inhibitor_constructor (GType                  type,
                           guint                  n_construct_properties,
                           GObjectConstructParam *construct_properties)
{
        CsmInhibitor *inhibitor;
        gboolean      res;

        inhibitor = CSM_INHIBITOR (G_OBJECT_CLASS (csm_inhibitor_parent_class)->constructor (type,
                                                                                             n_construct_properties,
                                                                                             construct_properties));

        g_free (inhibitor->priv->id);
        inhibitor->priv->id = g_strdup_printf ("/org/gnome/SessionManager/Inhibitor%u", get_next_inhibitor_serial ());
        res = register_inhibitor (inhibitor);
        if (! res) {
                g_warning ("Unable to register inhibitor with session bus");
        }

        return G_OBJECT (inhibitor);
}

static void
csm_inhibitor_init (CsmInhibitor *inhibitor)
{
        inhibitor->priv = CSM_INHIBITOR_GET_PRIVATE (inhibitor);
}

static void
csm_inhibitor_set_bus_name (CsmInhibitor  *inhibitor,
                            const char    *bus_name)
{
        g_return_if_fail (CSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->bus_name);

        if (bus_name != NULL) {
                inhibitor->priv->bus_name = g_strdup (bus_name);
        } else {
                inhibitor->priv->bus_name = g_strdup ("");
        }
        g_object_notify (G_OBJECT (inhibitor), "bus-name");
}

static void
csm_inhibitor_set_app_id (CsmInhibitor  *inhibitor,
                          const char    *app_id)
{
        g_return_if_fail (CSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->app_id);

        inhibitor->priv->app_id = g_strdup (app_id);
        g_object_notify (G_OBJECT (inhibitor), "app-id");
}

static void
csm_inhibitor_set_client_id (CsmInhibitor  *inhibitor,
                             const char    *client_id)
{
        g_return_if_fail (CSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->client_id);

        g_debug ("CsmInhibitor: setting client-id = %s", client_id);

        if (client_id != NULL) {
                inhibitor->priv->client_id = g_strdup (client_id);
        } else {
                inhibitor->priv->client_id = g_strdup ("");
        }
        g_object_notify (G_OBJECT (inhibitor), "client-id");
}

static void
csm_inhibitor_set_reason (CsmInhibitor  *inhibitor,
                          const char    *reason)
{
        g_return_if_fail (CSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->reason);

        if (reason != NULL) {
                inhibitor->priv->reason = g_strdup (reason);
        } else {
                inhibitor->priv->reason = g_strdup ("");
        }
        g_object_notify (G_OBJECT (inhibitor), "reason");
}

static void
csm_inhibitor_set_cookie (CsmInhibitor  *inhibitor,
                          guint          cookie)
{
        g_return_if_fail (CSM_IS_INHIBITOR (inhibitor));

        if (inhibitor->priv->cookie != cookie) {
                inhibitor->priv->cookie = cookie;
                g_object_notify (G_OBJECT (inhibitor), "cookie");
        }
}

static void
csm_inhibitor_set_flags (CsmInhibitor  *inhibitor,
                         guint          flags)
{
        g_return_if_fail (CSM_IS_INHIBITOR (inhibitor));

        if (inhibitor->priv->flags != flags) {
                inhibitor->priv->flags = flags;
                g_object_notify (G_OBJECT (inhibitor), "flags");
        }
}

static void
csm_inhibitor_set_toplevel_xid (CsmInhibitor  *inhibitor,
                                guint          xid)
{
        g_return_if_fail (CSM_IS_INHIBITOR (inhibitor));

        if (inhibitor->priv->toplevel_xid != xid) {
                inhibitor->priv->toplevel_xid = xid;
                g_object_notify (G_OBJECT (inhibitor), "toplevel-xid");
        }
}

const char *
csm_inhibitor_peek_bus_name (CsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->bus_name;
}

const char *
csm_inhibitor_peek_id (CsmInhibitor *inhibitor)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->id;
}

const char *
csm_inhibitor_peek_app_id (CsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->app_id;
}

const char *
csm_inhibitor_peek_client_id (CsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->client_id;
}

const char *
csm_inhibitor_peek_reason (CsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->reason;
}

guint
csm_inhibitor_peek_flags (CsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), 0);

        return inhibitor->priv->flags;
}

guint
csm_inhibitor_peek_toplevel_xid (CsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), 0);

        return inhibitor->priv->toplevel_xid;
}

guint
csm_inhibitor_peek_cookie (CsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), 0);

        return inhibitor->priv->cookie;
}

static void
csm_inhibitor_set_property (GObject       *object,
                            guint          prop_id,
                            const GValue  *value,
                            GParamSpec    *pspec)
{
        CsmInhibitor *self;

        self = CSM_INHIBITOR (object);

        switch (prop_id) {
        case PROP_BUS_NAME:
                csm_inhibitor_set_bus_name (self, g_value_get_string (value));
                break;
        case PROP_APP_ID:
                csm_inhibitor_set_app_id (self, g_value_get_string (value));
                break;
        case PROP_CLIENT_ID:
                csm_inhibitor_set_client_id (self, g_value_get_string (value));
                break;
        case PROP_REASON:
                csm_inhibitor_set_reason (self, g_value_get_string (value));
                break;
        case PROP_FLAGS:
                csm_inhibitor_set_flags (self, g_value_get_uint (value));
                break;
        case PROP_COOKIE:
                csm_inhibitor_set_cookie (self, g_value_get_uint (value));
                break;
        case PROP_TOPLEVEL_XID:
                csm_inhibitor_set_toplevel_xid (self, g_value_get_uint (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_inhibitor_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
        CsmInhibitor *self;

        self = CSM_INHIBITOR (object);

        switch (prop_id) {
        case PROP_BUS_NAME:
                g_value_set_string (value, self->priv->bus_name);
                break;
        case PROP_APP_ID:
                g_value_set_string (value, self->priv->app_id);
                break;
        case PROP_CLIENT_ID:
                g_value_set_string (value, self->priv->client_id);
                break;
        case PROP_REASON:
                g_value_set_string (value, self->priv->reason);
                break;
        case PROP_FLAGS:
                g_value_set_uint (value, self->priv->flags);
                break;
        case PROP_COOKIE:
                g_value_set_uint (value, self->priv->cookie);
                break;
        case PROP_TOPLEVEL_XID:
                g_value_set_uint (value, self->priv->toplevel_xid);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_inhibitor_finalize (GObject *object)
{
        CsmInhibitor *inhibitor = (CsmInhibitor *) object;

        g_free (inhibitor->priv->id);
        g_free (inhibitor->priv->bus_name);
        g_free (inhibitor->priv->app_id);
        g_free (inhibitor->priv->client_id);
        g_free (inhibitor->priv->reason);

        if (inhibitor->priv->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (inhibitor->priv->skeleton),
                                                                    inhibitor->priv->connection);
                g_clear_object (&inhibitor->priv->skeleton);
        }

        g_clear_object (&inhibitor->priv->connection);

        G_OBJECT_CLASS (csm_inhibitor_parent_class)->finalize (object);
}

static void
csm_inhibitor_class_init (CsmInhibitorClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize             = csm_inhibitor_finalize;
        object_class->constructor          = csm_inhibitor_constructor;
        object_class->get_property         = csm_inhibitor_get_property;
        object_class->set_property         = csm_inhibitor_set_property;

        g_object_class_install_property (object_class,
                                         PROP_BUS_NAME,
                                         g_param_spec_string ("bus-name",
                                                              "bus-name",
                                                              "bus-name",
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
                                         PROP_CLIENT_ID,
                                         g_param_spec_string ("client-id",
                                                              "client-id",
                                                              "client-id",
                                                              "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_REASON,
                                         g_param_spec_string ("reason",
                                                              "reason",
                                                              "reason",
                                                              "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_FLAGS,
                                         g_param_spec_uint ("flags",
                                                            "flags",
                                                            "flags",
                                                            0,
                                                            G_MAXINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_TOPLEVEL_XID,
                                         g_param_spec_uint ("toplevel-xid",
                                                            "toplevel-xid",
                                                            "toplevel-xid",
                                                            0,
                                                            G_MAXINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_COOKIE,
                                         g_param_spec_uint ("cookie",
                                                            "cookie",
                                                            "cookie",
                                                            0,
                                                            G_MAXINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (CsmInhibitorPrivate));
}

CsmInhibitor *
csm_inhibitor_new (const char    *app_id,
                   guint          toplevel_xid,
                   guint          flags,
                   const char    *reason,
                   const char    *bus_name,
                   guint          cookie)
{
        CsmInhibitor *inhibitor;

        inhibitor = g_object_new (CSM_TYPE_INHIBITOR,
                                  "app-id", app_id,
                                  "reason", reason,
                                  "bus-name", bus_name,
                                  "flags", flags,
                                  "toplevel-xid", toplevel_xid,
                                  "cookie", cookie,
                                  NULL);

        return inhibitor;
}

CsmInhibitor *
csm_inhibitor_new_for_client (const char    *client_id,
                              const char    *app_id,
                              guint          flags,
                              const char    *reason,
                              const char    *bus_name,
                              guint          cookie)
{
        CsmInhibitor *inhibitor;

        inhibitor = g_object_new (CSM_TYPE_INHIBITOR,
                                  "client-id", client_id,
                                  "app-id", app_id,
                                  "reason", reason,
                                  "bus-name", bus_name,
                                  "flags", flags,
                                  "cookie", cookie,
                                  NULL);

        return inhibitor;
}
