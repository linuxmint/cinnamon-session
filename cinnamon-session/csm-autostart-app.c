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

#include <config.h>

#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>

#include <glib.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include "csm-autostart-app.h"
#include "csm-util.h"

enum {
        AUTOSTART_LAUNCH_SPAWN = 0,
        AUTOSTART_LAUNCH_ACTIVATE
};

enum {
        CSM_CONDITION_NONE           = 0,
        CSM_CONDITION_IF_EXISTS      = 1,
        CSM_CONDITION_UNLESS_EXISTS  = 2,
        CSM_CONDITION_GSETTINGS      = 4,
        CSM_CONDITION_IF_SESSION     = 5,
        CSM_CONDITION_UNLESS_SESSION = 6,
        CSM_CONDITION_UNKNOWN        = 7
};

#define CSM_SESSION_CLIENT_DBUS_INTERFACE "org.cinnamon.SessionClient"

struct _CsmAutostartAppPrivate {
        char                 *desktop_filename;
        char                 *desktop_id;
        char                 *startup_id;

        GDesktopAppInfo      *app_info;
        /* provides defined in session definition */
        GSList               *session_provides;

        /* desktop file state */
        char                 *condition_string;
        gboolean              condition;
        gboolean              autorestart;
        int                   autostart_delay;
        char                 *working_dir;

        GFileMonitor         *condition_monitor;
        guint                 condition_notify_id;
        GSettings            *condition_settings;

        int                   launch_type;
        GPid                  pid;
        guint                 child_watch_id;
};

enum {
        CONDITION_CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_DESKTOP_FILENAME
};

static guint signals[LAST_SIGNAL] = { 0 };

#define CSM_AUTOSTART_APP_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), CSM_TYPE_AUTOSTART_APP, CsmAutostartAppPrivate))

static void csm_autostart_app_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (CsmAutostartApp, csm_autostart_app, CSM_TYPE_APP,
    G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, csm_autostart_app_initable_iface_init))

static void
csm_autostart_app_init (CsmAutostartApp *app)
{
        app->priv = CSM_AUTOSTART_APP_GET_PRIVATE (app);

        app->priv->pid = -1;
        app->priv->condition_monitor = NULL;
        app->priv->condition = FALSE;
        app->priv->autostart_delay = -1;
        app->priv->working_dir = NULL;
}

static gboolean
is_disabled (CsmApp *app)
{
        CsmAutostartAppPrivate *priv;
        const char *current_desktop;

        CsmManager *manager;
        manager = csm_manager_get ();

        if (csm_manager_get_app_is_blacklisted (manager, csm_app_peek_id (app)))
            return TRUE;

        priv = CSM_AUTOSTART_APP (app)->priv;

        /* CSM_AUTOSTART_APP_ENABLED_KEY key, used by old  */
        if (g_desktop_app_info_has_key (priv->app_info,
                                        CSM_AUTOSTART_APP_ENABLED_KEY) &&
            !g_desktop_app_info_get_boolean (priv->app_info,
                                             CSM_AUTOSTART_APP_ENABLED_KEY)) {
                g_debug ("app %s is disabled by " CSM_AUTOSTART_APP_ENABLED_KEY,
                         csm_app_peek_id (app));
                return TRUE;
        }

        /* Hidden key, used by autostart spec */
        if (g_desktop_app_info_get_is_hidden (priv->app_info)) {
                g_debug ("app %s is disabled by Hidden",
                         csm_app_peek_id (app));
                return TRUE;
        }

        /* Check OnlyShowIn/NotShowIn/TryExec */
        current_desktop = csm_util_get_current_desktop ();
        g_desktop_app_info_set_desktop_env (current_desktop);
        if (current_desktop != NULL &&
            !g_desktop_app_info_get_show_in (G_DESKTOP_APP_INFO (priv->app_info),
                                             "GNOME") &&
            !g_desktop_app_info_get_show_in (G_DESKTOP_APP_INFO (priv->app_info),
                                             current_desktop)) {
                        g_debug ("app %s not for %s",
                                 csm_app_peek_id (app), current_desktop);
                return TRUE;
        }

        /* Do not check AutostartCondition - this method is only to determine
         if the app is unconditionally disabled */

        return FALSE;
}

