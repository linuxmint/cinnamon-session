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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <string.h>

#include "csm-app.h"
#include "csm-exported-app.h"

#define CSM_APP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_APP, CsmAppPrivate))

/* If a component crashes twice within a minute, we count that as a fatal error */
#define _CSM_APP_RESPAWN_RATELIMIT_SECONDS 60

struct _CsmAppPrivate
{
        char            *id;
        char            *app_id;
        int              phase;
        char            *startup_id;
        gboolean         ever_started;
        GTimeVal         last_restart_time;
        CsmExportedApp  *skeleton;
        GDBusConnection *connection;
};


enum {
        EXITED,
        DIED,
        REGISTERED,
        LAST_SIGNAL
};

static guint32 app_serial = 1;

static guint signals[LAST_SIGNAL] = { 0 };

enum {
        PROP_0,
        PROP_ID,
        PROP_STARTUP_ID,
        PROP_PHASE,
        LAST_PROP
};

G_DEFINE_TYPE (CsmApp, csm_app, G_TYPE_OBJECT)

GQuark
csm_app_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("csm_app_error");
        }

        return ret;

}

static gboolean
csm_app_get_app_id (CsmExportedApp        *skeleton,
                    GDBusMethodInvocation *invocation,
                    CsmApp                *app)
{
        const gchar *id;

        id = CSM_APP_GET_CLASS (app)->impl_get_app_id (app);

        csm_exported_app_complete_get_app_id (skeleton,
                                              invocation,
                                              id);

        return TRUE;
}

static gboolean
csm_app_get_startup_id (CsmExportedApp        *skeleton,
                        GDBusMethodInvocation *invocation,
                        CsmApp                *app)
{
        const gchar *id;

        id = g_strdup (app->priv->startup_id);

        csm_exported_app_complete_get_startup_id (skeleton,
                                                  invocation,
                                                  id);

        return TRUE;
}

static gboolean
csm_app_get_phase (CsmExportedApp        *skeleton,
                   GDBusMethodInvocation *invocation,
                   CsmApp                *app)
{
        csm_exported_app_complete_get_phase (skeleton,
                                             invocation,
                                             app->priv->phase);
        return TRUE;
}

static guint32
get_next_app_serial (void)
{
        guint32 serial;

        serial = app_serial++;

        if ((gint32)app_serial < 0) {
                app_serial = 1;
        }

        return serial;
}


static gboolean
register_app (CsmApp *app)
{
        CsmExportedApp *skeleton;
        GError *error;

        error = NULL;
        app->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (app->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        skeleton = csm_exported_app_skeleton_new ();
        app->priv->skeleton = skeleton;

        g_debug ("exporting app to object path: %s", app->priv->id);
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          app->priv->connection,
                                          app->priv->id, &error);

        if (error != NULL) {
                g_critical ("error exporting app on session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_signal_connect (skeleton, "handle-get-app-id",
                          G_CALLBACK (csm_app_get_app_id), app);
        g_signal_connect (skeleton, "handle-get-startup-id",
                          G_CALLBACK (csm_app_get_startup_id), app);
        g_signal_connect (skeleton, "handle-get-phase",
                          G_CALLBACK (csm_app_get_phase), app);

        return TRUE;
}

static GObject *
csm_app_constructor (GType                  type,
                     guint                  n_construct_properties,
                     GObjectConstructParam *construct_properties)
{
        CsmApp    *app;
        gboolean   res;

        app = CSM_APP (G_OBJECT_CLASS (csm_app_parent_class)->constructor (type,
                                                                           n_construct_properties,
                                                                           construct_properties));

        g_free (app->priv->id);
        app->priv->id = g_strdup_printf ("/org/gnome/SessionManager/App%u", get_next_app_serial ());

        res = register_app (app);
        if (! res) {
                g_warning ("Unable to register app with session bus");
        }

        return G_OBJECT (app);
}

static void
csm_app_init (CsmApp *app)
{
        app->priv = CSM_APP_GET_PRIVATE (app);
}

static void
csm_app_set_phase (CsmApp *app,
                   int     phase)
{
        g_return_if_fail (CSM_IS_APP (app));

        app->priv->phase = phase;
}

static void
csm_app_set_id (CsmApp     *app,
                const char *id)
{
        g_return_if_fail (CSM_IS_APP (app));

        g_free (app->priv->id);

        app->priv->id = g_strdup (id);
        g_object_notify (G_OBJECT (app), "id");

}
static void
csm_app_set_startup_id (CsmApp     *app,
                        const char *startup_id)
{
        g_return_if_fail (CSM_IS_APP (app));

        g_free (app->priv->startup_id);

        app->priv->startup_id = g_strdup (startup_id);
        g_object_notify (G_OBJECT (app), "startup-id");

}

static void
csm_app_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
        CsmApp *app = CSM_APP (object);

        switch (prop_id) {
        case PROP_STARTUP_ID:
                csm_app_set_startup_id (app, g_value_get_string (value));
                break;
        case PROP_ID:
                csm_app_set_id (app, g_value_get_string (value));
                break;
        case PROP_PHASE:
                csm_app_set_phase (app, g_value_get_int (value));
                break;
        default:
                break;
        }
}

