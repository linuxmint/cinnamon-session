/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <gtk/gtk.h> /* for logout dialog */

#include "gsm-manager.h"
#include "gsm-manager-glue.h"

#include "gsm-store.h"
#include "gsm-inhibitor.h"
#include "gsm-presence.h"
#include "gsm-shell.h"

#include "gsm-xsmp-client.h"
#include "gsm-dbus-client.h"

#include "gsm-autostart-app.h"

#include "gsm-util.h"
#include "gdm.h"
#include "gsm-logout-dialog.h"
#include "gsm-fail-whale-dialog.h"
#include "gsm-icon-names.h"
#include "gsm-inhibit-dialog.h"
#include "gsm-system.h"
#include "gsm-session-save.h"
#include "gsm-shell-extensions.h"

#define GSM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_MANAGER, GsmManagerPrivate))

#define GSM_MANAGER_DBUS_PATH "/org/gnome/SessionManager"
#define GSM_MANAGER_DBUS_NAME "org.gnome.SessionManager"

/* Probably about the longest amount of time someone could reasonably
 * want to wait, at least for something happening more than once.
 * We can get deployed on very slow media though like CDROM devices,
 * often with complex stacking/compressing filesystems on top, which
 * is not a recipie for speed.   Particularly now that we throw up
 * a fail whale if required components don't show up quickly enough,
 * let's make this fairly long.
 */
#define GSM_MANAGER_PHASE_TIMEOUT 30 /* seconds */

#define GDM_FLEXISERVER_COMMAND "gdmflexiserver"
#define GDM_FLEXISERVER_ARGS    "--startnew Standard"

#define SESSION_SCHEMA            "org.gnome.desktop.session"
#define KEY_IDLE_DELAY            "idle-delay"
#define KEY_SESSION_NAME          "session-name"

#define GSM_MANAGER_SCHEMA        "org.gnome.SessionManager"
#define KEY_AUTOSAVE              "auto-save-session"
#define KEY_LOGOUT_PROMPT         "logout-prompt"
#define KEY_SHOW_FALLBACK_WARNING "show-fallback-warning"

#define SCREENSAVER_SCHEMA        "org.gnome.desktop.screensaver"
#define KEY_SLEEP_LOCK            "lock-enabled"

#define LOCKDOWN_SCHEMA           "org.gnome.desktop.lockdown"
#define KEY_DISABLE_LOG_OUT       "disable-log-out"
#define KEY_DISABLE_USER_SWITCHING "disable-user-switching"

static void app_registered (GsmApp     *app, GsmManager *manager);

typedef enum
{
        GSM_MANAGER_LOGOUT_NONE,
        GSM_MANAGER_LOGOUT_LOGOUT,
        GSM_MANAGER_LOGOUT_REBOOT,
        GSM_MANAGER_LOGOUT_REBOOT_INTERACT,
        GSM_MANAGER_LOGOUT_REBOOT_GDM,
        GSM_MANAGER_LOGOUT_SHUTDOWN,
        GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT,
        GSM_MANAGER_LOGOUT_SHUTDOWN_GDM
} GsmManagerLogoutType;

struct GsmManagerPrivate
{
        gboolean                failsafe;
        GsmStore               *clients;
        GsmStore               *inhibitors;
        GsmStore               *apps;
        GsmPresence            *presence;

        char                   *session_name;
        gboolean                is_fallback_session : 1;

        /* Current status */
        GsmManagerPhase         phase;
        guint                   phase_timeout_id;
        GSList                 *required_apps;
        GSList                 *pending_apps;
        GsmManagerLogoutMode    logout_mode;
        GSList                 *query_clients;
        guint                   query_timeout_id;
        /* This is used for GSM_MANAGER_PHASE_END_SESSION only at the moment,
         * since it uses a sublist of all running client that replied in a
         * specific way */
        GSList                 *next_query_clients;
        /* This is the action that will be done just before we exit */
        GsmManagerLogoutType    logout_type;

        GtkWidget              *inhibit_dialog;

        /* List of clients which were disconnected due to disabled condition
         * and shouldn't be automatically restarted */
        GSList                 *condition_clients;

        GSettings              *settings;
        GSettings              *session_settings;
        GSettings              *screensaver_settings;
        GSettings              *lockdown_settings;

        GsmSystem              *system;
        DBusGProxy             *bus_proxy;
        DBusGConnection        *connection;
        gboolean                dbus_disconnected : 1;

        GsmShell               *shell;
        guint                   shell_end_session_dialog_canceled_id;
        guint                   shell_end_session_dialog_open_failed_id;
        guint                   shell_end_session_dialog_confirmed_logout_id;
        guint                   shell_end_session_dialog_confirmed_shutdown_id;
        guint                   shell_end_session_dialog_confirmed_reboot_id;
};

enum {
        PROP_0,
        PROP_CLIENT_STORE,
        PROP_SESSION_NAME,
        PROP_FALLBACK,
        PROP_FAILSAFE
};

enum {
        PHASE_CHANGED,
        CLIENT_ADDED,
        CLIENT_REMOVED,
        INHIBITOR_ADDED,
        INHIBITOR_REMOVED,
        SESSION_RUNNING,
        SESSION_OVER,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

static void     gsm_manager_class_init  (GsmManagerClass *klass);
static void     gsm_manager_init        (GsmManager      *manager);
static void     gsm_manager_finalize    (GObject         *object);

static gboolean auto_save_is_enabled (GsmManager *manager);
static void     maybe_save_session   (GsmManager *manager);

static gboolean _log_out_is_locked_down     (GsmManager *manager);
static gboolean _switch_user_is_locked_down (GsmManager *manager);

static void     _handle_client_end_session_response (GsmManager *manager,
                                                     GsmClient  *client,
                                                     gboolean    is_ok,
                                                     gboolean    do_last,
                                                     gboolean    cancel,
                                                     const char *reason);
static void     show_shell_end_session_dialog (GsmManager                   *manager,
                                               GsmShellEndSessionDialogType  type);
static gpointer manager_object = NULL;

G_DEFINE_TYPE (GsmManager, gsm_manager, G_TYPE_OBJECT)

GQuark
gsm_manager_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gsm_manager_error");
        }

        return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
gsm_manager_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (GSM_MANAGER_ERROR_GENERAL, "GeneralError"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION, "NotInInitialization"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_NOT_IN_RUNNING, "NotInRunning"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_ALREADY_REGISTERED, "AlreadyRegistered"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_NOT_REGISTERED, "NotRegistered"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_INVALID_OPTION, "InvalidOption"),
                        ENUM_ENTRY (GSM_MANAGER_ERROR_LOCKED_DOWN, "LockedDown"),
                        { 0, 0, 0 }
                };

                g_assert (GSM_MANAGER_NUM_ERRORS == G_N_ELEMENTS (values) - 1);

                etype = g_enum_register_static ("GsmManagerError", values);
        }

        return etype;
}

static gboolean
start_app_or_warn (GsmManager *manager,
                   GsmApp     *app)
{
        gboolean res;
        GError *error = NULL;

        g_debug ("GsmManager: starting app '%s'", gsm_app_peek_id (app));

        res = gsm_app_start (app, &error);
        if (error != NULL) {
                g_warning ("Failed to start app: %s", error->message);
                g_clear_error (&error);
        }
        return res;
}

static gboolean
is_app_required (GsmManager *manager,
                 GsmApp     *app)
{
        return g_slist_find (manager->priv->required_apps, app) != NULL;
}

static void
on_required_app_failure (GsmManager  *manager,
                         GsmApp      *app)
{
        const gchar *app_id;
        gboolean allow_logout;
        GsmShellExtensions *extensions;

        app_id = gsm_app_peek_app_id (app);

        if (g_str_equal (app_id, "gnome-shell.desktop")) {
                extensions = g_object_new (GSM_TYPE_SHELL_EXTENSIONS, NULL);
                gsm_shell_extensions_disable_all (extensions);
        } else {
                extensions = NULL;
        }

        if (gsm_system_is_login_session (manager->priv->system)) {
                allow_logout = FALSE;
        } else {
                allow_logout = !_log_out_is_locked_down (manager);
        }

        gsm_fail_whale_dialog_we_failed (FALSE,
                                         allow_logout,
                                         extensions);
}

static gboolean
_debug_client (const char *id,
               GsmClient  *client,
               GsmManager *manager)
{
        g_debug ("GsmManager: Client %s", gsm_client_peek_id (client));
        return FALSE;
}

static void
debug_clients (GsmManager *manager)
{
        gsm_store_foreach (manager->priv->clients,
                           (GsmStoreFunc)_debug_client,
                           manager);
}

static gboolean
_debug_inhibitor (const char    *id,
                  GsmInhibitor  *inhibitor,
                  GsmManager    *manager)
{
        g_debug ("GsmManager: Inhibitor app:%s client:%s bus-name:%s reason:%s",
                 gsm_inhibitor_peek_app_id (inhibitor),
                 gsm_inhibitor_peek_client_id (inhibitor),
                 gsm_inhibitor_peek_bus_name (inhibitor),
                 gsm_inhibitor_peek_reason (inhibitor));
        return FALSE;
}

static void
debug_inhibitors (GsmManager *manager)
{
        gsm_store_foreach (manager->priv->inhibitors,
                           (GsmStoreFunc)_debug_inhibitor,
                           manager);
}

static gboolean
_find_by_cookie (const char   *id,
                 GsmInhibitor *inhibitor,
                 guint        *cookie_ap)
{
        guint cookie_b;

        cookie_b = gsm_inhibitor_peek_cookie (inhibitor);

        return (*cookie_ap == cookie_b);
}

static gboolean
_client_has_startup_id (const char *id,
                        GsmClient  *client,
                        const char *startup_id_a)
{
        const char *startup_id_b;

        startup_id_b = gsm_client_peek_startup_id (client);
        if (IS_STRING_EMPTY (startup_id_b)) {
                return FALSE;
        }

        return (strcmp (startup_id_a, startup_id_b) == 0);
}

static void
app_condition_changed (GsmApp     *app,
                       gboolean    condition,
                       GsmManager *manager)
{
        GsmClient *client;

        g_debug ("GsmManager: app:%s condition changed condition:%d",
                 gsm_app_peek_id (app),
                 condition);

        client = (GsmClient *)gsm_store_find (manager->priv->clients,
                                              (GsmStoreFunc)_client_has_startup_id,
                                              (char *)gsm_app_peek_startup_id (app));

        if (condition) {
                if (!gsm_app_is_running (app) && client == NULL) {
                        start_app_or_warn (manager, app);
                } else {
                        g_debug ("GsmManager: not starting - app still running '%s'", gsm_app_peek_id (app));
                }
        } else {
                GError  *error;
                gboolean res;

                if (client != NULL) {
                        /* Kill client in case condition if false and make sure it won't
                         * be automatically restarted by adding the client to
                         * condition_clients */
                        manager->priv->condition_clients =
                                g_slist_prepend (manager->priv->condition_clients, client);

                        g_debug ("GsmManager: stopping client %s for app", gsm_client_peek_id (client));

                        error = NULL;
                        res = gsm_client_stop (client, &error);
                        if (! res) {
                                g_warning ("Not able to stop app client from its condition: %s",
                                           error->message);
                                g_error_free (error);
                        }
                } else {
                        g_debug ("GsmManager: stopping app %s", gsm_app_peek_id (app));

                        /* If we don't have a client then we should try to kill the app */
                        error = NULL;
                        res = gsm_app_stop (app, &error);
                        if (! res) {
                                g_warning ("Not able to stop app from its condition: %s",
                                           error->message);
                                g_error_free (error);
                        }
                }
        }
}