static gboolean
parse_condition_string (const char *condition_string,
                        guint      *condition_kindp,
                        char      **keyp)
{
        const char *space;
        const char *key;
        int         len;
        guint       kind;

        space = condition_string + strcspn (condition_string, " ");
        len = space - condition_string;
        key = space;
        while (isspace ((unsigned char)*key)) {
                key++;
        }

        kind = CSM_CONDITION_UNKNOWN;

        if (!g_ascii_strncasecmp (condition_string, "if-exists", len) && key) {
                kind = CSM_CONDITION_IF_EXISTS;
        } else if (!g_ascii_strncasecmp (condition_string, "unless-exists", len) && key) {
                kind = CSM_CONDITION_UNLESS_EXISTS;
        } else if (!g_ascii_strncasecmp (condition_string, "GSettings", len)) {
                kind = CSM_CONDITION_GSETTINGS;
        } else if (!g_ascii_strncasecmp (condition_string, "GNOME3", len)) {
                condition_string = key;
                space = condition_string + strcspn (condition_string, " ");
                len = space - condition_string;
                key = space;
                while (isspace ((unsigned char)*key)) {
                        key++;
                }
                if (!g_ascii_strncasecmp (condition_string, "if-session", len) && key) {
                        kind = CSM_CONDITION_IF_SESSION;
                } else if (!g_ascii_strncasecmp (condition_string, "unless-session", len) && key) {
                        kind = CSM_CONDITION_UNLESS_SESSION;
                }
        }

        if (kind == CSM_CONDITION_UNKNOWN) {
                key = NULL;
        }

        if (keyp != NULL) {
                *keyp = g_strdup (key);
        }

        if (condition_kindp != NULL) {
                *condition_kindp = kind;
        }

        return (kind != CSM_CONDITION_UNKNOWN);
}