static void
csm_app_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
        CsmApp *app = CSM_APP (object);

        switch (prop_id) {
        case PROP_STARTUP_ID:
                g_value_set_string (value, app->priv->startup_id);
                break;
        case PROP_ID:
                g_value_set_string (value, app->priv->id);
                break;
        case PROP_PHASE:
                g_value_set_int (value, app->priv->phase);
                break;
        default:
                break;
        }
}

static void
csm_app_dispose (GObject *object)
{
        CsmApp *app = CSM_APP (object);

        g_free (app->priv->startup_id);
        app->priv->startup_id = NULL;

        g_free (app->priv->id);
        app->priv->id = NULL;

        if (app->priv->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (app->priv->skeleton),
                                                                    app->priv->connection);
                g_clear_object (&app->priv->skeleton);
        }

        g_clear_object (&app->priv->connection);

        G_OBJECT_CLASS (csm_app_parent_class)->dispose (object);
}

static void
csm_app_class_init (CsmAppClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = csm_app_set_property;
        object_class->get_property = csm_app_get_property;
        object_class->dispose = csm_app_dispose;
        object_class->constructor = csm_app_constructor;

        klass->impl_start = NULL;
        klass->impl_get_app_id = NULL;
        klass->impl_get_autorestart = NULL;
        klass->impl_provides = NULL;
        klass->impl_get_provides = NULL;
        klass->impl_is_running = NULL;
        klass->impl_peek_autostart_delay = NULL;

        g_object_class_install_property (object_class,
                                         PROP_PHASE,
                                         g_param_spec_int ("phase",
                                                           "Phase",
                                                           "Phase",
                                                           -1,
                                                           G_MAXINT,
                                                           -1,
                                                           G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_ID,
                                         g_param_spec_string ("id",
                                                              "ID",
                                                              "ID",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_STARTUP_ID,
                                         g_param_spec_string ("startup-id",
                                                              "startup ID",
                                                              "Session management startup ID",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        signals[EXITED] =
                g_signal_new ("exited",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmAppClass, exited),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              1, G_TYPE_UCHAR);
        signals[DIED] =
                g_signal_new ("died",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmAppClass, died),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              1, G_TYPE_INT);

        signals[REGISTERED] =
                g_signal_new ("registered",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmAppClass, registered),
                              NULL, NULL, NULL,
                              G_TYPE_NONE,
                              0);

        g_type_class_add_private (klass, sizeof (CsmAppPrivate));
}

const char *
csm_app_peek_id (CsmApp *app)
{
        return app->priv->id;
}

const char *
csm_app_peek_app_id (CsmApp *app)
{
        return CSM_APP_GET_CLASS (app)->impl_get_app_id (app);
}

const char *
csm_app_peek_startup_id (CsmApp *app)
{
        return app->priv->startup_id;
}

/**
 * csm_app_peek_phase:
 * @app: a %CsmApp
 *
 * Returns @app's startup phase.
 *
 * Return value: @app's startup phase
 **/
CsmManagerPhase
csm_app_peek_phase (CsmApp *app)
{
        g_return_val_if_fail (CSM_IS_APP (app), CSM_MANAGER_PHASE_APPLICATION);

        return app->priv->phase;
}

gboolean
csm_app_peek_is_disabled (CsmApp *app)
{
        g_return_val_if_fail (CSM_IS_APP (app), FALSE);

        if (CSM_APP_GET_CLASS (app)->impl_is_disabled) {
                return CSM_APP_GET_CLASS (app)->impl_is_disabled (app);
        } else {
                return FALSE;
        }
}

gboolean
csm_app_peek_is_conditionally_disabled (CsmApp *app)
{
        g_return_val_if_fail (CSM_IS_APP (app), FALSE);

        if (CSM_APP_GET_CLASS (app)->impl_is_conditionally_disabled) {
                return CSM_APP_GET_CLASS (app)->impl_is_conditionally_disabled (app);
        } else {
                return FALSE;
        }
}

gboolean
csm_app_is_running (CsmApp *app)
{
        g_return_val_if_fail (CSM_IS_APP (app), FALSE);

        if (CSM_APP_GET_CLASS (app)->impl_is_running) {
                return CSM_APP_GET_CLASS (app)->impl_is_running (app);
        } else {
                return FALSE;
        }
}

gboolean
csm_app_peek_autorestart (CsmApp *app)
{
        g_return_val_if_fail (CSM_IS_APP (app), FALSE);

        if (CSM_APP_GET_CLASS (app)->impl_get_autorestart) {
                return CSM_APP_GET_CLASS (app)->impl_get_autorestart (app);
        } else {
                return FALSE;
        }
}

gboolean
csm_app_provides (CsmApp *app, const char *service)
{
        if (CSM_APP_GET_CLASS (app)->impl_provides) {
                return CSM_APP_GET_CLASS (app)->impl_provides (app, service);
        } else {
                return FALSE;
        }
}

char **
csm_app_get_provides (CsmApp *app)
{
        if (CSM_APP_GET_CLASS (app)->impl_get_provides) {
                return CSM_APP_GET_CLASS (app)->impl_get_provides (app);
        } else {
                return NULL;
        }
}

gboolean
csm_app_has_autostart_condition (CsmApp     *app,
                                 const char *condition)
{

        if (CSM_APP_GET_CLASS (app)->impl_has_autostart_condition) {
                return CSM_APP_GET_CLASS (app)->impl_has_autostart_condition (app, condition);
        } else {
                return FALSE;
        }
}

gboolean
csm_app_start (CsmApp  *app,
               GError **error)
{
        g_debug ("Starting app: %s", app->priv->id);
        return CSM_APP_GET_CLASS (app)->impl_start (app, error);
}

gboolean
csm_app_restart (CsmApp  *app,
                 GError **error)
{
        GTimeVal current_time;
        g_debug ("Re-starting app: %s", app->priv->id);

        g_get_current_time (&current_time);
        if (app->priv->last_restart_time.tv_sec > 0
            && (current_time.tv_sec - app->priv->last_restart_time.tv_sec) < _CSM_APP_RESPAWN_RATELIMIT_SECONDS) {
                g_warning ("App '%s' respawning too quickly", csm_app_peek_app_id (app));
                g_set_error (error,
                             CSM_APP_ERROR,
                             CSM_APP_ERROR_RESTART_LIMIT,
                             "Component '%s' crashing too quickly",
                             csm_app_peek_app_id (app));
                return FALSE;
        }
        app->priv->last_restart_time = current_time;

        return CSM_APP_GET_CLASS (app)->impl_restart (app, error);
}

gboolean
csm_app_stop (CsmApp  *app,
              GError **error)
{
        return CSM_APP_GET_CLASS (app)->impl_stop (app, error);
}

void
csm_app_registered (CsmApp *app)
{
        g_return_if_fail (CSM_IS_APP (app));

        g_signal_emit (app, signals[REGISTERED], 0);
}

int
csm_app_peek_autostart_delay (CsmApp *app)
{
        g_return_val_if_fail (CSM_IS_APP (app), FALSE);

        if (CSM_APP_GET_CLASS (app)->impl_peek_autostart_delay) {
                return CSM_APP_GET_CLASS (app)->impl_peek_autostart_delay (app);
        } else {
                return 0;
        }
}

void
csm_app_exited (CsmApp *app,
                guchar  exit_code)
{
        g_return_if_fail (CSM_IS_APP (app));

        g_signal_emit (app, signals[EXITED], 0, exit_code);
}

void
csm_app_died (CsmApp *app,
              int     signal)
{
        g_return_if_fail (CSM_IS_APP (app));

        g_signal_emit (app, signals[DIED], 0, signal);
}
