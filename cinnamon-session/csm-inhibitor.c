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

#include <dbus/dbus-glib.h>

#include "csm-inhibitor.h"
#include "csm-inhibitor-glue.h"

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
        DBusGConnection *connection;
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

GQuark
csm_inhibitor_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("csm_inhibitor_error");
        }

        return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
csm_inhibitor_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (CSM_INHIBITOR_ERROR_GENERAL, "GeneralError"),
                        ENUM_ENTRY (CSM_INHIBITOR_ERROR_NOT_SET, "NotSet"),
                        { 0, 0, 0 }
                };

                g_assert (CSM_INHIBITOR_NUM_ERRORS == G_N_ELEMENTS (values) - 1);

                etype = g_enum_register_static ("CsmInhibitorError", values);
        }

        return etype;
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

        error = NULL;
        inhibitor->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (inhibitor->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        dbus_g_connection_register_g_object (inhibitor->priv->connection, inhibitor->priv->id, G_OBJECT (inhibitor));

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

gboolean
csm_inhibitor_get_app_id (CsmInhibitor *inhibitor,
                          char        **id,
                          GError      **error)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), FALSE);

        if (inhibitor->priv->app_id != NULL) {
                *id = g_strdup (inhibitor->priv->app_id);
        } else {
                *id = g_strdup ("");
        }

        return TRUE;
}

gboolean
csm_inhibitor_get_client_id (CsmInhibitor *inhibitor,
                             char        **id,
                             GError      **error)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), FALSE);

        /* object paths are not allowed to be NULL or blank */
        if (IS_STRING_EMPTY (inhibitor->priv->client_id)) {
                g_set_error (error,
                             CSM_INHIBITOR_ERROR,
                             CSM_INHIBITOR_ERROR_NOT_SET,
                             "Value is not set");
                return FALSE;
        }

        *id = g_strdup (inhibitor->priv->client_id);

        g_debug ("CsmInhibitor: getting client-id = '%s'", *id);

        return TRUE;
}

gboolean
csm_inhibitor_get_reason (CsmInhibitor *inhibitor,
                          char        **reason,
                          GError      **error)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), FALSE);

        if (inhibitor->priv->reason != NULL) {
                *reason = g_strdup (inhibitor->priv->reason);
        } else {
                *reason = g_strdup ("");
        }

        return TRUE;
}

gboolean
csm_inhibitor_get_flags (CsmInhibitor *inhibitor,
                         guint        *flags,
                         GError      **error)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), FALSE);

        *flags = inhibitor->priv->flags;

        return TRUE;
}

gboolean
csm_inhibitor_get_toplevel_xid (CsmInhibitor *inhibitor,
                                guint        *xid,
                                GError      **error)
{
        g_return_val_if_fail (CSM_IS_INHIBITOR (inhibitor), FALSE);

        *xid = inhibitor->priv->toplevel_xid;

        return TRUE;
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

        dbus_g_object_type_install_info (CSM_TYPE_INHIBITOR, &dbus_glib_csm_inhibitor_object_info);
        dbus_g_error_domain_register (CSM_INHIBITOR_ERROR, NULL, CSM_INHIBITOR_TYPE_ERROR);
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