static void
if_exists_condition_cb (GFileMonitor     *monitor,
                        GFile            *file,
                        GFile            *other_file,
                        GFileMonitorEvent event,
                        CsmApp           *app)
{
        CsmAutostartAppPrivate *priv;
        gboolean                condition = FALSE;

        priv = CSM_AUTOSTART_APP (app)->priv;

        switch (event) {
        case G_FILE_MONITOR_EVENT_CREATED:
                condition = TRUE;
                break;
        case G_FILE_MONITOR_EVENT_DELETED:
                condition = FALSE;
                break;
        default:
                /* Ignore any other monitor event */
                return;
        }

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static void
unless_exists_condition_cb (GFileMonitor     *monitor,
                            GFile            *file,
                            GFile            *other_file,
                            GFileMonitorEvent event,
                            CsmApp           *app)
{
        CsmAutostartAppPrivate *priv;
        gboolean                condition = FALSE;

        priv = CSM_AUTOSTART_APP (app)->priv;

        switch (event) {
        case G_FILE_MONITOR_EVENT_CREATED:
                condition = FALSE;
                break;
        case G_FILE_MONITOR_EVENT_DELETED:
                condition = TRUE;
                break;
        default:
                /* Ignore any other monitor event */
                return;
        }

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static void
gsettings_condition_cb (GSettings  *settings,
                        const char *key,
                        gpointer    user_data)
{
        CsmApp                 *app;
        CsmAutostartAppPrivate *priv;
        gboolean                condition;

        g_return_if_fail (CSM_IS_APP (user_data));

        app = CSM_APP (user_data);

        priv = CSM_AUTOSTART_APP (app)->priv;

        condition = g_settings_get_boolean (settings, key);

        g_debug ("CsmAutostartApp: app:%s condition changed condition:%d",
                 csm_app_peek_id (app),
                 condition);

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static gboolean
contained (const gchar * const *items,
           const gchar         *item)
{
        while (*items)
                if (strcmp (*items++, item) == 0)
                        return TRUE;
        return FALSE;
}

static gboolean
check_gsettings_schema_has_key (GSettingsSchema   *schema,
                                const gchar *key)
{
        gboolean good;
        gchar **keys;

        keys = g_settings_schema_list_keys (schema);
        good = contained ((const gchar **) keys, key);
        g_strfreev (keys);

        return good;
}

static gboolean
setup_gsettings_condition_monitor (CsmAutostartApp *app,
                                   const char      *key)
{
        GSettingsSchemaSource *source;
        GSettingsSchema *schema;
        GSettings *settings;
        char **elems;
        gboolean retval;
        char *signal;

        retval = FALSE;

        elems = g_strsplit (key, " ", 2);

        if (elems == NULL)
                goto out;

        if (elems[0] == NULL || elems[1] == NULL)
                goto out;

        source = g_settings_schema_source_get_default ();

        schema = g_settings_schema_source_lookup (source, elems[0], TRUE);

        if (schema == NULL)
                goto out;

        if (!check_gsettings_schema_has_key(schema, elems[1])) {
                g_warning ("Gsettings key %s %s could not be found!",
                           elems[0],
                           elems[1]);
                goto out;
        }

        settings = g_settings_new_full (schema, NULL, NULL);
        retval = g_settings_get_boolean (settings, elems[1]);
        g_settings_schema_unref (schema);

        signal = g_strdup_printf ("changed::%s", elems[1]);
        g_signal_connect (G_OBJECT (settings), signal,
                          G_CALLBACK (gsettings_condition_cb), app);
        g_free (signal);

        app->priv->condition_settings = settings;

out:
        g_strfreev (elems);

        return retval;
}

static void
if_session_condition_cb (GObject    *object,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
        CsmApp                 *app;
        CsmAutostartAppPrivate *priv;
        char                   *session_name;
        char                   *key;
        gboolean                condition;

        g_return_if_fail (CSM_IS_APP (user_data));

        app = CSM_APP (user_data);

        priv = CSM_AUTOSTART_APP (app)->priv;

        parse_condition_string (priv->condition_string, NULL, &key);

        g_object_get (object, "session-name", &session_name, NULL);
        condition = strcmp (session_name, key) == 0;
        g_free (session_name);

        g_free (key);

        g_debug ("CsmAutostartApp: app:%s condition changed condition:%d",
                 csm_app_peek_id (app),
                 condition);

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static void
unless_session_condition_cb (GObject    *object,
                             GParamSpec *pspec,
                             gpointer    user_data)
{
        CsmApp                 *app;
        CsmAutostartAppPrivate *priv;
        char                   *session_name;
        char                   *key;
        gboolean                condition;

        g_return_if_fail (CSM_IS_APP (user_data));

        app = CSM_APP (user_data);

        priv = CSM_AUTOSTART_APP (app)->priv;

        parse_condition_string (priv->condition_string, NULL, &key);

        g_object_get (object, "session-name", &session_name, NULL);
        condition = strcmp (session_name, key) != 0;
        g_free (session_name);

        g_free (key);

        g_debug ("CsmAutostartApp: app:%s condition changed condition:%d",
                 csm_app_peek_id (app),
                 condition);

        /* Emit only if the condition actually changed */
        if (condition != priv->condition) {
                priv->condition = condition;
                g_signal_emit (app, signals[CONDITION_CHANGED], 0, condition);
        }
}

static void
setup_condition_monitor (CsmAutostartApp *app)
{
        guint    kind;
        char    *key;
        gboolean res;
        gboolean disabled;

        if (app->priv->condition_monitor != NULL) {
                g_file_monitor_cancel (app->priv->condition_monitor);
        }

        if (app->priv->condition_string == NULL) {
                return;
        }

        /* if it is disabled outright there is no point in monitoring */
        if (is_disabled (CSM_APP (app))) {
                return;
        }

        key = NULL;
        res = parse_condition_string (app->priv->condition_string, &kind, &key);
        if (! res) {
                g_free (key);
                return;
        }

        if (key == NULL) {
                return;
        }

        if (kind == CSM_CONDITION_IF_EXISTS) {
                char  *file_path;
                GFile *file;

                file_path = g_build_filename (g_get_user_config_dir (), key, NULL);

                disabled = !g_file_test (file_path, G_FILE_TEST_EXISTS);

                file = g_file_new_for_path (file_path);
                app->priv->condition_monitor = g_file_monitor_file (file, 0, NULL, NULL);

                g_signal_connect (app->priv->condition_monitor, "changed",
                                  G_CALLBACK (if_exists_condition_cb),
                                  app);

                g_object_unref (file);
                g_free (file_path);
        } else if (kind == CSM_CONDITION_UNLESS_EXISTS) {
                char  *file_path;
                GFile *file;

                file_path = g_build_filename (g_get_user_config_dir (), key, NULL);

                disabled = g_file_test (file_path, G_FILE_TEST_EXISTS);

                file = g_file_new_for_path (file_path);
                app->priv->condition_monitor = g_file_monitor_file (file, 0, NULL, NULL);

                g_signal_connect (app->priv->condition_monitor, "changed",
                                  G_CALLBACK (unless_exists_condition_cb),
                                  app);

                g_object_unref (file);
                g_free (file_path);
        } else if (kind == CSM_CONDITION_GSETTINGS) {
                disabled = !setup_gsettings_condition_monitor (app, key);
        } else if (kind == CSM_CONDITION_IF_SESSION) {
                CsmManager *manager;
                char *session_name;

                /* get the singleton */
                manager = csm_manager_get ();

                g_object_get (manager, "session-name", &session_name, NULL);
                disabled = strcmp (session_name, key) != 0;

                g_signal_connect (manager, "notify::session-name",
                                  G_CALLBACK (if_session_condition_cb), app);
                g_free (session_name);
        } else if (kind == CSM_CONDITION_UNLESS_SESSION) {
                CsmManager *manager;
                char *session_name;

                /* get the singleton */
                manager = csm_manager_get ();

                g_object_get (manager, "session-name", &session_name, NULL);
                disabled = strcmp (session_name, key) == 0;

                g_signal_connect (manager, "notify::session-name",
                                  G_CALLBACK (unless_session_condition_cb), app);
                g_free (session_name);
        } else {
                disabled = TRUE;
        }

        g_free (key);

        if (disabled) {
                /* FIXME: cache the disabled value? */
        }
}

static gboolean
load_desktop_file (CsmAutostartApp *app)
{
        char    *dbus_name;
        char    *startup_id;
        char    *phase_str;
        int      phase;
        gboolean res;

        if (app->priv->app_info == NULL) {
                return FALSE;
        }

        phase_str = g_desktop_app_info_get_string (app->priv->app_info,
                                                   CSM_AUTOSTART_APP_PHASE_KEY);
        if (phase_str != NULL) {
                if (strcmp (phase_str, "EarlyInitialization") == 0) {
                        phase = CSM_MANAGER_PHASE_EARLY_INITIALIZATION;
                } else if (strcmp (phase_str, "PreDisplayServer") == 0) {
                        phase = CSM_MANAGER_PHASE_PRE_DISPLAY_SERVER;
                } else if (strcmp (phase_str, "DisplayServer") == 0) {
                        phase = CSM_MANAGER_PHASE_DISPLAY_SERVER;
                } else if (strcmp (phase_str, "Initialization") == 0) {
                        phase = CSM_MANAGER_PHASE_INITIALIZATION;
                } else if (strcmp (phase_str, "WindowManager") == 0) {
                        phase = CSM_MANAGER_PHASE_WINDOW_MANAGER;
                } else if (strcmp (phase_str, "Panel") == 0) {
                        phase = CSM_MANAGER_PHASE_PANEL;
                } else if (strcmp (phase_str, "Desktop") == 0) {
                        phase = CSM_MANAGER_PHASE_DESKTOP;
                } else {
                        phase = CSM_MANAGER_PHASE_APPLICATION;
                }

                g_free (phase_str);
        } else {
                phase = CSM_MANAGER_PHASE_APPLICATION;
        }

        dbus_name = g_desktop_app_info_get_string (app->priv->app_info,
                                                   CSM_AUTOSTART_APP_DBUS_NAME_KEY);
        if (dbus_name != NULL) {
                app->priv->launch_type = AUTOSTART_LAUNCH_ACTIVATE;
        } else {
                app->priv->launch_type = AUTOSTART_LAUNCH_SPAWN;
        }

        /* this must only be done on first load */
        switch (app->priv->launch_type) {
        case AUTOSTART_LAUNCH_SPAWN:
                startup_id =
                        g_desktop_app_info_get_string (app->priv->app_info,
                                                       CSM_AUTOSTART_APP_STARTUP_ID_KEY);

                if (startup_id == NULL) {
                        startup_id = csm_util_generate_startup_id ();
                }
                break;
        case AUTOSTART_LAUNCH_ACTIVATE:
                startup_id = g_strdup (dbus_name);
                break;
        default:
                g_assert_not_reached ();
        }

        res = g_desktop_app_info_has_key (app->priv->app_info,
                                          CSM_AUTOSTART_APP_AUTORESTART_KEY);
        if (res) {
                app->priv->autorestart = g_desktop_app_info_get_boolean (app->priv->app_info,
                                                                         CSM_AUTOSTART_APP_AUTORESTART_KEY);
        } else {
                app->priv->autorestart = FALSE;
        }

        g_free (app->priv->condition_string);
        app->priv->condition_string = g_desktop_app_info_get_string (app->priv->app_info,
                                                                   "AutostartCondition");
        setup_condition_monitor (app);

        if (phase == CSM_MANAGER_PHASE_APPLICATION) {
            /* Only accept an autostart delay for the application phase */
            const char *delay;
            delay = g_desktop_app_info_get_string (app->priv->app_info,
                                                   CSM_AUTOSTART_APP_DELAY_KEY);

            if (delay != NULL) {
                    app->priv->autostart_delay = strtol (delay, NULL, 10);

                    if (app->priv->autostart_delay < 0) {
                            g_warning ("Invalid autostart delay of %d for %s",
                                       app->priv->autostart_delay,
                                       csm_app_peek_id (CSM_APP (app)));
                            app->priv->autostart_delay = -1;
                    }
            }
        }

        g_object_set (app,
                      "phase", phase,
                      "startup-id", startup_id,
                      NULL);

        g_free (startup_id);
        g_free (dbus_name);

        return TRUE;
}

static gboolean
csm_autostart_app_initable_init (GInitable *initable,
                                 GCancellable *cancellable,
                                 GError  **error)
{
        CsmAutostartApp *app = CSM_AUTOSTART_APP (initable);

        g_return_val_if_fail (app->priv->desktop_filename != NULL, FALSE);

        g_clear_object (&app->priv->app_info);

        app->priv->app_info = g_desktop_app_info_new_from_filename (app->priv->desktop_filename);
        if (app->priv->app_info == NULL) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Could not parse desktop file %s or it references a not found TryExec binary", app->priv->desktop_id);
                return FALSE;
        }

        load_desktop_file (app);

        return TRUE;
}

static void
csm_autostart_app_set_desktop_filename (CsmAutostartApp *app,
                                        const char      *desktop_filename)
{
        if (app->priv->app_info != NULL) {
                g_clear_object (&app->priv->app_info);
                g_clear_pointer (&app->priv->desktop_id, g_free);
                g_clear_pointer (&app->priv->desktop_filename, g_free);
        }

        if (desktop_filename == NULL) {
                return;
        }

        app->priv->desktop_id = g_path_get_basename (desktop_filename);
        app->priv->desktop_filename = g_strdup (desktop_filename);
}

static void
csm_autostart_app_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        CsmAutostartApp *self;

        self = CSM_AUTOSTART_APP (object);

        switch (prop_id) {
        case PROP_DESKTOP_FILENAME:
                csm_autostart_app_set_desktop_filename (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_autostart_app_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        CsmAutostartApp *self;

        self = CSM_AUTOSTART_APP (object);

        switch (prop_id) {
        case PROP_DESKTOP_FILENAME:
                if (self->priv->app_info != NULL) {
                        g_value_set_string (value, g_desktop_app_info_get_filename (self->priv->app_info));
                } else {
                        g_value_set_string (value, NULL);
                }
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_autostart_app_dispose (GObject *object)
{
        CsmAutostartAppPrivate *priv;

        priv = CSM_AUTOSTART_APP (object)->priv;

        if (priv->startup_id) {
                g_free (priv->startup_id);
                priv->startup_id = NULL;
        }

        if (priv->session_provides) {
                g_slist_free_full (priv->session_provides, g_free);
                priv->session_provides = NULL;
        }

        if (priv->condition_string) {
                g_free (priv->condition_string);
                priv->condition_string = NULL;
        }

        if (priv->condition_settings) {
                g_object_unref (priv->condition_settings);
                priv->condition_settings = NULL;
        }

        g_clear_object (&priv->app_info);

        if (priv->desktop_id) {
                g_free (priv->desktop_id);
                priv->desktop_id = NULL;
        }

        g_clear_pointer (&priv->working_dir, g_free);

        if (priv->child_watch_id > 0) {
                g_source_remove (priv->child_watch_id);
                priv->child_watch_id = 0;
        }

        if (priv->condition_monitor) {
                g_file_monitor_cancel (priv->condition_monitor);
        }

        G_OBJECT_CLASS (csm_autostart_app_parent_class)->dispose (object);
}

static gboolean
is_running (CsmApp *app)
{
        CsmAutostartAppPrivate *priv;
        gboolean                is;

        priv = CSM_AUTOSTART_APP (app)->priv;

        /* is running if pid is still valid or
         * or a client is connected
         */
        /* FIXME: check client */
        is = (priv->pid != -1);

        return is;
}

static gboolean
is_conditionally_disabled (CsmApp *app)
{
        CsmAutostartAppPrivate *priv;
        gboolean                res;
        gboolean                disabled;
        char                   *key;
        guint                   kind;

        priv = CSM_AUTOSTART_APP (app)->priv;

        /* Check AutostartCondition */
        if (priv->condition_string == NULL) {
                return FALSE;
        }

        key = NULL;
        res = parse_condition_string (priv->condition_string, &kind, &key);
        if (! res) {
                g_free (key);
                return TRUE;
        }

        if (key == NULL) {
                return TRUE;
        }

        if (kind == CSM_CONDITION_IF_EXISTS) {
                char *file_path;

                file_path = g_build_filename (g_get_user_config_dir (), key, NULL);
                disabled = !g_file_test (file_path, G_FILE_TEST_EXISTS);
                g_free (file_path);
        } else if (kind == CSM_CONDITION_UNLESS_EXISTS) {
                char *file_path;

                file_path = g_build_filename (g_get_user_config_dir (), key, NULL);
                disabled = g_file_test (file_path, G_FILE_TEST_EXISTS);
                g_free (file_path);
        } else if (kind == CSM_CONDITION_GSETTINGS &&
                   priv->condition_settings != NULL) {
                char **elems;
                elems = g_strsplit (key, " ", 2);
                disabled = !g_settings_get_boolean (priv->condition_settings, elems[1]);
                g_strfreev (elems);
        } else if (kind == CSM_CONDITION_IF_SESSION) {
                CsmManager *manager;
                char *session_name;

                /* get the singleton */
                manager = csm_manager_get ();

                g_object_get (manager, "session-name", &session_name, NULL);
                disabled = strcmp (session_name, key) != 0;
                g_free (session_name);
        } else if (kind == CSM_CONDITION_UNLESS_SESSION) {
                CsmManager *manager;
                char *session_name;

                /* get the singleton */
                manager = csm_manager_get ();

                g_object_get (manager, "session-name", &session_name, NULL);
                disabled = strcmp (session_name, key) == 0;
                g_free (session_name);
        } else {
                disabled = TRUE;
        }

        /* Set initial condition */
        priv->condition = !disabled;

        g_free (key);

        return disabled;
}

static void
app_exited (GPid             pid,
            int              status,
            CsmAutostartApp *app)
{
        g_debug ("CsmAutostartApp: (pid:%d) done (%s:%d)",
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        g_spawn_close_pid (app->priv->pid);
        app->priv->pid = -1;
        app->priv->child_watch_id = 0;

        if (WIFEXITED (status)) {
                csm_app_exited (CSM_APP (app), WEXITSTATUS (status));
        } else if (WIFSIGNALED (status)) {
                csm_app_died (CSM_APP (app), WTERMSIG (status));
        }
}

static int
_signal_pid (int pid,
             int signal)
{
        int status;

        /* perhaps block sigchld */
        g_debug ("CsmAutostartApp: sending signal %d to process %d", signal, pid);
        errno = 0;
        status = kill (pid, signal);

        if (status < 0) {
                if (errno == ESRCH) {
                        g_warning ("Child process %d was already dead.",
                                   (int)pid);
                } else {
                        g_warning ("Couldn't kill child process %d: %s",
                                   pid,
                                   g_strerror (errno));
                }
        }

        /* perhaps unblock sigchld */

        return status;
}

static gboolean
autostart_app_stop_spawn (CsmAutostartApp *app,
                          GError         **error)
{
        int res;

        if (app->priv->pid < 1) {
                g_set_error (error,
                             CSM_APP_ERROR,
                             CSM_APP_ERROR_STOP,
                             "Not running");
                return FALSE;
        }

        res = _signal_pid (app->priv->pid, SIGTERM);
        if (res != 0) {
                g_set_error (error,
                             CSM_APP_ERROR,
                             CSM_APP_ERROR_STOP,
                             "Unable to stop: %s",
                             g_strerror (errno));
                return FALSE;
        }

        return TRUE;
}

static gboolean
autostart_app_stop_activate (CsmAutostartApp *app,
                             GError         **error)
{
        return TRUE;
}

static gboolean
csm_autostart_app_stop (CsmApp  *app,
                        GError **error)
{
        CsmAutostartApp *aapp;
        gboolean         ret;

        aapp = CSM_AUTOSTART_APP (app);

        g_return_val_if_fail (aapp->priv->app_info != NULL, FALSE);

        switch (aapp->priv->launch_type) {
        case AUTOSTART_LAUNCH_SPAWN:
                ret = autostart_app_stop_spawn (aapp, error);
                break;
        case AUTOSTART_LAUNCH_ACTIVATE:
                ret = autostart_app_stop_activate (aapp, error);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return ret;
}

static void
app_launched (GAppLaunchContext *ctx,
              GAppInfo    *appinfo,
              GVariant    *platform_data,
              gpointer     data)
{
        CsmAutostartApp *app = data;
        CsmAutostartAppPrivate *priv = app->priv;
        gint pid;
        gchar *sn_id;

        pid = 0;
        sn_id = NULL;

        g_variant_lookup (platform_data, "pid", "i", &pid);
        g_variant_lookup (platform_data, "startup-notification-id", "s", &sn_id);
        priv->pid = pid;
        priv->startup_id = sn_id;
}

static gboolean
autostart_app_start_spawn (CsmAutostartApp *app,
                           GError         **error)
{
        gboolean         success;
        GError          *local_error;
        const char      *startup_id;
        GAppLaunchContext *ctx;
        guint            handler;

        startup_id = csm_app_peek_startup_id (CSM_APP (app));
        g_assert (startup_id != NULL);

        g_debug ("CsmAutostartApp: starting %s: command=%s startup-id=%s", app->priv->desktop_id, g_app_info_get_commandline (G_APP_INFO (app->priv->app_info)), startup_id);

        g_free (app->priv->startup_id);
        local_error = NULL;

        ctx = g_app_launch_context_new ();

        if  (startup_id != NULL) {
            g_app_launch_context_setenv (ctx, "DESKTOP_AUTOSTART_ID", startup_id);
        }

        handler = g_signal_connect (ctx, "launched", G_CALLBACK (app_launched), app);
        success = g_desktop_app_info_launch_uris_as_manager (app->priv->app_info,
                                                             NULL,
                                                             ctx,
                                                             G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                                             NULL, NULL,
                                                             NULL, NULL,
                                                             &local_error);
        g_signal_handler_disconnect (ctx, handler);

        if (success) {
                if (app->priv->pid > 0) {
                        g_debug ("CsmAutostartApp: started pid:%d", app->priv->pid);
                        app->priv->child_watch_id = g_child_watch_add (app->priv->pid,
                                                                       (GChildWatchFunc)app_exited,
                                                                       app);
                }
        } else {
                g_set_error (error,
                             CSM_APP_ERROR,
                             CSM_APP_ERROR_START,
                             "Unable to start application: %s", local_error->message);
                g_error_free (local_error);
        }

        return success;
}

static void
start_notify (GObject *source,
              GAsyncResult *result,
              CsmAutostartApp *app)
{
        GError  *error;

        error = NULL;

        g_dbus_connection_call_finish (G_DBUS_CONNECTION (source),
                                       result,
                                       &error);

        if (error != NULL) {
                g_warning ("CsmAutostartApp: Error starting application: %s", error->message);
                g_error_free (error);
        } else {
                g_debug ("CsmAutostartApp: Started application %s", app->priv->desktop_id);
        }
}

static gboolean
autostart_app_start_activate (CsmAutostartApp  *app,
                              GError          **error)
{
        const char      *name;
        char            *path;
        char            *arguments;
        GDBusConnection *bus;
        GError          *local_error;

        local_error = NULL;
        bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &local_error);
        if (bus == NULL) {
                if (local_error != NULL) {
                        g_warning ("error getting session bus: %s", local_error->message);
                }
                g_propagate_error (error, local_error);
                return FALSE;
        }

        name = csm_app_peek_startup_id (CSM_APP (app));
        g_assert (name != NULL);

        path = g_desktop_app_info_get_string (app->priv->app_info,
                                              CSM_AUTOSTART_APP_DBUS_PATH_KEY);
        if (path == NULL) {
                /* just pick one? */
                path = g_strdup ("/");
        }

        arguments = g_desktop_app_info_get_string (app->priv->app_info,
                                                   CSM_AUTOSTART_APP_DBUS_ARGS_KEY);

        g_dbus_connection_call (bus,
                                name,
                                path,
                                CSM_SESSION_CLIENT_DBUS_INTERFACE,
                                "Start",
                                g_variant_new ("(s)", arguments),
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1, NULL,
                                (GAsyncReadyCallback) start_notify, app);

        g_object_unref (bus);

        return TRUE;
}

static gboolean
csm_autostart_app_start (CsmApp  *app,
                         GError **error)
{
        CsmAutostartApp *aapp;
        gboolean         ret;

        aapp = CSM_AUTOSTART_APP (app);

        g_return_val_if_fail (aapp->priv->app_info != NULL, FALSE);

        switch (aapp->priv->launch_type) {
        case AUTOSTART_LAUNCH_SPAWN:
                ret = autostart_app_start_spawn (aapp, error);
                break;
        case AUTOSTART_LAUNCH_ACTIVATE:
                ret = autostart_app_start_activate (aapp, error);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return ret;
}

static gboolean
csm_autostart_app_restart (CsmApp  *app,
                           GError **error)
{
        GError  *local_error;
        gboolean res;

        /* ignore stop errors - it is fine if it is already stopped */
        local_error = NULL;
        res = csm_app_stop (app, &local_error);
        if (! res) {
                g_debug ("CsmAutostartApp: Couldn't stop app: %s", local_error->message);
                g_error_free (local_error);
        }

        res = csm_app_start (app, &local_error);
        if (! res) {
                g_propagate_error (error, local_error);
                return FALSE;
        }

        return TRUE;
}

static gboolean
csm_autostart_app_provides (CsmApp     *app,
                            const char *service)
{
        gchar           *provides_str;
        char           **provides;
        gsize            i;
        GSList          *l;
        CsmAutostartApp *aapp;

        g_return_val_if_fail (CSM_IS_APP (app), FALSE);

        aapp = CSM_AUTOSTART_APP (app);

        if (aapp->priv->app_info == NULL) {
                return FALSE;
        }

        for (l = aapp->priv->session_provides; l != NULL; l = l->next) {
                if (!strcmp (l->data, service))
                        return TRUE;
        }

        provides_str = g_desktop_app_info_get_string (aapp->priv->app_info,
                                                      CSM_AUTOSTART_APP_PROVIDES_KEY);
        if (!provides_str) {
                return FALSE;
        }
        provides = g_strsplit (provides_str, ";", -1);
        g_free (provides_str);

        for (i = 0; provides[i]; i++) {
                if (!strcmp (provides[i], service)) {
                        g_strfreev (provides);
                        return TRUE;
                }
        }

        g_strfreev (provides);

        return FALSE;
}

static char **
csm_autostart_app_get_provides (CsmApp *app)
{
        CsmAutostartApp  *aapp;
        gchar            *provides_str;
        char            **provides;
        gsize             provides_len;
        char            **result;
        gsize             result_len;
        int               i;
        GSList           *l;

        g_return_val_if_fail (CSM_IS_APP (app), NULL);

        aapp = CSM_AUTOSTART_APP (app);

        if (aapp->priv->app_info == NULL) {
                return NULL;
        }

        provides_str = g_desktop_app_info_get_string (aapp->priv->app_info,
                                                      CSM_AUTOSTART_APP_PROVIDES_KEY);

        if (provides_str == NULL) {
                return NULL;
        }

        provides = g_strsplit (provides_str, ";", -1);
        provides_len = g_strv_length (provides);
        g_free (provides_str);

        if (!aapp->priv->session_provides) {
                return provides;
        }

        result_len = provides_len + g_slist_length (aapp->priv->session_provides);
        result = g_new (char *, result_len + 1); /* including last NULL */

        for (i = 0; provides[i] != NULL; i++)
                result[i] = provides[i];
        g_free (provides);

        for (l = aapp->priv->session_provides; l != NULL; l = l->next, i++)
                result[i] = g_strdup (l->data);

        result[i] = NULL;

        g_assert (i == result_len);

        return result;
}

void
csm_autostart_app_add_provides (CsmAutostartApp *aapp,
                                const char      *provides)
{
        g_return_if_fail (CSM_IS_AUTOSTART_APP (aapp));

        aapp->priv->session_provides = g_slist_prepend (aapp->priv->session_provides,
                                                        g_strdup (provides));
}

static gboolean
csm_autostart_app_has_autostart_condition (CsmApp     *app,
                                           const char *condition)
{
        CsmAutostartApp *aapp;

        g_return_val_if_fail (CSM_IS_APP (app), FALSE);
        g_return_val_if_fail (condition != NULL, FALSE);

        aapp = CSM_AUTOSTART_APP (app);

        if (aapp->priv->condition_string == NULL) {
                return FALSE;
        }

        if (strcmp (aapp->priv->condition_string, condition) == 0) {
                return TRUE;
        }

        return FALSE;
}

static gboolean
csm_autostart_app_get_autorestart (CsmApp *app)
{
        gboolean res;
        gboolean autorestart;

        if (CSM_AUTOSTART_APP (app)->priv->app_info == NULL) {
                return FALSE;
        }

        autorestart = FALSE;

        res = g_desktop_app_info_has_key (CSM_AUTOSTART_APP (app)->priv->app_info,
                                          CSM_AUTOSTART_APP_AUTORESTART_KEY);
        if (res) {
                autorestart = g_desktop_app_info_get_boolean (CSM_AUTOSTART_APP (app)->priv->app_info,
                                                              CSM_AUTOSTART_APP_AUTORESTART_KEY);
        }

        return autorestart;
}

static const char *
csm_autostart_app_get_app_id (CsmApp *app)
{
        const char *location;
        const char *slash;

        if (CSM_AUTOSTART_APP (app)->priv->app_info == NULL) {
                return NULL;
        }

        location = g_desktop_app_info_get_filename (CSM_AUTOSTART_APP (app)->priv->app_info);

        slash = strrchr (location, '/');
        if (slash != NULL) {
                return slash + 1;
        } else {
                return location;
        }
}

static int
csm_autostart_app_peek_autostart_delay (CsmApp *app)
{
        CsmAutostartApp *aapp = CSM_AUTOSTART_APP (app);

        return aapp->priv->autostart_delay;
}

static void
csm_autostart_app_initable_iface_init (GInitableIface  *iface)
{
        iface->init = csm_autostart_app_initable_init;
}

static void
csm_autostart_app_class_init (CsmAutostartAppClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        CsmAppClass  *app_class = CSM_APP_CLASS (klass);

        object_class->set_property = csm_autostart_app_set_property;
        object_class->get_property = csm_autostart_app_get_property;
        object_class->dispose = csm_autostart_app_dispose;

        app_class->impl_is_disabled = is_disabled;
        app_class->impl_is_conditionally_disabled = is_conditionally_disabled;
        app_class->impl_is_running = is_running;
        app_class->impl_start = csm_autostart_app_start;
        app_class->impl_restart = csm_autostart_app_restart;
        app_class->impl_stop = csm_autostart_app_stop;
        app_class->impl_provides = csm_autostart_app_provides;
        app_class->impl_get_provides = csm_autostart_app_get_provides;
        app_class->impl_has_autostart_condition = csm_autostart_app_has_autostart_condition;
        app_class->impl_get_app_id = csm_autostart_app_get_app_id;
        app_class->impl_get_autorestart = csm_autostart_app_get_autorestart;
        app_class->impl_peek_autostart_delay = csm_autostart_app_peek_autostart_delay;

        g_object_class_install_property (object_class,
                                         PROP_DESKTOP_FILENAME,
                                         g_param_spec_string ("desktop-filename",
                                                              "Desktop filename",
                                                              "Freedesktop .desktop file",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        signals[CONDITION_CHANGED] =
                g_signal_new ("condition-changed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmAutostartAppClass, condition_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);

        g_type_class_add_private (object_class, sizeof (CsmAutostartAppPrivate));
}

CsmApp *
csm_autostart_app_new (const char *desktop_file)
{
        CsmAutostartApp *app;
        GError *error;

        error = NULL;

        app = g_initable_new (CSM_TYPE_AUTOSTART_APP,
                              NULL, &error,
                              "desktop-filename", desktop_file,
                              NULL);

        if (error != NULL) {
                g_warning ("Could not read %s: %s", desktop_file, error->message);
                g_clear_error (&error);
        }

        return CSM_APP (app);
}