static const char *
phase_num_to_name (guint phase)
{
        const char *name;

        switch (phase) {
        case GSM_MANAGER_PHASE_STARTUP:
                name = "STARTUP";
                break;
        case GSM_MANAGER_PHASE_INITIALIZATION:
                name = "INITIALIZATION";
                break;
        case GSM_MANAGER_PHASE_WINDOW_MANAGER:
                name = "WINDOW_MANAGER";
                break;
        case GSM_MANAGER_PHASE_PANEL:
                name = "PANEL";
                break;
        case GSM_MANAGER_PHASE_DESKTOP:
                name = "DESKTOP";
                break;
        case GSM_MANAGER_PHASE_APPLICATION:
                name = "APPLICATION";
                break;
        case GSM_MANAGER_PHASE_RUNNING:
                name = "RUNNING";
                break;
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                name = "QUERY_END_SESSION";
                break;
        case GSM_MANAGER_PHASE_END_SESSION:
                name = "END_SESSION";
                break;
        case GSM_MANAGER_PHASE_EXIT:
                name = "EXIT";
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return name;
}

static void start_phase (GsmManager *manager);

static void
quit_request_completed (GsmSystem *system,
                        GError    *error,
                        gpointer   user_data)
{
        GdmLogoutAction fallback_action = GPOINTER_TO_INT (user_data);

        if (error != NULL) {
                gdm_set_logout_action (fallback_action);
        }

        gtk_main_quit ();
}

static void
gsm_manager_quit (GsmManager *manager)
{
        /* See the comment in request_reboot() for some more details about how
         * this works. */

        switch (manager->priv->logout_type) {
        case GSM_MANAGER_LOGOUT_LOGOUT:
                gtk_main_quit ();
                break;
        case GSM_MANAGER_LOGOUT_REBOOT:
        case GSM_MANAGER_LOGOUT_REBOOT_INTERACT:
                gdm_set_logout_action (GDM_LOGOUT_ACTION_NONE);

                g_signal_connect (manager->priv->system,
                                  "request-completed",
                                  G_CALLBACK (quit_request_completed),
                                  GINT_TO_POINTER (GDM_LOGOUT_ACTION_REBOOT));
                gsm_system_attempt_restart (manager->priv->system);
                break;
        case GSM_MANAGER_LOGOUT_REBOOT_GDM:
                gdm_set_logout_action (GDM_LOGOUT_ACTION_REBOOT);
                gtk_main_quit ();
                break;
        case GSM_MANAGER_LOGOUT_SHUTDOWN:
        case GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:
                gdm_set_logout_action (GDM_LOGOUT_ACTION_NONE);

                g_signal_connect (manager->priv->system,
                                  "request-completed",
                                  G_CALLBACK (quit_request_completed),
                                  GINT_TO_POINTER (GDM_LOGOUT_ACTION_SHUTDOWN));
                gsm_system_attempt_stop (manager->priv->system);
                break;
        case GSM_MANAGER_LOGOUT_SHUTDOWN_GDM:
                gdm_set_logout_action (GDM_LOGOUT_ACTION_SHUTDOWN);
                gtk_main_quit ();
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
end_phase (GsmManager *manager)
{
        gboolean start_next_phase = TRUE;

        g_debug ("GsmManager: ending phase %s\n",
                 phase_num_to_name (manager->priv->phase));

        g_slist_free (manager->priv->pending_apps);
        manager->priv->pending_apps = NULL;

        g_slist_free (manager->priv->query_clients);
        manager->priv->query_clients = NULL;

        g_slist_free (manager->priv->next_query_clients);
        manager->priv->next_query_clients = NULL;

        if (manager->priv->query_timeout_id > 0) {
                g_source_remove (manager->priv->query_timeout_id);
                manager->priv->query_timeout_id = 0;
        }
        if (manager->priv->phase_timeout_id > 0) {
                g_source_remove (manager->priv->phase_timeout_id);
                manager->priv->phase_timeout_id = 0;
        }

        switch (manager->priv->phase) {
        case GSM_MANAGER_PHASE_STARTUP:
        case GSM_MANAGER_PHASE_INITIALIZATION:
        case GSM_MANAGER_PHASE_WINDOW_MANAGER:
        case GSM_MANAGER_PHASE_PANEL:
        case GSM_MANAGER_PHASE_DESKTOP:
        case GSM_MANAGER_PHASE_APPLICATION:
                break;
        case GSM_MANAGER_PHASE_RUNNING:
                if (_log_out_is_locked_down (manager)) {
                        g_warning ("Unable to logout: Logout has been locked down");
                        start_next_phase = FALSE;
                }
                break;
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                break;
        case GSM_MANAGER_PHASE_END_SESSION:
                maybe_save_session (manager);
                break;
        case GSM_MANAGER_PHASE_EXIT:
                start_next_phase = FALSE;
                gsm_manager_quit (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        if (start_next_phase) {
                manager->priv->phase++;
                start_phase (manager);
        }
}

static void
app_event_during_startup (GsmManager *manager,
                          GsmApp     *app)
{
        if (!(manager->priv->phase < GSM_MANAGER_PHASE_APPLICATION))
                return;

        manager->priv->pending_apps = g_slist_remove (manager->priv->pending_apps, app);

        if (manager->priv->pending_apps == NULL) {
                if (manager->priv->phase_timeout_id > 0) {
                        g_source_remove (manager->priv->phase_timeout_id);
                        manager->priv->phase_timeout_id = 0;
                }

                end_phase (manager);
        }
}

static void
_restart_app (GsmManager *manager,
              GsmApp     *app)
{
        GError *error = NULL;

        if (!gsm_app_restart (app, &error)) {
                if (is_app_required (manager, app)) {
                        on_required_app_failure (manager, app);
                } else {
                        g_warning ("Error on restarting session managed app: %s", error->message);
                }
                g_clear_error (&error);

                app_event_during_startup (manager, app);
        }
}

static void
app_died (GsmApp     *app,
          int         signal,
          GsmManager *manager)
{
        g_warning ("Application '%s' killed by signal %d", gsm_app_peek_app_id (app), signal);

        if (gsm_app_peek_autorestart (app)) {
                g_debug ("Component '%s' is autorestart, ignoring died signal",
                         gsm_app_peek_app_id (app));
                return;
        }

        _restart_app (manager, app);

        /* For now, we don't do anything with crashes from
         * non-required apps after they hit the restart limit.
         *
         * Note that both required and not-required apps will be
         * caught by ABRT/apport type infrastructure, and it'd be
         * better to pick up the crash from there and do something
         * un-intrusive about it generically.
         */
}

static void
app_exited (GsmApp     *app,
            guchar      exit_code,
            GsmManager *manager)
{
        g_debug ("App %s exited with %d", gsm_app_peek_app_id (app), exit_code);

        /* Consider that non-success exit status means "crash" for required components */
        if (exit_code != 0 && is_app_required (manager, app)) {
                if (gsm_app_peek_autorestart (app)) {
                        g_debug ("Component '%s' is autorestart, ignoring non-successful exit",
                                 gsm_app_peek_app_id (app));
                        return;
                }

                _restart_app (manager, app);
        } else {
                app_event_during_startup (manager, app);
        }
}

static void
app_registered (GsmApp     *app,
                GsmManager *manager)
{
        g_debug ("App %s registered", gsm_app_peek_app_id (app));

        app_event_during_startup (manager, app);
}

static gboolean
_client_failed_to_stop (const char *id,
                        GsmClient  *client,
                        gpointer    user_data)
{
        g_debug ("GsmManager: client failed to stop: %s, %s", gsm_client_peek_id (client), gsm_client_peek_app_id (client));
        return FALSE;
}

static gboolean
on_phase_timeout (GsmManager *manager)
{
        GSList *a;

        manager->priv->phase_timeout_id = 0;

        switch (manager->priv->phase) {
        case GSM_MANAGER_PHASE_STARTUP:
        case GSM_MANAGER_PHASE_INITIALIZATION:
        case GSM_MANAGER_PHASE_WINDOW_MANAGER:
        case GSM_MANAGER_PHASE_PANEL:
        case GSM_MANAGER_PHASE_DESKTOP:
        case GSM_MANAGER_PHASE_APPLICATION:
                for (a = manager->priv->pending_apps; a; a = a->next) {
                        GsmApp *app = a->data;
                        g_warning ("Application '%s' failed to register before timeout",
                                   gsm_app_peek_app_id (app));
                        if (is_app_required (manager, app))
                                on_required_app_failure (manager, app);
                }
                break;
        case GSM_MANAGER_PHASE_RUNNING:
                break;
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
        case GSM_MANAGER_PHASE_END_SESSION:
                break;
        case GSM_MANAGER_PHASE_EXIT:
                gsm_store_foreach (manager->priv->clients,
                                   (GsmStoreFunc)_client_failed_to_stop,
                                   NULL);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        end_phase (manager);

        return FALSE;
}

static gboolean
_start_app (const char *id,
            GsmApp     *app,
            GsmManager *manager)
{
        if (gsm_app_peek_phase (app) != manager->priv->phase) {
                goto out;
        }

        /* Keep track of app autostart condition in order to react
         * accordingly in the future. */
        g_signal_connect (app,
                          "condition-changed",
                          G_CALLBACK (app_condition_changed),
                          manager);

        if (gsm_app_peek_is_disabled (app)
            || gsm_app_peek_is_conditionally_disabled (app)) {
                g_debug ("GsmManager: Skipping disabled app: %s", id);
                goto out;
        }

        if (!start_app_or_warn (manager, app))
                goto out;

        if (manager->priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                /* Historical note - apparently,
                 * e.g. gnome-settings-daemon used to "daemonize", and
                 * so gnome-session assumes process exit means "ok
                 * we're done".  Of course this is broken, we don't
                 * even distinguish between exit code 0 versus not-0,
                 * nor do we have any metadata which tells us a
                 * process is going to "daemonize" or not (and
                 * basically nothing should be anyways).
                 */
                g_signal_connect (app,
                                  "exited",
                                  G_CALLBACK (app_exited),
                                  manager);
                g_signal_connect (app,
                                  "registered",
                                  G_CALLBACK (app_registered),
                                  manager);
                g_signal_connect (app,
                                  "died",
                                  G_CALLBACK (app_died),
                                  manager);
                manager->priv->pending_apps = g_slist_prepend (manager->priv->pending_apps, app);
        }
 out:
        return FALSE;
}

static void
do_phase_startup (GsmManager *manager)
{
        gsm_store_foreach (manager->priv->apps,
                           (GsmStoreFunc)_start_app,
                           manager);

        if (manager->priv->pending_apps != NULL) {
                if (manager->priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                        manager->priv->phase_timeout_id = g_timeout_add_seconds (GSM_MANAGER_PHASE_TIMEOUT,
                                                                                 (GSourceFunc)on_phase_timeout,
                                                                                 manager);
                }
        } else {
                end_phase (manager);
        }
}

typedef struct {
        GsmManager *manager;
        guint       flags;
} ClientEndSessionData;


static gboolean
_client_end_session (GsmClient            *client,
                     ClientEndSessionData *data)
{
        gboolean ret;
        GError  *error;

        error = NULL;
        ret = gsm_client_end_session (client, data->flags, &error);
        if (! ret) {
                g_warning ("Unable to query client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("GsmManager: adding client to end-session clients: %s", gsm_client_peek_id (client));
                data->manager->priv->query_clients = g_slist_prepend (data->manager->priv->query_clients,
                                                                      client);
        }

        return FALSE;
}

static gboolean
_client_end_session_helper (const char           *id,
                            GsmClient            *client,
                            ClientEndSessionData *data)
{
        return _client_end_session (client, data);
}

static void
do_phase_end_session (GsmManager *manager)
{
        ClientEndSessionData data;

        data.manager = manager;
        data.flags = 0;

        if (manager->priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        if (auto_save_is_enabled (manager)) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_SAVE;
        }

        if (manager->priv->phase_timeout_id > 0) {
                g_source_remove (manager->priv->phase_timeout_id);
                manager->priv->phase_timeout_id = 0;
        }

        if (gsm_store_size (manager->priv->clients) > 0) {
                manager->priv->phase_timeout_id = g_timeout_add_seconds (GSM_MANAGER_PHASE_TIMEOUT,
                                                                         (GSourceFunc)on_phase_timeout,
                                                                         manager);

                gsm_store_foreach (manager->priv->clients,
                                   (GsmStoreFunc)_client_end_session_helper,
                                   &data);
        } else {
                end_phase (manager);
        }
}

static void
do_phase_end_session_part_2 (GsmManager *manager)
{
        ClientEndSessionData data;

        data.manager = manager;
        data.flags = 0;

        if (manager->priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        if (auto_save_is_enabled (manager)) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_SAVE;
        }
        data.flags |= GSM_CLIENT_END_SESSION_FLAG_LAST;

        /* keep the timeout that was started at the beginning of the
         * GSM_MANAGER_PHASE_END_SESSION phase */

        if (g_slist_length (manager->priv->next_query_clients) > 0) {
                g_slist_foreach (manager->priv->next_query_clients,
                                 (GFunc)_client_end_session,
                                 &data);

                g_slist_free (manager->priv->next_query_clients);
                manager->priv->next_query_clients = NULL;
        } else {
                end_phase (manager);
        }
}

static gboolean
_client_stop (const char *id,
              GsmClient  *client,
              gpointer    user_data)
{
        gboolean ret;
        GError  *error;

        error = NULL;
        ret = gsm_client_stop (client, &error);
        if (! ret) {
                g_warning ("Unable to stop client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("GsmManager: stopped client: %s", gsm_client_peek_id (client));
        }

        return FALSE;
}

static void
do_phase_exit (GsmManager *manager)
{
        if (gsm_store_size (manager->priv->clients) > 0) {
                manager->priv->phase_timeout_id = g_timeout_add_seconds (GSM_MANAGER_PHASE_TIMEOUT,
                                                                         (GSourceFunc)on_phase_timeout,
                                                                         manager);

                gsm_store_foreach (manager->priv->clients,
                                   (GsmStoreFunc)_client_stop,
                                   NULL);
        } else {
                end_phase (manager);
        }
}

static gboolean
_client_query_end_session (const char           *id,
                           GsmClient            *client,
                           ClientEndSessionData *data)
{
        gboolean ret;
        GError  *error;

        error = NULL;
        ret = gsm_client_query_end_session (client, data->flags, &error);
        if (! ret) {
                g_warning ("Unable to query client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("GsmManager: adding client to query clients: %s", gsm_client_peek_id (client));
                data->manager->priv->query_clients = g_slist_prepend (data->manager->priv->query_clients,
                                                                      client);
        }

        return FALSE;
}

static gboolean
inhibitor_has_flag (gpointer      key,
                    GsmInhibitor *inhibitor,
                    gpointer      data)
{
        guint flag;
        guint flags;

        flag = GPOINTER_TO_UINT (data);

        flags = gsm_inhibitor_peek_flags (inhibitor);

        return (flags & flag);
}

static gboolean
gsm_manager_is_logout_inhibited (GsmManager *manager)
{
        GsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (GsmInhibitor *)gsm_store_find (manager->priv->inhibitors,
                                                    (GsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (GSM_INHIBITOR_FLAG_LOGOUT));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
gsm_manager_is_idle_inhibited (GsmManager *manager)
{
        GsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (GsmInhibitor *)gsm_store_find (manager->priv->inhibitors,
                                                    (GsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (GSM_INHIBITOR_FLAG_IDLE));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
_client_cancel_end_session (const char *id,
                            GsmClient  *client,
                            GsmManager *manager)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = gsm_client_cancel_end_session (client, &error);
        if (! res) {
                g_warning ("Unable to cancel end session: %s", error->message);
                g_error_free (error);
        }

        return FALSE;
}

static gboolean
inhibitor_is_jit (gpointer      key,
                  GsmInhibitor *inhibitor,
                  GsmManager   *manager)
{
        gboolean    matches;
        const char *id;

        id = gsm_inhibitor_peek_client_id (inhibitor);

        matches = (id != NULL && id[0] != '\0');

        return matches;
}

static void
cancel_end_session (GsmManager *manager)
{
        /* just ignore if received outside of shutdown */
        if (manager->priv->phase < GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        /* switch back to running phase */
        g_debug ("GsmManager: Cancelling the end of session");

        /* remove the dialog before we remove the inhibitors, else the dialog
         * will activate itself automatically when the last inhibitor will be
         * removed */
        if (manager->priv->inhibit_dialog)
                gtk_widget_destroy (GTK_WIDGET (manager->priv->inhibit_dialog));
        manager->priv->inhibit_dialog = NULL;

        /* clear all JIT inhibitors */
        gsm_store_foreach_remove (manager->priv->inhibitors,
                                  (GsmStoreFunc)inhibitor_is_jit,
                                  (gpointer)manager);

        gsm_store_foreach (manager->priv->clients,
                           (GsmStoreFunc)_client_cancel_end_session,
                           NULL);

        gsm_manager_set_phase (manager, GSM_MANAGER_PHASE_RUNNING);
        manager->priv->logout_mode = GSM_MANAGER_LOGOUT_MODE_NORMAL;

        manager->priv->logout_type = GSM_MANAGER_LOGOUT_NONE;
        gdm_set_logout_action (GDM_LOGOUT_ACTION_NONE);

        start_phase (manager);
}


static void
manager_switch_user (GdkDisplay *display,
                     GsmManager *manager)
{
        GError  *error;
        char    *command;
        GAppLaunchContext *context;
        GAppInfo *app;

	/* We have to do this here and in request_switch_user() because this
	 * function can be called at a later time, not just directly after
	 * request_switch_user(). */
	if (_switch_user_is_locked_down (manager)) {
		g_warning ("Unable to switch user: User switching has been locked down");
		return;
	}

        command = g_strdup_printf ("%s %s",
                                   GDM_FLEXISERVER_COMMAND,
                                   GDM_FLEXISERVER_ARGS);

        error = NULL;
        context = (GAppLaunchContext*) gdk_display_get_app_launch_context (display);
        app = g_app_info_create_from_commandline (command, GDM_FLEXISERVER_COMMAND, 0, &error);

        if (app) {
                g_app_info_launch (app, NULL, context, &error);
                g_object_unref (app);
        }

        g_free (command);
        g_object_unref (context);

        if (error) {
                g_debug ("GsmManager: Unable to start GDM greeter: %s", error->message);
                g_error_free (error);
        }
}

static gboolean
sleep_lock_is_enabled (GsmManager *manager)
{
        return g_settings_get_boolean (manager->priv->screensaver_settings,
                                       KEY_SLEEP_LOCK);
}

static void
manager_perhaps_lock (GsmManager *manager)
{
        GError   *error;
        gboolean  ret;

        /* only lock if gnome-screensaver is set to lock */
        if (!sleep_lock_is_enabled (manager)) {
                return;
        }

        /* do this sync to ensure it's on the screen when we start suspending */
        error = NULL;
        ret = g_spawn_command_line_sync ("gnome-screensaver-command --lock", NULL, NULL, NULL, &error);
        if (!ret) {
                g_warning ("Couldn't lock screen: %s", error->message);
                g_error_free (error);
        }
}

static void
manager_attempt_hibernate (GsmManager *manager)
{
        if (gsm_system_can_hibernate (manager->priv->system)) {
                /* lock the screen before we suspend */
                manager_perhaps_lock (manager);
                gsm_system_hibernate (manager->priv->system);
        }
}

static void
manager_attempt_suspend (GsmManager *manager)
{
        if (gsm_system_can_suspend (manager->priv->system)) {
                /* lock the screen before we suspend */
                manager_perhaps_lock (manager);
                gsm_system_suspend (manager->priv->system);
        }
}

static void
do_inhibit_dialog_action (GdkDisplay *display,
                          GsmManager *manager,
                          int         action)
{
        switch (action) {
        case GSM_LOGOUT_ACTION_SWITCH_USER:
                manager_switch_user (display, manager);
                break;
        case GSM_LOGOUT_ACTION_HIBERNATE:
                manager_attempt_hibernate (manager);
                break;
        case GSM_LOGOUT_ACTION_SLEEP:
                manager_attempt_suspend (manager);
                break;
        case GSM_LOGOUT_ACTION_SHUTDOWN:
        case GSM_LOGOUT_ACTION_REBOOT:
        case GSM_LOGOUT_ACTION_LOGOUT:
                manager->priv->logout_mode = GSM_MANAGER_LOGOUT_MODE_FORCE;
                end_phase (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
inhibit_dialog_response (GsmInhibitDialog *dialog,
                         guint             response_id,
                         GsmManager       *manager)
{
        GdkDisplay *display;
        int action;

        g_debug ("GsmManager: Inhibit dialog response: %d", response_id);

        display = gtk_widget_get_display (GTK_WIDGET (dialog));

        /* must destroy dialog before cancelling since we'll
           remove JIT inhibitors and we don't want to trigger
           action. */
        g_object_get (dialog, "action", &action, NULL);
        gtk_widget_destroy (GTK_WIDGET (dialog));
        manager->priv->inhibit_dialog = NULL;

        /* In case of dialog cancel, switch user, hibernate and
         * suspend, we just perform the respective action and return,
         * without shutting down the session. */
        switch (response_id) {
        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_NONE:
        case GTK_RESPONSE_DELETE_EVENT:
                if (action == GSM_LOGOUT_ACTION_LOGOUT
                    || action == GSM_LOGOUT_ACTION_SHUTDOWN
                    || action == GSM_LOGOUT_ACTION_REBOOT) {
                        cancel_end_session (manager);
                }
                break;
        case GTK_RESPONSE_ACCEPT:
                g_debug ("GsmManager: doing action %d", action);
                do_inhibit_dialog_action (display, manager, action);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
end_session_or_show_fallback_dialog (GsmManager *manager)
{
        GsmLogoutAction action;

        if (! gsm_manager_is_logout_inhibited (manager)) {
                end_phase (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        switch (manager->priv->logout_type) {
        case GSM_MANAGER_LOGOUT_LOGOUT:
                action = GSM_LOGOUT_ACTION_LOGOUT;
                break;
        case GSM_MANAGER_LOGOUT_REBOOT:
        case GSM_MANAGER_LOGOUT_REBOOT_INTERACT:
        case GSM_MANAGER_LOGOUT_REBOOT_GDM:
                action = GSM_LOGOUT_ACTION_REBOOT;
                break;
        case GSM_MANAGER_LOGOUT_SHUTDOWN:
        case GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:
        case GSM_MANAGER_LOGOUT_SHUTDOWN_GDM:
                action = GSM_LOGOUT_ACTION_SHUTDOWN;
                break;
        default:
                g_warning ("Unexpected logout type %d when creating inhibit dialog",
                           manager->priv->logout_type);
                action = GSM_LOGOUT_ACTION_LOGOUT;
                break;
        }

        /* Note: GSM_LOGOUT_ACTION_SHUTDOWN and GSM_LOGOUT_ACTION_REBOOT are
         * actually handled the same way as GSM_LOGOUT_ACTION_LOGOUT in the
         * inhibit dialog; the action, if the button is clicked, will be to
         * simply go to the next phase. */
        manager->priv->inhibit_dialog = gsm_inhibit_dialog_new (manager->priv->inhibitors,
                                                                manager->priv->clients,
                                                                action);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
end_session_or_show_shell_dialog (GsmManager *manager)
{
        gboolean logout_prompt;
        GsmShellEndSessionDialogType type;
        gboolean logout_inhibited;

        switch (manager->priv->logout_type) {
        case GSM_MANAGER_LOGOUT_LOGOUT:
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_LOGOUT;
                break;
        case GSM_MANAGER_LOGOUT_REBOOT:
        case GSM_MANAGER_LOGOUT_REBOOT_INTERACT:
        case GSM_MANAGER_LOGOUT_REBOOT_GDM:
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_RESTART;
                break;
        case GSM_MANAGER_LOGOUT_SHUTDOWN:
        case GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:
        case GSM_MANAGER_LOGOUT_SHUTDOWN_GDM:
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_SHUTDOWN;
                break;
        default:
                g_warning ("Unexpected logout type %d when creating end session dialog",
                           manager->priv->logout_type);
                type = GSM_SHELL_END_SESSION_DIALOG_TYPE_LOGOUT;
                break;
        }

        logout_inhibited = gsm_manager_is_logout_inhibited (manager);
        logout_prompt = g_settings_get_boolean (manager->priv->settings,
                                                KEY_LOGOUT_PROMPT);

        switch (manager->priv->logout_mode) {
        case GSM_MANAGER_LOGOUT_MODE_NORMAL:
                if (logout_inhibited || logout_prompt) {
                        show_shell_end_session_dialog (manager, type);
                } else {
                        end_phase (manager);
                }
                break;

        case GSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
                if (logout_inhibited) {
                        show_shell_end_session_dialog (manager, type);
                } else {
                        end_phase (manager);
                }
                break;

        case GSM_MANAGER_LOGOUT_MODE_FORCE:
                end_phase (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

}

static void
show_fallback_dialog (const char *title,
                      const char *description,
                      const char *link_text,
                      const char *uri)
{
        GtkWidget *dialog, *image, *link, *hbox;

        dialog = gtk_message_dialog_new (NULL, 0,
                                         GTK_MESSAGE_WARNING,
                                         GTK_BUTTONS_CLOSE,
                                         "%s", title);

        gtk_window_set_icon_name (GTK_WINDOW (dialog), GSM_ICON_COMPUTER_FAIL);

        image = gtk_image_new_from_icon_name (GSM_ICON_COMPUTER_FAIL,
                                              gsm_util_get_computer_fail_icon_size ());
        gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (dialog), image);

        if (description) {
                gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                                            "%s", description);
        }

        hbox = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));

        if (uri) {
                if (link_text) {
                        link = gtk_link_button_new_with_label (uri, link_text);
                } else {
                        link = gtk_link_button_new (uri);
                }
                gtk_box_pack_start (GTK_BOX (hbox), link, FALSE, FALSE, 0);
        }

        gtk_widget_show_all (dialog);

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (gtk_widget_destroy),
                          NULL);
}

static void
possibly_show_fallback_dialog (GsmManager *manager)
{
        if (manager->priv->is_fallback_session &&
            g_strcmp0 (manager->priv->session_name, "gnome-fallback") == 0 &&
            g_settings_get_boolean (manager->priv->settings,
                                    KEY_SHOW_FALLBACK_WARNING)) {
                show_fallback_dialog (_("GNOME 3 Failed to Load"),
                                      _("Unfortunately GNOME 3 failed to start properly and started in the <i>fallback mode</i>.\n\n"
                                        "This most likely means your system (graphics hardware or driver) is not capable of delivering the full GNOME 3 experience."),
                                      _("Learn more about GNOME 3"),
                                      "http://www.gnome3.org");
                g_settings_set_boolean (manager->priv->settings,
                                        KEY_SHOW_FALLBACK_WARNING, FALSE);
        } else if (g_strcmp0 (manager->priv->session_name, "gnome") == 0 &&
                   g_settings_get_boolean (manager->priv->settings,
                                           KEY_SHOW_FALLBACK_WARNING)) {
                /* Reset the setting if we ever manage to log into gnome 3 */
                g_settings_reset (manager->priv->settings, KEY_SHOW_FALLBACK_WARNING);
        }
}

static void
query_end_session_complete (GsmManager *manager)
{

        g_debug ("GsmManager: query end session complete");

        /* Remove the timeout since this can be called from outside the timer
         * and we don't want to have it called twice */
        if (manager->priv->query_timeout_id > 0) {
                g_source_remove (manager->priv->query_timeout_id);
                manager->priv->query_timeout_id = 0;
        }

        if (gsm_shell_is_running (manager->priv->shell)) {
                end_session_or_show_shell_dialog (manager);
        } else {
                end_session_or_show_fallback_dialog (manager);
        }

}

static guint32
generate_cookie (void)
{
        guint32 cookie;

        cookie = (guint32)g_random_int_range (1, G_MAXINT32);

        return cookie;
}

static guint32
_generate_unique_cookie (GsmManager *manager)
{
        guint32 cookie;

        do {
                cookie = generate_cookie ();
        } while (gsm_store_find (manager->priv->inhibitors, (GsmStoreFunc)_find_by_cookie, &cookie) != NULL);

        return cookie;
}

static gboolean
_on_query_end_session_timeout (GsmManager *manager)
{
        GSList *l;

        manager->priv->query_timeout_id = 0;

        g_debug ("GsmManager: query end session timed out");

        for (l = manager->priv->query_clients; l != NULL; l = l->next) {
                guint         cookie;
                GsmInhibitor *inhibitor;
                const char   *bus_name;
                char         *app_id;

                g_warning ("Client '%s' failed to reply before timeout",
                           gsm_client_peek_id (l->data));

                /* Don't add "not responding" inhibitors if logout is forced
                 */
                if (manager->priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                        continue;
                }

                /* Add JIT inhibit for unresponsive client */
                if (GSM_IS_DBUS_CLIENT (l->data)) {
                        bus_name = gsm_dbus_client_get_bus_name (l->data);
                } else {
                        bus_name = NULL;
                }

                app_id = g_strdup (gsm_client_peek_app_id (l->data));
                if (IS_STRING_EMPTY (app_id)) {
                        /* XSMP clients don't give us an app id unless we start them */
                        g_free (app_id);
                        app_id = gsm_client_get_app_name (l->data);
                }

                cookie = _generate_unique_cookie (manager);
                inhibitor = gsm_inhibitor_new_for_client (gsm_client_peek_id (l->data),
                                                          app_id,
                                                          GSM_INHIBITOR_FLAG_LOGOUT,
                                                          _("Not responding"),
                                                          bus_name,
                                                          cookie);
                g_free (app_id);
                gsm_store_add (manager->priv->inhibitors, gsm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        }

        g_slist_free (manager->priv->query_clients);
        manager->priv->query_clients = NULL;

        query_end_session_complete (manager);

        return FALSE;
}

static void
do_phase_query_end_session (GsmManager *manager)
{
        ClientEndSessionData data;

        data.manager = manager;
        data.flags = 0;

        if (manager->priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= GSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        /* We only query if an app is ready to log out, so we don't use
         * GSM_CLIENT_END_SESSION_FLAG_SAVE here.
         */

        debug_clients (manager);
        g_debug ("GsmManager: sending query-end-session to clients (logout mode: %s)",
                 manager->priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_NORMAL? "normal" :
                 manager->priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE? "forceful":
                 "no confirmation");
        gsm_store_foreach (manager->priv->clients,
                           (GsmStoreFunc)_client_query_end_session,
                           &data);

        /* This phase doesn't time out unless logout is forced. Typically, this
         * separate timer is only used to show UI. */
        manager->priv->query_timeout_id = g_timeout_add_seconds (1, (GSourceFunc)_on_query_end_session_timeout, manager);
}

static void
update_idle (GsmManager *manager)
{
        if (gsm_manager_is_idle_inhibited (manager)) {
                gsm_presence_set_idle_enabled (manager->priv->presence, FALSE);
        } else {
                gsm_presence_set_idle_enabled (manager->priv->presence, TRUE);
        }
}

static void
start_phase (GsmManager *manager)
{

        g_debug ("GsmManager: starting phase %s\n",
                 phase_num_to_name (manager->priv->phase));

        /* reset state */
        g_slist_free (manager->priv->pending_apps);
        manager->priv->pending_apps = NULL;
        g_slist_free (manager->priv->query_clients);
        manager->priv->query_clients = NULL;
        g_slist_free (manager->priv->next_query_clients);
        manager->priv->next_query_clients = NULL;

        if (manager->priv->query_timeout_id > 0) {
                g_source_remove (manager->priv->query_timeout_id);
                manager->priv->query_timeout_id = 0;
        }
        if (manager->priv->phase_timeout_id > 0) {
                g_source_remove (manager->priv->phase_timeout_id);
                manager->priv->phase_timeout_id = 0;
        }

        switch (manager->priv->phase) {
        case GSM_MANAGER_PHASE_STARTUP:
        case GSM_MANAGER_PHASE_INITIALIZATION:
        case GSM_MANAGER_PHASE_WINDOW_MANAGER:
        case GSM_MANAGER_PHASE_PANEL:
        case GSM_MANAGER_PHASE_DESKTOP:
        case GSM_MANAGER_PHASE_APPLICATION:
                do_phase_startup (manager);
                break;
        case GSM_MANAGER_PHASE_RUNNING:
                possibly_show_fallback_dialog (manager);
                g_signal_emit (manager, signals[SESSION_RUNNING], 0);
                update_idle (manager);
                break;
        case GSM_MANAGER_PHASE_QUERY_END_SESSION:
                do_phase_query_end_session (manager);
                break;
        case GSM_MANAGER_PHASE_END_SESSION:
                do_phase_end_session (manager);
                break;
        case GSM_MANAGER_PHASE_EXIT:
                do_phase_exit (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static gboolean
_debug_app_for_phase (const char *id,
                      GsmApp     *app,
                      gpointer    data)
{
        guint phase;

        phase = GPOINTER_TO_UINT (data);

        if (gsm_app_peek_phase (app) != phase) {
                return FALSE;
        }

        g_debug ("GsmManager:\tID: %s\tapp-id:%s\tis-disabled:%d\tis-conditionally-disabled:%d",
                 gsm_app_peek_id (app),
                 gsm_app_peek_app_id (app),
                 gsm_app_peek_is_disabled (app),
                 gsm_app_peek_is_conditionally_disabled (app));

        return FALSE;
}

static void
debug_app_summary (GsmManager *manager)
{
        guint phase;

        g_debug ("GsmManager: App startup summary");
        for (phase = GSM_MANAGER_PHASE_INITIALIZATION; phase < GSM_MANAGER_PHASE_RUNNING; phase++) {
                g_debug ("GsmManager: Phase %s", phase_num_to_name (phase));
                gsm_store_foreach (manager->priv->apps,
                                   (GsmStoreFunc)_debug_app_for_phase,
                                   GUINT_TO_POINTER (phase));
        }
}

void
gsm_manager_start (GsmManager *manager)
{
        g_debug ("GsmManager: GSM starting to manage");

        g_return_if_fail (GSM_IS_MANAGER (manager));

        gsm_manager_set_phase (manager, GSM_MANAGER_PHASE_INITIALIZATION);
        debug_app_summary (manager);
        start_phase (manager);
}

const char *
_gsm_manager_get_default_session (GsmManager     *manager)
{
        return g_settings_get_string (manager->priv->session_settings,
                                      KEY_SESSION_NAME);
}

void
_gsm_manager_set_active_session (GsmManager     *manager,
                                 const char     *session_name,
                                 gboolean        is_fallback)
{
        g_free (manager->priv->session_name);
        manager->priv->session_name = g_strdup (session_name);
        manager->priv->is_fallback_session = is_fallback;
}

static gboolean
_app_has_app_id (const char   *id,
                 GsmApp       *app,
                 const char   *app_id_a)
{
        const char *app_id_b;

        app_id_b = gsm_app_peek_app_id (app);
        return (app_id_b != NULL && strcmp (app_id_a, app_id_b) == 0);
}

static GsmApp *
find_app_for_app_id (GsmManager *manager,
                     const char *app_id)
{
        GsmApp *app;
        app = (GsmApp *)gsm_store_find (manager->priv->apps,
                                        (GsmStoreFunc)_app_has_app_id,
                                        (char *)app_id);
        return app;
}

static gboolean
inhibitor_has_client_id (gpointer      key,
                         GsmInhibitor *inhibitor,
                         const char   *client_id_a)
{
        gboolean    matches;
        const char *client_id_b;

        client_id_b = gsm_inhibitor_peek_client_id (inhibitor);

        matches = FALSE;
        if (! IS_STRING_EMPTY (client_id_a) && ! IS_STRING_EMPTY (client_id_b)) {
                matches = (strcmp (client_id_a, client_id_b) == 0);
                if (matches) {
                        g_debug ("GsmManager: removing JIT inhibitor for %s for reason '%s'",
                                 gsm_inhibitor_peek_client_id (inhibitor),
                                 gsm_inhibitor_peek_reason (inhibitor));
                }
        }

        return matches;
}

static gboolean
_app_has_startup_id (const char *id,
                     GsmApp     *app,
                     const char *startup_id_a)
{
        const char *startup_id_b;

        startup_id_b = gsm_app_peek_startup_id (app);

        if (IS_STRING_EMPTY (startup_id_b)) {
                return FALSE;
        }

        return (strcmp (startup_id_a, startup_id_b) == 0);
}

static GsmApp *
find_app_for_startup_id (GsmManager *manager,
                        const char *startup_id)
{
        GsmApp *found_app;
        GSList *a;

        found_app = NULL;

        /* If we're starting up the session, try to match the new client
         * with one pending apps for the current phase. If not, try to match
         * with any of the autostarted apps. */
        if (manager->priv->phase < GSM_MANAGER_PHASE_APPLICATION) {
                for (a = manager->priv->pending_apps; a != NULL; a = a->next) {
                        GsmApp *app = GSM_APP (a->data);

                        if (strcmp (startup_id, gsm_app_peek_startup_id (app)) == 0) {
                                found_app = app;
                                goto out;
                        }
                }
        } else {
                GsmApp *app;

                app = (GsmApp *)gsm_store_find (manager->priv->apps,
                                                (GsmStoreFunc)_app_has_startup_id,
                                                (char *)startup_id);
                if (app != NULL) {
                        found_app = app;
                        goto out;
                }
        }
 out:
        return found_app;
}

static void
_disconnect_client (GsmManager *manager,
                    GsmClient  *client)
{
        gboolean              is_condition_client;
        GsmApp               *app;
        const char           *app_id;
        const char           *startup_id;
        gboolean              app_restart;
        GsmClientRestartStyle client_restart_hint;

        g_debug ("GsmManager: disconnect client: %s", gsm_client_peek_id (client));

        /* take a ref so it doesn't get finalized */
        g_object_ref (client);

        gsm_client_set_status (client, GSM_CLIENT_FINISHED);

        is_condition_client = FALSE;
        if (g_slist_find (manager->priv->condition_clients, client)) {
                manager->priv->condition_clients = g_slist_remove (manager->priv->condition_clients, client);

                is_condition_client = TRUE;
        }

        /* remove any inhibitors for this client */
        gsm_store_foreach_remove (manager->priv->inhibitors,
                                  (GsmStoreFunc)inhibitor_has_client_id,
                                  (gpointer)gsm_client_peek_id (client));

        app = NULL;

        /* first try to match on startup ID */
        startup_id = gsm_client_peek_startup_id (client);
        if (! IS_STRING_EMPTY (startup_id)) {
                app = find_app_for_startup_id (manager, startup_id);

        }

        /* then try to find matching app-id */
        if (app == NULL) {
                app_id = gsm_client_peek_app_id (client);
                if (! IS_STRING_EMPTY (app_id)) {
                        g_debug ("GsmManager: disconnect for app '%s'", app_id);
                        app = find_app_for_app_id (manager, app_id);
                }
        }

        if (manager->priv->phase == GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Instead of answering our end session query, the client just exited.
                 * Treat that as an "okay, end the session" answer.
                 *
                 * This call implicitly removes any inhibitors for the client, along
                 * with removing the client from the pending query list.
                 */
                _handle_client_end_session_response (manager,
                                                     client,
                                                     TRUE,
                                                     FALSE,
                                                     FALSE,
                                                     "Client exited in "
                                                     "query end session phase "
                                                     "instead of end session "
                                                     "phase");
        }

        if (manager->priv->dbus_disconnected && GSM_IS_DBUS_CLIENT (client)) {
                g_debug ("GsmManager: dbus disconnected, not restarting application");
                goto out;
        }

        if (app == NULL) {
                g_debug ("GsmManager: unable to find application for client - not restarting");
                goto out;
        }

        if (manager->priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                g_debug ("GsmManager: in shutdown, not restarting application");
                goto out;
        }

        app_restart = gsm_app_peek_autorestart (app);
        client_restart_hint = gsm_client_peek_restart_style_hint (client);

        /* allow legacy clients to override the app info */
        if (! app_restart
            && client_restart_hint != GSM_CLIENT_RESTART_IMMEDIATELY) {
                g_debug ("GsmManager: autorestart not set, not restarting application");
                goto out;
        }

        if (is_condition_client) {
                g_debug ("GsmManager: app conditionally disabled, not restarting application");
                goto out;
        }

        g_debug ("GsmManager: restarting app");

        _restart_app (manager, app);

 out:
        g_object_unref (client);
}

typedef struct {
        const char *service_name;
        GsmManager *manager;
} RemoveClientData;

static gboolean
_disconnect_dbus_client (const char       *id,
                         GsmClient        *client,
                         RemoveClientData *data)
{
        const char *name;

        if (! GSM_IS_DBUS_CLIENT (client)) {
                return FALSE;
        }

        /* If no service name, then we simply disconnect all clients */
        if (!data->service_name) {
                _disconnect_client (data->manager, client);
                return TRUE;
        }

        name = gsm_dbus_client_get_bus_name (GSM_DBUS_CLIENT (client));
        if (IS_STRING_EMPTY (name)) {
                return FALSE;
        }

        if (strcmp (data->service_name, name) == 0) {
                _disconnect_client (data->manager, client);
                return TRUE;
        }

        return FALSE;
}

/**
 * remove_clients_for_connection:
 * @manager: a #GsmManager
 * @service_name: a service name
 *
 * Disconnects clients that own @service_name.
 *
 * If @service_name is NULL, then disconnects all clients for the connection.
 */
static void
remove_clients_for_connection (GsmManager *manager,
                               const char *service_name)
{
        RemoveClientData data;

        data.service_name = service_name;
        data.manager = manager;

        /* disconnect dbus clients for name */
        gsm_store_foreach_remove (manager->priv->clients,
                                  (GsmStoreFunc)_disconnect_dbus_client,
                                  &data);

        if (manager->priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION
            && gsm_store_size (manager->priv->clients) == 0) {
                g_debug ("GsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

static gboolean
inhibitor_has_bus_name (gpointer          key,
                        GsmInhibitor     *inhibitor,
                        RemoveClientData *data)
{
        gboolean    matches;
        const char *bus_name_b;

        bus_name_b = gsm_inhibitor_peek_bus_name (inhibitor);

        matches = FALSE;
        if (! IS_STRING_EMPTY (data->service_name) && ! IS_STRING_EMPTY (bus_name_b)) {
                matches = (strcmp (data->service_name, bus_name_b) == 0);
                if (matches) {
                        g_debug ("GsmManager: removing inhibitor from %s for reason '%s' on connection %s",
                                 gsm_inhibitor_peek_app_id (inhibitor),
                                 gsm_inhibitor_peek_reason (inhibitor),
                                 gsm_inhibitor_peek_bus_name (inhibitor));
                }
        }

        return matches;
}

static void
remove_inhibitors_for_connection (GsmManager *manager,
                                  const char *service_name)
{
        RemoveClientData data;

        data.service_name = service_name;
        data.manager = manager;

        debug_inhibitors (manager);

        gsm_store_foreach_remove (manager->priv->inhibitors,
                                  (GsmStoreFunc)inhibitor_has_bus_name,
                                  &data);
}

static void
bus_name_owner_changed (DBusGProxy  *bus_proxy,
                        const char  *service_name,
                        const char  *old_service_name,
                        const char  *new_service_name,
                        GsmManager  *manager)
{
        if (strlen (new_service_name) == 0
            && strlen (old_service_name) > 0) {
                /* service removed */
                remove_inhibitors_for_connection (manager, old_service_name);
                remove_clients_for_connection (manager, old_service_name);
        } else if (strlen (old_service_name) == 0
                   && strlen (new_service_name) > 0) {
                /* service added */

                /* use this if we support automatically registering
                 * well known bus names */
        }
}

static DBusHandlerResult
gsm_manager_bus_filter (DBusConnection *connection,
                        DBusMessage    *message,
                        void           *user_data)
{
        GsmManager *manager;

        manager = GSM_MANAGER (user_data);

        if (dbus_message_is_signal (message,
                                    DBUS_INTERFACE_LOCAL, "Disconnected") &&
            strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                g_debug ("GsmManager: dbus disconnected; disconnecting dbus clients...");
                manager->priv->dbus_disconnected = TRUE;
                remove_clients_for_connection (manager, NULL);
                /* let other filters get this disconnected signal, so that they
                 * can handle it too */
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}
static gboolean
register_manager (GsmManager *manager)
{
        GError *error = NULL;
        DBusConnection *connection;

        error = NULL;
        manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        connection = dbus_g_connection_get_connection (manager->priv->connection);
        dbus_connection_add_filter (connection,
                                    gsm_manager_bus_filter,
                                    manager, NULL);
        manager->priv->dbus_disconnected = FALSE;

        manager->priv->bus_proxy = dbus_g_proxy_new_for_name (manager->priv->connection,
                                                              DBUS_SERVICE_DBUS,
                                                              DBUS_PATH_DBUS,
                                                              DBUS_INTERFACE_DBUS);
        dbus_g_proxy_add_signal (manager->priv->bus_proxy,
                                 "NameOwnerChanged",
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (manager->priv->bus_proxy,
                                     "NameOwnerChanged",
                                     G_CALLBACK (bus_name_owner_changed),
                                     manager,
                                     NULL);

        dbus_g_connection_register_g_object (manager->priv->connection, GSM_MANAGER_DBUS_PATH, G_OBJECT (manager));

        return TRUE;
}

static void
gsm_manager_set_failsafe (GsmManager *manager,
                          gboolean    enabled)
{
        g_return_if_fail (GSM_IS_MANAGER (manager));

        manager->priv->failsafe = enabled;
}

gboolean
gsm_manager_get_failsafe (GsmManager *manager)
{
        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

	return manager->priv->failsafe;
}

static void
on_client_disconnected (GsmClient  *client,
                        GsmManager *manager)
{
        g_debug ("GsmManager: disconnect client");
        _disconnect_client (manager, client);
        gsm_store_remove (manager->priv->clients, gsm_client_peek_id (client));
        if (manager->priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION
            && gsm_store_size (manager->priv->clients) == 0) {
                g_debug ("GsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

static gboolean
on_xsmp_client_register_request (GsmXSMPClient *client,
                                 char         **id,
                                 GsmManager    *manager)
{
        gboolean handled;
        char    *new_id;
        GsmApp  *app;

        handled = TRUE;
        new_id = NULL;

        if (manager->priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                goto out;
        }

        if (IS_STRING_EMPTY (*id)) {
                new_id = gsm_util_generate_startup_id ();
        } else {
                GsmClient *client;

                client = (GsmClient *)gsm_store_find (manager->priv->clients,
                                                      (GsmStoreFunc)_client_has_startup_id,
                                                      *id);
                /* We can't have two clients with the same id. */
                if (client != NULL) {
                        goto out;
                }

                new_id = g_strdup (*id);
        }

        g_debug ("GsmManager: Adding new client %s to session", new_id);

        g_signal_connect (client,
                          "disconnected",
                          G_CALLBACK (on_client_disconnected),
                          manager);

        /* If it's a brand new client id, we just accept the client*/
        if (IS_STRING_EMPTY (*id)) {
                goto out;
        }

        app = find_app_for_startup_id (manager, new_id);
        if (app != NULL) {
                gsm_client_set_app_id (GSM_CLIENT (client), gsm_app_peek_app_id (app));
                gsm_app_registered (app);
                goto out;
        }

        /* app not found */
        g_free (new_id);
        new_id = NULL;

 out:
        g_free (*id);
        *id = new_id;

        return handled;
}

static gboolean
auto_save_is_enabled (GsmManager *manager)
{
        return g_settings_get_boolean (manager->priv->settings,
                                       KEY_AUTOSAVE);
}

static void
maybe_save_session (GsmManager *manager)
{
        GError *error;

        if (gsm_system_is_login_session (manager->priv->system))
                return;

        /* We only allow session saving when session is running or when
         * logging out */
        if (manager->priv->phase != GSM_MANAGER_PHASE_RUNNING &&
            manager->priv->phase != GSM_MANAGER_PHASE_END_SESSION) {
                return;
        }

        if (!auto_save_is_enabled (manager)) {
                gsm_session_save_clear ();
                return;
        }

        error = NULL;
        gsm_session_save (manager->priv->clients, &error);

        if (error) {
                g_warning ("Error saving session: %s", error->message);
                g_error_free (error);
        }
}

static void
_handle_client_end_session_response (GsmManager *manager,
                                     GsmClient  *client,
                                     gboolean    is_ok,
                                     gboolean    do_last,
                                     gboolean    cancel,
                                     const char *reason)
{
        /* just ignore if received outside of shutdown */
        if (manager->priv->phase < GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        g_debug ("GsmManager: Response from end session request: is-ok=%d do-last=%d cancel=%d reason=%s", is_ok, do_last, cancel, reason ? reason :"");

        if (cancel) {
                cancel_end_session (manager);
                return;
        }

        manager->priv->query_clients = g_slist_remove (manager->priv->query_clients, client);

        if (! is_ok && manager->priv->logout_mode != GSM_MANAGER_LOGOUT_MODE_FORCE) {
                guint         cookie;
                GsmInhibitor *inhibitor;
                char         *app_id;
                const char   *bus_name;

                /* FIXME: do we support updating the reason? */

                /* Create JIT inhibit */
                if (GSM_IS_DBUS_CLIENT (client)) {
                        bus_name = gsm_dbus_client_get_bus_name (GSM_DBUS_CLIENT (client));
                } else {
                        bus_name = NULL;
                }

                app_id = g_strdup (gsm_client_peek_app_id (client));
                if (IS_STRING_EMPTY (app_id)) {
                        /* XSMP clients don't give us an app id unless we start them */
                        g_free (app_id);
                        app_id = gsm_client_get_app_name (client);
                }

                cookie = _generate_unique_cookie (manager);
                inhibitor = gsm_inhibitor_new_for_client (gsm_client_peek_id (client),
                                                          app_id,
                                                          GSM_INHIBITOR_FLAG_LOGOUT,
                                                          reason != NULL ? reason : _("Not responding"),
                                                          bus_name,
                                                          cookie);
                g_free (app_id);
                gsm_store_add (manager->priv->inhibitors, gsm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        } else {
                gsm_store_foreach_remove (manager->priv->inhibitors,
                                          (GsmStoreFunc)inhibitor_has_client_id,
                                          (gpointer)gsm_client_peek_id (client));
        }

        if (manager->priv->phase == GSM_MANAGER_PHASE_QUERY_END_SESSION) { 
                if (manager->priv->query_clients == NULL) {
                        query_end_session_complete (manager);
                }
        } else if (manager->priv->phase == GSM_MANAGER_PHASE_END_SESSION) {
                if (do_last) {
                        /* This only makes sense if we're in part 1 of
                         * GSM_MANAGER_PHASE_END_SESSION. Doing this in part 2
                         * can only happen because of a buggy client that loops
                         * wanting to be last again and again. The phase
                         * timeout will take care of this issue. */
                        manager->priv->next_query_clients = g_slist_prepend (manager->priv->next_query_clients,
                                                                             client);
                }

                /* we can continue to the next step if all clients have replied
                 * and if there's no inhibitor */
                if (manager->priv->query_clients != NULL
                    || gsm_manager_is_logout_inhibited (manager)) {
                        return;
                }

                if (manager->priv->next_query_clients != NULL) {
                        do_phase_end_session_part_2 (manager);
                } else {
                        end_phase (manager);
                }
        }
}

static void
on_client_end_session_response (GsmClient  *client,
                                gboolean    is_ok,
                                gboolean    do_last,
                                gboolean    cancel,
                                const char *reason,
                                GsmManager *manager)
{
        _handle_client_end_session_response (manager,
                                             client,
                                             is_ok,
                                             do_last,
                                             cancel,
                                             reason);
}

static void
on_xsmp_client_logout_request (GsmXSMPClient *client,
                               gboolean       show_dialog,
                               GsmManager    *manager)
{
        GError *error;
        int     logout_mode;

        if (show_dialog) {
                logout_mode = GSM_MANAGER_LOGOUT_MODE_NORMAL;
        } else {
                logout_mode = GSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION;
        }

        error = NULL;
        gsm_manager_logout (manager, logout_mode, &error);
        if (error != NULL) {
                g_warning ("Unable to logout: %s", error->message);
                g_error_free (error);
        }
}

static void
on_store_client_added (GsmStore   *store,
                       const char *id,
                       GsmManager *manager)
{
        GsmClient *client;

        g_debug ("GsmManager: Client added: %s", id);

        client = (GsmClient *)gsm_store_lookup (store, id);

        /* a bit hacky */
        if (GSM_IS_XSMP_CLIENT (client)) {
                g_signal_connect (client,
                                  "register-request",
                                  G_CALLBACK (on_xsmp_client_register_request),
                                  manager);
                g_signal_connect (client,
                                  "logout-request",
                                  G_CALLBACK (on_xsmp_client_logout_request),
                                  manager);
        }

        g_signal_connect (client,
                          "end-session-response",
                          G_CALLBACK (on_client_end_session_response),
                          manager);

        g_signal_emit (manager, signals [CLIENT_ADDED], 0, id);
        /* FIXME: disconnect signal handler */
}

static void
on_store_client_removed (GsmStore   *store,
                         const char *id,
                         GsmManager *manager)
{
        g_debug ("GsmManager: Client removed: %s", id);

        g_signal_emit (manager, signals [CLIENT_REMOVED], 0, id);
}

static void
gsm_manager_set_client_store (GsmManager *manager,
                              GsmStore   *store)
{
        g_return_if_fail (GSM_IS_MANAGER (manager));

        if (store != NULL) {
                g_object_ref (store);
        }

        if (manager->priv->clients != NULL) {
                g_signal_handlers_disconnect_by_func (manager->priv->clients,
                                                      on_store_client_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (manager->priv->clients,
                                                      on_store_client_removed,
                                                      manager);

                g_object_unref (manager->priv->clients);
        }


        g_debug ("GsmManager: setting client store %p", store);

        manager->priv->clients = store;

        if (manager->priv->clients != NULL) {
                g_signal_connect (manager->priv->clients,
                                  "added",
                                  G_CALLBACK (on_store_client_added),
                                  manager);
                g_signal_connect (manager->priv->clients,
                                  "removed",
                                  G_CALLBACK (on_store_client_removed),
                                  manager);
        }
}

static void
gsm_manager_set_property (GObject       *object,
                          guint          prop_id,
                          const GValue  *value,
                          GParamSpec    *pspec)
{
        GsmManager *self;

        self = GSM_MANAGER (object);

        switch (prop_id) {
        case PROP_FAILSAFE:
                gsm_manager_set_failsafe (self, g_value_get_boolean (value));
                break;
         case PROP_FALLBACK:
                self->priv->is_fallback_session = g_value_get_boolean (value);
                break;
         case PROP_CLIENT_STORE:
                gsm_manager_set_client_store (self, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_manager_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
        GsmManager *self;

        self = GSM_MANAGER (object);

        switch (prop_id) {
        case PROP_FAILSAFE:
                g_value_set_boolean (value, self->priv->failsafe);
                break;
        case PROP_SESSION_NAME:
                g_value_set_string (value, self->priv->session_name);
                break;
        case PROP_FALLBACK:
                g_value_set_boolean (value, self->priv->is_fallback_session);
                break;
        case PROP_CLIENT_STORE:
                g_value_set_object (value, self->priv->clients);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
_find_app_provides (const char *id,
                    GsmApp     *app,
                    const char *service)
{
        return gsm_app_provides (app, service);
}

static GObject *
gsm_manager_constructor (GType                  type,
                         guint                  n_construct_properties,
                         GObjectConstructParam *construct_properties)
{
        GsmManager *manager;

        manager = GSM_MANAGER (G_OBJECT_CLASS (gsm_manager_parent_class)->constructor (type,
                                                                                       n_construct_properties,
                                                                                       construct_properties));
        return G_OBJECT (manager);
}

static void
on_store_inhibitor_added (GsmStore   *store,
                          const char *id,
                          GsmManager *manager)
{
        GsmInhibitor *i;

        g_debug ("GsmManager: Inhibitor added: %s", id);

        i = GSM_INHIBITOR (gsm_store_lookup (store, id));
        gsm_system_add_inhibitor (manager->priv->system, id,
                                  gsm_inhibitor_peek_flags (i));

        g_signal_emit (manager, signals [INHIBITOR_ADDED], 0, id);
        update_idle (manager);
}

static void
on_store_inhibitor_removed (GsmStore   *store,
                            const char *id,
                            GsmManager *manager)
{
        g_debug ("GsmManager: Inhibitor removed: %s", id);

        gsm_system_remove_inhibitor (manager->priv->system, id);

        g_signal_emit (manager, signals [INHIBITOR_REMOVED], 0, id);
        update_idle (manager);
}

static void
gsm_manager_dispose (GObject *object)
{
        GsmManager *manager = GSM_MANAGER (object);

        g_debug ("GsmManager: disposing manager");

        if (manager->priv->clients != NULL) {
                g_signal_handlers_disconnect_by_func (manager->priv->clients,
                                                      on_store_client_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (manager->priv->clients,
                                                      on_store_client_removed,
                                                      manager);
                g_object_unref (manager->priv->clients);
                manager->priv->clients = NULL;
        }

        if (manager->priv->apps != NULL) {
                g_object_unref (manager->priv->apps);
                manager->priv->apps = NULL;
        }

        g_slist_free (manager->priv->required_apps);
        manager->priv->required_apps = NULL;

        if (manager->priv->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (manager->priv->inhibitors,
                                                      on_store_inhibitor_added,
                                                      manager);
                g_signal_handlers_disconnect_by_func (manager->priv->inhibitors,
                                                      on_store_inhibitor_removed,
                                                      manager);

                g_object_unref (manager->priv->inhibitors);
                manager->priv->inhibitors = NULL;
        }

        if (manager->priv->presence != NULL) {
                g_object_unref (manager->priv->presence);
                manager->priv->presence = NULL;
        }

        if (manager->priv->settings) {
                g_object_unref (manager->priv->settings);
                manager->priv->settings = NULL;
        }

        if (manager->priv->session_settings) {
                g_object_unref (manager->priv->session_settings);
                manager->priv->session_settings = NULL;
        }

        if (manager->priv->screensaver_settings) {
                g_object_unref (manager->priv->screensaver_settings);
                manager->priv->screensaver_settings = NULL;
        }

        if (manager->priv->lockdown_settings) {
                g_object_unref (manager->priv->lockdown_settings);
                manager->priv->lockdown_settings = NULL;
        }

        if (manager->priv->system != NULL) {
                g_object_unref (manager->priv->system);
                manager->priv->system = NULL;
        }

        if (manager->priv->shell != NULL) {
                g_object_unref (manager->priv->shell);
                manager->priv->shell = NULL;
        }

        G_OBJECT_CLASS (gsm_manager_parent_class)->dispose (object);
}

static void
gsm_manager_class_init (GsmManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsm_manager_get_property;
        object_class->set_property = gsm_manager_set_property;
        object_class->constructor = gsm_manager_constructor;
        object_class->finalize = gsm_manager_finalize;
        object_class->dispose = gsm_manager_dispose;

        signals [PHASE_CHANGED] =
                g_signal_new ("phase-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, phase_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        signals [SESSION_RUNNING] =
                g_signal_new ("session-running",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, session_running),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [SESSION_OVER] =
                g_signal_new ("session-over",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, session_over),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [CLIENT_ADDED] =
                g_signal_new ("client-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, client_added),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);
        signals [CLIENT_REMOVED] =
                g_signal_new ("client-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, client_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);
        signals [INHIBITOR_ADDED] =
                g_signal_new ("inhibitor-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, inhibitor_added),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);
        signals [INHIBITOR_REMOVED] =
                g_signal_new ("inhibitor-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsmManagerClass, inhibitor_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);

        g_object_class_install_property (object_class,
                                         PROP_FAILSAFE,
                                         g_param_spec_boolean ("failsafe",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        /**
         * GsmManager::session-name
         *
         * Then name of the currently active session, typically "gnome" or "gnome-fallback".
         * This may be the name of the configured default session, or the name of a fallback
         * session in case we fell back.
         */
        g_object_class_install_property (object_class,
                                         PROP_SESSION_NAME,
                                         g_param_spec_string ("session-name",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READABLE));

        /**
         * GsmManager::fallback
         *
         * If %TRUE, the current session is running in the "fallback" mode;
         * this is distinct from whether or not it was configured as default.
         */
        g_object_class_install_property (object_class,
                                         PROP_FALLBACK,
                                         g_param_spec_boolean ("fallback",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_object_class_install_property (object_class,
                                         PROP_CLIENT_STORE,
                                         g_param_spec_object ("client-store",
                                                              NULL,
                                                              NULL,
                                                              GSM_TYPE_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GsmManagerPrivate));

        dbus_g_object_type_install_info (GSM_TYPE_MANAGER, &dbus_glib_gsm_manager_object_info);
        dbus_g_error_domain_register (GSM_MANAGER_ERROR, NULL, GSM_MANAGER_TYPE_ERROR);
}

static void
on_presence_status_changed (GsmPresence  *presence,
                            guint         status,
                            GsmManager   *manager)
{
        GsmSystem *system;

        system = gsm_get_system ();
        gsm_system_set_session_idle (system,
                                     (status == GSM_PRESENCE_STATUS_IDLE));
        g_object_unref (system);
}

static gboolean
idle_timeout_get_mapping (GValue *value,
                          GVariant *variant,
                          gpointer user_data)
{
        guint32 idle_timeout;

        idle_timeout = g_variant_get_uint32 (variant);
        g_value_set_uint (value, idle_timeout * 1000);

        return TRUE;
}

static void
gsm_manager_init (GsmManager *manager)
{

        manager->priv = GSM_MANAGER_GET_PRIVATE (manager);

        manager->priv->settings = g_settings_new (GSM_MANAGER_SCHEMA);
        manager->priv->session_settings = g_settings_new (SESSION_SCHEMA);
        manager->priv->screensaver_settings = g_settings_new (SCREENSAVER_SCHEMA);
        manager->priv->lockdown_settings = g_settings_new (LOCKDOWN_SCHEMA);

        manager->priv->inhibitors = gsm_store_new ();
        g_signal_connect (manager->priv->inhibitors,
                          "added",
                          G_CALLBACK (on_store_inhibitor_added),
                          manager);
        g_signal_connect (manager->priv->inhibitors,
                          "removed",
                          G_CALLBACK (on_store_inhibitor_removed),
                          manager);

        manager->priv->apps = gsm_store_new ();

        manager->priv->presence = gsm_presence_new ();
        g_signal_connect (manager->priv->presence,
                          "status-changed",
                          G_CALLBACK (on_presence_status_changed),
                          manager);

        g_settings_bind_with_mapping (manager->priv->session_settings,
                                      KEY_IDLE_DELAY,
                                      manager->priv->presence,
                                      "idle-timeout",
                                      G_SETTINGS_BIND_GET,
                                      idle_timeout_get_mapping,
                                      NULL,
                                      NULL, NULL);

        manager->priv->system = gsm_get_system ();

        manager->priv->shell = gsm_get_shell ();
}

static void
gsm_manager_finalize (GObject *object)
{
        GsmManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSM_IS_MANAGER (object));

        manager = GSM_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        G_OBJECT_CLASS (gsm_manager_parent_class)->finalize (object);
}

GsmManager *
gsm_manager_get (void)
{
        return manager_object;
}

GsmManager *
gsm_manager_new (GsmStore *client_store,
                 gboolean  failsafe)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (GSM_TYPE_MANAGER,
                                               "client-store", client_store,
                                               "failsafe", failsafe,
                                               NULL);

                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                res = register_manager (manager_object);
                if (! res) {
                        g_object_unref (manager_object);
                        return NULL;
                }
        }

        return GSM_MANAGER (manager_object);
}

gboolean
gsm_manager_setenv (GsmManager  *manager,
                    const char  *variable,
                    const char  *value,
                    GError     **error)
{
        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        if (manager->priv->phase > GSM_MANAGER_PHASE_INITIALIZATION) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                             "Setenv interface is only available during the Initialization phase");
                return FALSE;
        }

        gsm_util_setenv (variable, value);

        return TRUE;
}

gboolean
gsm_manager_initialization_error (GsmManager  *manager,
                                  const char  *message,
                                  gboolean     fatal,
                                  GError     **error)
{
        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        if (manager->priv->phase > GSM_MANAGER_PHASE_INITIALIZATION) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                             "InitializationError interface is only available during the Initialization phase");
                return FALSE;
        }

        gsm_util_init_error (fatal, "%s", message);

        return TRUE;
}

static gboolean
gsm_manager_is_switch_user_inhibited (GsmManager *manager)
{
        GsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (GsmInhibitor *)gsm_store_find (manager->priv->inhibitors,
                                                    (GsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (GSM_INHIBITOR_FLAG_SWITCH_USER));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
gsm_manager_is_suspend_inhibited (GsmManager *manager)
{
        GsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (GsmInhibitor *)gsm_store_find (manager->priv->inhibitors,
                                                    (GsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (GSM_INHIBITOR_FLAG_SUSPEND));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static void
request_reboot (GsmManager *manager)
{
        g_debug ("GsmManager: requesting reboot");

        /* FIXME: We need to support a more structured shutdown here,
         * but that's blocking on an improved ConsoleKit api.
         *
         * See https://bugzilla.gnome.org/show_bug.cgi?id=585614
         */
        manager->priv->logout_type = GSM_MANAGER_LOGOUT_REBOOT_INTERACT;
        end_phase (manager);
}

static void
request_shutdown (GsmManager *manager)
{
        g_debug ("GsmManager: requesting shutdown");

        /* See the comment in request_reboot() for some more details about
         * what work needs to be done here. */
        manager->priv->logout_type = GSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT;
        end_phase (manager);
}

static void
request_suspend (GsmManager *manager)
{
        g_debug ("GsmManager: requesting suspend");

        if (! gsm_manager_is_suspend_inhibited (manager)) {
                manager_attempt_suspend (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = gsm_inhibit_dialog_new (manager->priv->inhibitors,
                                                                manager->priv->clients,
                                                                GSM_LOGOUT_ACTION_SLEEP);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
request_hibernate (GsmManager *manager)
{
        g_debug ("GsmManager: requesting hibernate");

        /* hibernate uses suspend inhibit */
        if (! gsm_manager_is_suspend_inhibited (manager)) {
                manager_attempt_hibernate (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = gsm_inhibit_dialog_new (manager->priv->inhibitors,
                                                                manager->priv->clients,
                                                                GSM_LOGOUT_ACTION_HIBERNATE);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}


static void
request_logout (GsmManager           *manager,
                GsmManagerLogoutMode  mode)
{
        g_debug ("GsmManager: requesting logout");

        manager->priv->logout_mode = mode;
        manager->priv->logout_type = GSM_MANAGER_LOGOUT_LOGOUT;

        end_phase (manager);
}

static void
request_switch_user (GdkDisplay *display,
                     GsmManager *manager)
{
        g_debug ("GsmManager: requesting user switch");

	/* See comment in manager_switch_user() to understand why we do this in
	 * both functions. */
	if (_switch_user_is_locked_down (manager)) {
		g_warning ("Unable to switch user: User switching has been locked down");
		return;
	}

        if (! gsm_manager_is_switch_user_inhibited (manager)) {
                manager_switch_user (display, manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("GsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = gsm_inhibit_dialog_new (manager->priv->inhibitors,
                                                                manager->priv->clients,
                                                                GSM_LOGOUT_ACTION_SWITCH_USER);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
logout_dialog_response (GsmLogoutDialog *logout_dialog,
                        guint            response_id,
                        GsmManager      *manager)
{
        GdkDisplay *display;

        /* We should only be here if mode has already have been set from
         * show_fallback_shutdown/logout_dialog
         */
        g_assert (manager->priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_NORMAL);

        g_debug ("GsmManager: Logout dialog response: %d", response_id);

        display = gtk_widget_get_display (GTK_WIDGET (logout_dialog));

        gtk_widget_destroy (GTK_WIDGET (logout_dialog));

        /* In case of dialog cancel, switch user, hibernate and
         * suspend, we just perform the respective action and return,
         * without shutting down the session. */
        switch (response_id) {
        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_NONE:
        case GTK_RESPONSE_DELETE_EVENT:
                break;
        case GSM_LOGOUT_RESPONSE_SWITCH_USER:
                request_switch_user (display, manager);
                break;
        case GSM_LOGOUT_RESPONSE_HIBERNATE:
                request_hibernate (manager);
                break;
        case GSM_LOGOUT_RESPONSE_SLEEP:
                request_suspend (manager);
                break;
        case GSM_LOGOUT_RESPONSE_SHUTDOWN:
                request_shutdown (manager);
                break;
        case GSM_LOGOUT_RESPONSE_REBOOT:
                request_reboot (manager);
                break;
        case GSM_LOGOUT_RESPONSE_LOGOUT:
                /* We've already gotten confirmation from the user so
                 * initiate the logout in NO_CONFIRMATION mode.
                 *
                 * (it shouldn't matter whether we use NO_CONFIRMATION or stay
                 * with NORMAL, unless the shell happens to start after the
                 * user confirmed)
                 */
                request_logout (manager, GSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
show_fallback_shutdown_dialog (GsmManager *manager,
                               gboolean    is_reboot)
{
        GtkWidget *dialog;

        if (manager->priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        manager->priv->logout_mode = GSM_MANAGER_LOGOUT_MODE_NORMAL;

        dialog = gsm_get_shutdown_dialog (gdk_screen_get_default (),
                                          gtk_get_current_event_time (),
                                          is_reboot ?
                                          GSM_DIALOG_LOGOUT_TYPE_REBOOT :
                                          GSM_DIALOG_LOGOUT_TYPE_SHUTDOWN);

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (logout_dialog_response),
                          manager);
        gtk_widget_show (dialog);
}

static void
show_fallback_logout_dialog (GsmManager *manager)
{
        GtkWidget *dialog;

        if (manager->priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        manager->priv->logout_mode = GSM_MANAGER_LOGOUT_MODE_NORMAL;

        dialog = gsm_get_logout_dialog (gdk_screen_get_default (),
                                        gtk_get_current_event_time ());

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (logout_dialog_response),
                          manager);
        gtk_widget_show (dialog);
}

static void
disconnect_shell_dialog_signals (GsmManager *manager)
{
        if (manager->priv->shell_end_session_dialog_canceled_id != 0) {
                g_signal_handler_disconnect (manager->priv->shell,
                                             manager->priv->shell_end_session_dialog_canceled_id);
                manager->priv->shell_end_session_dialog_canceled_id = 0;
        }

        if (manager->priv->shell_end_session_dialog_confirmed_logout_id != 0) {
                g_signal_handler_disconnect (manager->priv->shell,
                                             manager->priv->shell_end_session_dialog_confirmed_logout_id);
                manager->priv->shell_end_session_dialog_confirmed_logout_id = 0;
        }

        if (manager->priv->shell_end_session_dialog_confirmed_shutdown_id != 0) {
                g_signal_handler_disconnect (manager->priv->shell,
                                             manager->priv->shell_end_session_dialog_confirmed_shutdown_id);
                manager->priv->shell_end_session_dialog_confirmed_shutdown_id = 0;
        }

        if (manager->priv->shell_end_session_dialog_confirmed_reboot_id != 0) {
                g_signal_handler_disconnect (manager->priv->shell,
                                             manager->priv->shell_end_session_dialog_confirmed_reboot_id);
                manager->priv->shell_end_session_dialog_confirmed_reboot_id = 0;
        }

        if (manager->priv->shell_end_session_dialog_open_failed_id != 0) {
                g_signal_handler_disconnect (manager->priv->shell,
                                             manager->priv->shell_end_session_dialog_open_failed_id);
                manager->priv->shell_end_session_dialog_open_failed_id = 0;
        }
}

static void
on_shell_end_session_dialog_canceled (GsmShell   *shell,
                                      GsmManager *manager)
{
        cancel_end_session (manager);
        disconnect_shell_dialog_signals (manager);
}

static void
_handle_end_session_dialog_response (GsmManager           *manager,
                                     GsmManagerLogoutType  logout_type)
{
        /* Note we're checking for END_SESSION here and
         * QUERY_END_SESSION in the fallback cases elsewhere.
         *
         * That's because they run at different times in the logout
         * process. The shell combines the inhibit and
         * confirmation dialogs, so it gets displayed after we've collected
         * inhibitors. The fallback code has two distinct dialogs, once of
         * which we can (and do show) before collecting the inhibitors.
         */
        if (manager->priv->phase >= GSM_MANAGER_PHASE_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        manager->priv->logout_mode = GSM_MANAGER_LOGOUT_MODE_FORCE;
        manager->priv->logout_type = logout_type;
        end_phase (manager);
}

static void
on_shell_end_session_dialog_confirmed_logout (GsmShell   *shell,
                                              GsmManager *manager)
{
        _handle_end_session_dialog_response (manager, GSM_MANAGER_LOGOUT_LOGOUT);
        disconnect_shell_dialog_signals (manager);
}

static void
on_shell_end_session_dialog_confirmed_shutdown (GsmShell   *shell,
                                                GsmManager *manager)
{
        _handle_end_session_dialog_response (manager, GSM_MANAGER_LOGOUT_SHUTDOWN);
        disconnect_shell_dialog_signals (manager);
}

static void
on_shell_end_session_dialog_confirmed_reboot (GsmShell   *shell,
                                              GsmManager *manager)
{
        _handle_end_session_dialog_response (manager, GSM_MANAGER_LOGOUT_REBOOT);
        disconnect_shell_dialog_signals (manager);
}

static void
connect_shell_dialog_signals (GsmManager *manager)
{
        if (manager->priv->shell_end_session_dialog_canceled_id != 0)
                return;

        manager->priv->shell_end_session_dialog_canceled_id =
                g_signal_connect (manager->priv->shell,
                                  "end-session-dialog-canceled",
                                  G_CALLBACK (on_shell_end_session_dialog_canceled),
                                  manager);

        manager->priv->shell_end_session_dialog_open_failed_id =
                g_signal_connect (manager->priv->shell,
                                  "end-session-dialog-open-failed",
                                  G_CALLBACK (on_shell_end_session_dialog_canceled),
                                  manager);

        manager->priv->shell_end_session_dialog_confirmed_logout_id =
                g_signal_connect (manager->priv->shell,
                                  "end-session-dialog-confirmed-logout",
                                  G_CALLBACK (on_shell_end_session_dialog_confirmed_logout),
                                  manager);

        manager->priv->shell_end_session_dialog_confirmed_shutdown_id =
                g_signal_connect (manager->priv->shell,
                                  "end-session-dialog-confirmed-shutdown",
                                  G_CALLBACK (on_shell_end_session_dialog_confirmed_shutdown),
                                  manager);

        manager->priv->shell_end_session_dialog_confirmed_reboot_id =
                g_signal_connect (manager->priv->shell,
                                  "end-session-dialog-confirmed-reboot",
                                  G_CALLBACK (on_shell_end_session_dialog_confirmed_reboot),
                                  manager);
}

static void
show_shell_end_session_dialog (GsmManager                   *manager,
                               GsmShellEndSessionDialogType  type)
{
        if (!gsm_shell_is_running (manager->priv->shell))
                return;

        gsm_shell_open_end_session_dialog (manager->priv->shell,
                                           type,
                                           manager->priv->inhibitors);
        connect_shell_dialog_signals (manager);
}

static void
user_logout (GsmManager           *manager,
             GsmManagerLogoutMode  mode)
{
        gboolean logout_prompt;
        gboolean shell_running;

        if (manager->priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        shell_running = gsm_shell_is_running (manager->priv->shell);
        logout_prompt = g_settings_get_boolean (manager->priv->settings,
                                                KEY_LOGOUT_PROMPT);

        /* If the shell isn't running, and this isn't a non-interative logout request,
         * and the user has their settings configured to show a confirmation dialog for
         * logout, then go ahead and show the fallback confirmation dialog now.
         *
         * If the shell is running, then the confirmation dialog and inhibitor dialog are
         * combined, so we'll show it at a later stage in the logout process.
         */
        if (!shell_running && mode == GSM_MANAGER_LOGOUT_MODE_NORMAL && logout_prompt) {
                show_fallback_logout_dialog (manager);
        } else {
                request_logout (manager, mode);
        }
}

/*
  dbus-send --session --type=method_call --print-reply
      --dest=org.gnome.SessionManager
      /org/gnome/SessionManager
      org.freedesktop.DBus.Introspectable.Introspect
*/

gboolean
gsm_manager_set_phase (GsmManager      *manager,
                       GsmManagerPhase  phase)
{
        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);
        manager->priv->phase = phase;
        return (TRUE);
}

static gboolean
_log_out_is_locked_down (GsmManager *manager)
{
        return g_settings_get_boolean (manager->priv->lockdown_settings,
                                       KEY_DISABLE_LOG_OUT);
}

static gboolean
_switch_user_is_locked_down (GsmManager *manager)
{
        return g_settings_get_boolean (manager->priv->lockdown_settings,
                                       KEY_DISABLE_USER_SWITCHING);
}

gboolean
gsm_manager_shutdown (GsmManager *manager,
                      GError    **error)
{
        gboolean shell_running;

        g_debug ("GsmManager: Shutdown called");

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        if (manager->priv->phase != GSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Shutdown interface is only available during the Running phase");
                return FALSE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_LOCKED_DOWN,
                             "Logout has been locked down");
                return FALSE;
        }

        shell_running = gsm_shell_is_running (manager->priv->shell);

        if (!shell_running)
                show_fallback_shutdown_dialog (manager, FALSE);
        else
                request_shutdown (manager);

        return TRUE;
}

gboolean
gsm_manager_reboot (GsmManager  *manager,
                    GError     **error)
{
        gboolean shell_running;

        g_debug ("GsmManager: Reboot called");

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        if (manager->priv->phase != GSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Reboot interface is only available during the Running phase");
                return FALSE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_LOCKED_DOWN,
                             "Logout has been locked down");
                return FALSE;
        }

        shell_running = gsm_shell_is_running (manager->priv->shell);

        if (!shell_running)
                show_fallback_shutdown_dialog (manager, TRUE);
        else
                request_reboot (manager);

        return TRUE;
}


gboolean
gsm_manager_can_shutdown (GsmManager *manager,
                          gboolean   *shutdown_available,
                          GError    **error)
{
        g_debug ("GsmManager: CanShutdown called");

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        *shutdown_available = !_log_out_is_locked_down (manager) &&
                              (gsm_system_can_stop (manager->priv->system)
                               || gsm_system_can_restart (manager->priv->system)
                               || gsm_system_can_suspend (manager->priv->system)
                               || gsm_system_can_hibernate (manager->priv->system));

        return TRUE;
}

gboolean
gsm_manager_logout (GsmManager *manager,
                    guint       logout_mode,
                    GError    **error)
{
        g_debug ("GsmManager: Logout called");

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        if (manager->priv->phase != GSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Logout interface is only available during the Running phase");
                return FALSE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_LOCKED_DOWN,
                             "Logout has been locked down");
                return FALSE;
        }

        switch (logout_mode) {
        case GSM_MANAGER_LOGOUT_MODE_NORMAL:
        case GSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
        case GSM_MANAGER_LOGOUT_MODE_FORCE:
                user_logout (manager, logout_mode);
                break;

        default:
                g_debug ("Unknown logout mode option");

                g_set_error (error,
                             GSM_MANAGER_ERROR,
                             GSM_MANAGER_ERROR_INVALID_OPTION,
                             "Unknown logout mode flag");
                return FALSE;
        }

        return TRUE;
}

gboolean
gsm_manager_register_client (GsmManager            *manager,
                             const char            *app_id,
                             const char            *startup_id,
                             DBusGMethodInvocation *context)
{
        char      *new_startup_id;
        char      *sender;
        GsmClient *client;
        GsmApp    *app;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        app = NULL;
        client = NULL;

        g_debug ("GsmManager: RegisterClient %s", startup_id);

        if (manager->priv->phase >= GSM_MANAGER_PHASE_QUERY_END_SESSION) {
                GError *new_error;

                g_debug ("Unable to register client: shutting down");

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                         "Unable to register client");
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (IS_STRING_EMPTY (startup_id)) {
                new_startup_id = gsm_util_generate_startup_id ();
        } else {

                client = (GsmClient *)gsm_store_find (manager->priv->clients,
                                                      (GsmStoreFunc)_client_has_startup_id,
                                                      (char *)startup_id);
                /* We can't have two clients with the same startup id. */
                if (client != NULL) {
                        GError *new_error;

                        g_debug ("Unable to register client: already registered");

                        new_error = g_error_new (GSM_MANAGER_ERROR,
                                                 GSM_MANAGER_ERROR_ALREADY_REGISTERED,
                                                 "Unable to register client");
                        dbus_g_method_return_error (context, new_error);
                        g_error_free (new_error);
                        return FALSE;
                }

                new_startup_id = g_strdup (startup_id);
        }

        g_debug ("GsmManager: Adding new client %s to session", new_startup_id);

        if (app == NULL && !IS_STRING_EMPTY (startup_id)) {
                app = find_app_for_startup_id (manager, startup_id);
        }
        if (app == NULL && !IS_STRING_EMPTY (app_id)) {
                /* try to associate this app id with a known app */
                app = find_app_for_app_id (manager, app_id);
        }

        sender = dbus_g_method_get_sender (context);
        client = gsm_dbus_client_new (new_startup_id, sender);
        g_free (sender);
        if (client == NULL) {
                GError *new_error;

                g_debug ("Unable to create client");

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Unable to register client");
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        gsm_store_add (manager->priv->clients, gsm_client_peek_id (client), G_OBJECT (client));
        /* the store will own the ref */
        g_object_unref (client);

        if (app != NULL) {
                gsm_client_set_app_id (client, gsm_app_peek_app_id (app));
                gsm_app_registered (app);
        } else {
                /* if an app id is specified store it in the client
                   so we can save it later */
                gsm_client_set_app_id (client, app_id);
        }

        gsm_client_set_status (client, GSM_CLIENT_REGISTERED);

        g_assert (new_startup_id != NULL);
        g_free (new_startup_id);

        dbus_g_method_return (context, gsm_client_peek_id (client));

        return TRUE;
}

gboolean
gsm_manager_unregister_client (GsmManager            *manager,
                               const char            *client_id,
                               DBusGMethodInvocation *context)
{
        GsmClient *client;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        g_debug ("GsmManager: UnregisterClient %s", client_id);

        client = (GsmClient *)gsm_store_lookup (manager->priv->clients, client_id);
        if (client == NULL) {
                GError *new_error;

                g_debug ("Unable to unregister client: not registered");

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_NOT_REGISTERED,
                                         "Unable to unregister client");
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        /* don't disconnect client here, only change the status.
           Wait until it leaves the bus before disconnecting it */
        gsm_client_set_status (client, GSM_CLIENT_UNREGISTERED);

        dbus_g_method_return (context);

        return TRUE;
}

gboolean
gsm_manager_inhibit (GsmManager            *manager,
                     const char            *app_id,
                     guint                  toplevel_xid,
                     const char            *reason,
                     guint                  flags,
                     DBusGMethodInvocation *context)
{
        GsmInhibitor *inhibitor;
        guint         cookie;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        g_debug ("GsmManager: Inhibit xid=%u app_id=%s reason=%s flags=%u",
                 toplevel_xid,
                 app_id,
                 reason,
                 flags);

        if (manager->priv->logout_mode == GSM_MANAGER_LOGOUT_MODE_FORCE) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Forced logout cannot be inhibited");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (IS_STRING_EMPTY (app_id)) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Application ID not specified");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (IS_STRING_EMPTY (reason)) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Reason not specified");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        if (flags == 0) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Invalid inhibit flags");
                g_debug ("GsmManager: Unable to inhibit: %s", new_error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);
                return FALSE;
        }

        cookie = _generate_unique_cookie (manager);
        inhibitor = gsm_inhibitor_new (app_id,
                                       toplevel_xid,
                                       flags,
                                       reason,
                                       dbus_g_method_get_sender (context),
                                       cookie);
        gsm_store_add (manager->priv->inhibitors, gsm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
        g_object_unref (inhibitor);

        dbus_g_method_return (context, cookie);

        return TRUE;
}

gboolean
gsm_manager_uninhibit (GsmManager            *manager,
                       guint                  cookie,
                       DBusGMethodInvocation *context)
{
        GsmInhibitor *inhibitor;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        g_debug ("GsmManager: Uninhibit %u", cookie);

        inhibitor = (GsmInhibitor *)gsm_store_find (manager->priv->inhibitors,
                                                    (GsmStoreFunc)_find_by_cookie,
                                                    &cookie);
        if (inhibitor == NULL) {
                GError *new_error;

                new_error = g_error_new (GSM_MANAGER_ERROR,
                                         GSM_MANAGER_ERROR_GENERAL,
                                         "Unable to uninhibit: Invalid cookie");
                dbus_g_method_return_error (context, new_error);
                g_debug ("Unable to uninhibit: %s", new_error->message);
                g_error_free (new_error);
                return FALSE;
        }

        g_debug ("GsmManager: removing inhibitor %s %u reason '%s' %u connection %s",
                 gsm_inhibitor_peek_app_id (inhibitor),
                 gsm_inhibitor_peek_toplevel_xid (inhibitor),
                 gsm_inhibitor_peek_reason (inhibitor),
                 gsm_inhibitor_peek_flags (inhibitor),
                 gsm_inhibitor_peek_bus_name (inhibitor));

        gsm_store_remove (manager->priv->inhibitors, gsm_inhibitor_peek_id (inhibitor));

        dbus_g_method_return (context);

        return TRUE;
}

gboolean
gsm_manager_is_inhibited (GsmManager *manager,
                          guint       flags,
                          gboolean   *is_inhibited,
                          GError     *error)
{
        GsmInhibitor *inhibitor;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        if (manager->priv->inhibitors == NULL
            || gsm_store_size (manager->priv->inhibitors) == 0) {
                *is_inhibited = FALSE;
                return TRUE;
        }

        inhibitor = (GsmInhibitor *) gsm_store_find (manager->priv->inhibitors,
                                                     (GsmStoreFunc)inhibitor_has_flag,
                                                     GUINT_TO_POINTER (flags));
        if (inhibitor == NULL) {
                *is_inhibited = FALSE;
        } else {
                *is_inhibited = TRUE;
        }

        return TRUE;

}

static gboolean
listify_store_ids (char       *id,
                   GObject    *object,
                   GPtrArray **array)
{
        g_ptr_array_add (*array, g_strdup (id));
        return FALSE;
}

gboolean
gsm_manager_get_clients (GsmManager *manager,
                         GPtrArray **clients,
                         GError    **error)
{
        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        if (clients == NULL) {
                return FALSE;
        }

        *clients = g_ptr_array_new ();
        gsm_store_foreach (manager->priv->clients, (GsmStoreFunc)listify_store_ids, clients);

        return TRUE;
}

gboolean
gsm_manager_get_inhibitors (GsmManager *manager,
                            GPtrArray **inhibitors,
                            GError    **error)
{
        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        if (inhibitors == NULL) {
                return FALSE;
        }

        *inhibitors = g_ptr_array_new ();
        gsm_store_foreach (manager->priv->inhibitors,
                           (GsmStoreFunc) listify_store_ids,
                           inhibitors);

        return TRUE;
}


static gboolean
_app_has_autostart_condition (const char *id,
                              GsmApp     *app,
                              const char *condition)
{
        gboolean has;
        gboolean disabled;

        has = gsm_app_has_autostart_condition (app, condition);
        disabled = gsm_app_peek_is_disabled (app);

        return has && !disabled;
}

gboolean
gsm_manager_is_autostart_condition_handled (GsmManager *manager,
                                            const char *condition,
                                            gboolean   *handled,
                                            GError    **error)
{
        GsmApp *app;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        app = (GsmApp *) gsm_store_find (manager->priv->apps,(
                                         GsmStoreFunc) _app_has_autostart_condition,
                                         (char *)condition);

        if (app != NULL) {
                *handled = TRUE;
        } else {
                *handled = FALSE;
        }

        return TRUE;
}

static void
append_app (GsmManager *manager,
            GsmApp     *app,
            const char *provides,
            gboolean    is_required)
{
        const char *id;
        const char *app_id;
        GsmApp     *dup;

        id = gsm_app_peek_id (app);
        if (IS_STRING_EMPTY (id)) {
                g_debug ("GsmManager: not adding app: no id");
                return;
        }

        dup = (GsmApp *)gsm_store_lookup (manager->priv->apps, id);
        if (dup != NULL) {
                g_debug ("GsmManager: not adding app: already added");
                return;
        }

        app_id = gsm_app_peek_app_id (app);
        if (IS_STRING_EMPTY (app_id)) {
                g_debug ("GsmManager: not adding app: no app-id");
                return;
        }

        dup = find_app_for_app_id (manager, app_id);
        if (dup != NULL) {
                g_debug ("GsmManager: not adding app: app-id '%s' already exists", app_id);

                if (provides && GSM_IS_AUTOSTART_APP (dup))
                        gsm_autostart_app_add_provides (GSM_AUTOSTART_APP (dup), provides);

                if (is_required &&
                    !g_slist_find (manager->priv->required_apps, dup)) {
                        g_debug ("GsmManager: making app '%s' required", gsm_app_peek_app_id (dup));
                        manager->priv->required_apps = g_slist_prepend (manager->priv->required_apps, dup);
                }

                return;
        }

        gsm_store_add (manager->priv->apps, id, G_OBJECT (app));
        if (is_required) {
                g_debug ("GsmManager: adding required app %s", gsm_app_peek_app_id (app));
                manager->priv->required_apps = g_slist_prepend (manager->priv->required_apps, app);
        }
}

static gboolean
add_autostart_app_internal (GsmManager *manager,
                            const char *path,
                            const char *provides,
                            gboolean    is_required)
{
        GsmApp  *app;
        char   **internal_provides;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (path != NULL, FALSE);

        /* Note: if we cannot add the app because its service is already
         * provided, because its app-id is taken, or because of any other
         * reason meaning there is already an app playing its role, then we
         * should make sure that relevant properties (like
         * provides/is_required) are set in the pre-existing app if needed. */

        /* first check to see if service is already provided */
        if (provides != NULL) {
                GsmApp *dup;

                dup = (GsmApp *)gsm_store_find (manager->priv->apps,
                                                (GsmStoreFunc)_find_app_provides,
                                                (char *)provides);
                if (dup != NULL) {
                        g_debug ("GsmManager: service '%s' is already provided", provides);

                        if (is_required &&
                            !g_slist_find (manager->priv->required_apps, dup)) {
                                g_debug ("GsmManager: making app '%s' required", gsm_app_peek_app_id (dup));
                                manager->priv->required_apps = g_slist_prepend (manager->priv->required_apps, dup);
                        }

                        return FALSE;
                }
        }

        app = gsm_autostart_app_new (path);
        if (app == NULL) {
                g_warning ("could not read %s", path);
                return FALSE;
        }

        internal_provides = gsm_app_get_provides (app);
        if (internal_provides) {
                int i;
                gboolean provided = FALSE;

                for (i = 0; internal_provides[i] != NULL; i++) {
                        GsmApp *dup;

                        dup = (GsmApp *)gsm_store_find (manager->priv->apps,
                                                        (GsmStoreFunc)_find_app_provides,
                                                        (char *)internal_provides[i]);
                        if (dup != NULL) {
                                g_debug ("GsmManager: service '%s' is already provided", internal_provides[i]);

                                if (is_required &&
                                    !g_slist_find (manager->priv->required_apps, dup)) {
                                        g_debug ("GsmManager: making app '%s' required", gsm_app_peek_app_id (dup));
                                        manager->priv->required_apps = g_slist_prepend (manager->priv->required_apps, dup);
                                }

                                provided = TRUE;
                                break;
                        }
                }

                g_strfreev (internal_provides);

                if (provided) {
                        g_object_unref (app);
                        return FALSE;
                }
        }

        if (provides)
                gsm_autostart_app_add_provides (GSM_AUTOSTART_APP (app), provides);

        g_debug ("GsmManager: read %s", path);
        append_app (manager, app, provides, is_required);
        g_object_unref (app);

        return TRUE;
}

gboolean
gsm_manager_add_autostart_app (GsmManager *manager,
                               const char *path,
                               const char *provides)
{
        return add_autostart_app_internal (manager,
                                           path,
                                           provides,
                                           FALSE);
}

/**
 * gsm_manager_add_required_app:
 * @manager: a #GsmManager
 * @path: Path to desktop file
 * @provides: What the component provides, as a space separated list
 *
 * Similar to gsm_manager_add_autostart_app(), except marks the
 * component as being required; we then try harder to ensure
 * it's running and inform the user if we can't.
 *
 */
gboolean
gsm_manager_add_required_app (GsmManager *manager,
                              const char *path,
                              const char *provides)
{
        return add_autostart_app_internal (manager,
                                           path,
                                           provides,
                                           TRUE);
}


gboolean
gsm_manager_add_autostart_apps_from_dir (GsmManager *manager,
                                         const char *path)
{
        GDir       *dir;
        const char *name;

        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (path != NULL, FALSE);

        g_debug ("GsmManager: *** Adding autostart apps for %s", path);

        dir = g_dir_open (path, 0, NULL);
        if (dir == NULL) {
                return FALSE;
        }

        while ((name = g_dir_read_name (dir))) {
                char *desktop_file;

                if (!g_str_has_suffix (name, ".desktop")) {
                        continue;
                }

                desktop_file = g_build_filename (path, name, NULL);
                gsm_manager_add_autostart_app (manager, desktop_file, NULL);
                g_free (desktop_file);
        }

        g_dir_close (dir);

        return TRUE;
}

gboolean
gsm_manager_is_session_running (GsmManager *manager,
                                gboolean   *running,
                                GError    **error)
{
        g_return_val_if_fail (GSM_IS_MANAGER (manager), FALSE);

        *running = (manager->priv->phase == GSM_MANAGER_PHASE_RUNNING);
        return TRUE;
}

