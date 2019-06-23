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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA 02110-1335, USA.
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

#include <gtk/gtk.h> /* for logout dialog */

#include <canberra.h>

#include "csm-manager.h"
#include "csm-exported-manager.h"

#include "csm-store.h"
#include "csm-inhibitor.h"
#include "csm-presence.h"

#include "csm-xsmp-server.h"
#include "csm-xsmp-client.h"
#include "csm-dbus-client.h"

#include "csm-autostart-app.h"

#include "csm-util.h"
#include "mdm.h"
#include "csm-logout-dialog.h"
#include "csm-fail-whale-dialog.h"
#include "csm-icon-names.h"
#include "csm-inhibit-dialog.h"
#include "csm-system.h"
#include "csm-session-save.h"

#define CSM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_MANAGER, CsmManagerPrivate))

#define CSM_MANAGER_DBUS_PATH "/org/gnome/SessionManager"
#define CSM_MANAGER_DBUS_NAME "org.gnome.SessionManager"

/* Probably about the longest amount of time someone could reasonably
 * want to wait, at least for something happening more than once.
 * We can get deployed on very slow media though like CDROM devices,
 * often with complex stacking/compressing filesystems on top, which
 * is not a recipie for speed.   Particularly now that we throw up
 * a fail whale if required components don't show up quickly enough,
 * let's make this fairly long.
 */
#define CSM_MANAGER_PHASE_TIMEOUT 30 /* seconds */

#define MDM_FLEXISERVER_COMMAND "mdmflexiserver"
#define MDM_FLEXISERVER_ARGS    "--startnew Standard"

#define GDM_FLEXISERVER_COMMAND "gdmflexiserver"
#define GDM_FLEXISERVER_ARGS    "--startnew Standard"

#define SESSION_SCHEMA            "org.cinnamon.desktop.session"
#define KEY_IDLE_DELAY            "idle-delay"
#define KEY_SESSION_NAME          "session-name"

#define CSM_MANAGER_SCHEMA        "org.cinnamon.SessionManager"
#define KEY_AUTOSAVE              "auto-save-session"
#define KEY_LOGOUT_PROMPT         "logout-prompt"
#define KEY_SHOW_FALLBACK_WARNING "show-fallback-warning"
#define KEY_BLACKLIST             "autostart-blacklist"
#define KEY_PREFER_HYBRID_SLEEP   "prefer-hybrid-sleep"

#define POWER_SETTINGS_SCHEMA     "org.cinnamon.settings-daemon.plugins.power"
#define KEY_LOCK_ON_SUSPEND       "lock-on-suspend"

#define LOCKDOWN_SCHEMA           "org.cinnamon.desktop.lockdown"
#define KEY_DISABLE_LOG_OUT       "disable-log-out"
#define KEY_DISABLE_USER_SWITCHING "disable-user-switching"

static void app_registered (CsmApp     *app, CsmManager *manager);

typedef enum
{
        CSM_MANAGER_LOGOUT_NONE,
        CSM_MANAGER_LOGOUT_LOGOUT,
        CSM_MANAGER_LOGOUT_REBOOT,
        CSM_MANAGER_LOGOUT_REBOOT_INTERACT,
        CSM_MANAGER_LOGOUT_REBOOT_MDM,
        CSM_MANAGER_LOGOUT_SHUTDOWN,
        CSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT,
        CSM_MANAGER_LOGOUT_SHUTDOWN_MDM
} CsmManagerLogoutType;

struct CsmManagerPrivate
{
        gboolean                failsafe;
        CsmStore               *clients;
        CsmStore               *inhibitors;
        CsmInhibitorFlag        inhibited_actions;
        CsmStore               *apps;
        CsmPresence            *presence;
        CsmXsmpServer          *xsmp_server;

        char                   *session_name;
        gboolean                is_fallback_session : 1;

        /* Current status */
        CsmManagerPhase         phase;
        guint                   phase_timeout_id;
        GSList                 *required_apps;
        GSList                 *pending_apps;
        CsmManagerLogoutMode    logout_mode;
        GSList                 *query_clients;
        guint                   query_timeout_id;
        /* This is used for CSM_MANAGER_PHASE_END_SESSION only at the moment,
         * since it uses a sublist of all running client that replied in a
         * specific way */
        GSList                 *next_query_clients;
        /* This is the action that will be done just before we exit */
        CsmManagerLogoutType    logout_type;

        GtkWidget              *inhibit_dialog;

        /* List of clients which were disconnected due to disabled condition
         * and shouldn't be automatically restarted */
        GSList                 *condition_clients;

        GSettings              *settings;
        GSettings              *session_settings;
        GSettings              *power_settings;
        GSettings              *lockdown_settings;

        CsmSystem              *system;

        GDBusProxy             *bus_proxy;
        GDBusConnection        *connection;
        CsmExportedManager     *skeleton;

        gboolean                dbus_disconnected : 1;
        guint                   name_owner_id;

        ca_context             *ca;
        gboolean               logout_sound_is_playing;

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
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

static void     csm_manager_finalize    (GObject         *object);

static void     show_fallback_shutdown_dialog (CsmManager *manager,
                                               gboolean    is_reboot);

static void     show_fallback_logout_dialog   (CsmManager *manager);

static void     user_logout (CsmManager           *manager,
                             CsmManagerLogoutMode  mode);
static void     request_shutdown (CsmManager *manager);
static void     request_reboot (CsmManager *manager);


static void     maybe_save_session   (CsmManager *manager);
static void     maybe_play_logout_sound (CsmManager *manager);

static gboolean _log_out_is_locked_down     (CsmManager *manager);
static gboolean _switch_user_is_locked_down (CsmManager *manager);

static void     _handle_client_end_session_response (CsmManager *manager,
                                                     CsmClient  *client,
                                                     gboolean    is_ok,
                                                     gboolean    do_last,
                                                     gboolean    cancel,
                                                     const char *reason);
static gpointer manager_object = NULL;

G_DEFINE_TYPE (CsmManager, csm_manager, G_TYPE_OBJECT)

#define CSM_MANAGER_DBUS_IFACE "org.gnome.SessionManager"

static const GDBusErrorEntry csm_manager_error_entries[] = {
        { CSM_MANAGER_ERROR_GENERAL,               CSM_MANAGER_DBUS_IFACE ".GeneralError" },
        { CSM_MANAGER_ERROR_NOT_IN_INITIALIZATION, CSM_MANAGER_DBUS_IFACE ".NotInInitialization" },
        { CSM_MANAGER_ERROR_NOT_IN_RUNNING,        CSM_MANAGER_DBUS_IFACE ".NotInRunning" },
        { CSM_MANAGER_ERROR_ALREADY_REGISTERED,    CSM_MANAGER_DBUS_IFACE ".AlreadyRegistered" },
        { CSM_MANAGER_ERROR_NOT_REGISTERED,        CSM_MANAGER_DBUS_IFACE ".NotRegistered" },
        { CSM_MANAGER_ERROR_INVALID_OPTION,        CSM_MANAGER_DBUS_IFACE ".InvalidOption" },
        { CSM_MANAGER_ERROR_LOCKED_DOWN,           CSM_MANAGER_DBUS_IFACE ".LockedDown" }
};

GQuark
csm_manager_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("csm_manager_error",
                                            &quark_volatile,
                                            csm_manager_error_entries,
                                            G_N_ELEMENTS (csm_manager_error_entries));

        return quark_volatile;
}

static gboolean
start_app_or_warn (CsmManager *manager,
                   CsmApp     *app)
{
        gboolean res;
        GError *error = NULL;

        g_debug ("CsmManager: starting app '%s'", csm_app_peek_id (app));

        res = csm_app_start (app, &error);
        if (error != NULL) {
                g_warning ("Failed to start app: %s", error->message);
                g_clear_error (&error);
        }
        return res;
}

static gboolean
is_app_required (CsmManager *manager,
                 CsmApp     *app)
{
        return g_slist_find (manager->priv->required_apps, app) != NULL;
}

static void
on_required_app_failure (CsmManager  *manager,
                         CsmApp      *app)
{
        gboolean allow_logout;

        if (csm_system_is_login_session (manager->priv->system)) {
                allow_logout = FALSE;
        } else {
                allow_logout = !_log_out_is_locked_down (manager);
        }

        csm_fail_whale_dialog_we_failed (FALSE,
                                         allow_logout);
}

static gboolean
_debug_client (const char *id,
               CsmClient  *client,
               CsmManager *manager)
{
        g_debug ("CsmManager: Client %s", csm_client_peek_id (client));
        return FALSE;
}

static void
debug_clients (CsmManager *manager)
{
        csm_store_foreach (manager->priv->clients,
                           (CsmStoreFunc)_debug_client,
                           manager);
}

static gboolean
_debug_inhibitor (const char    *id,
                  CsmInhibitor  *inhibitor,
                  CsmManager    *manager)
{
        g_debug ("CsmManager: Inhibitor app:%s client:%s bus-name:%s reason:%s",
                 csm_inhibitor_peek_app_id (inhibitor),
                 csm_inhibitor_peek_client_id (inhibitor),
                 csm_inhibitor_peek_bus_name (inhibitor),
                 csm_inhibitor_peek_reason (inhibitor));
        return FALSE;
}

static void
debug_inhibitors (CsmManager *manager)
{
        csm_store_foreach (manager->priv->inhibitors,
                           (CsmStoreFunc)_debug_inhibitor,
                           manager);
}

static gboolean
_find_by_cookie (const char   *id,
                 CsmInhibitor *inhibitor,
                 guint        *cookie_ap)
{
        guint cookie_b;

        cookie_b = csm_inhibitor_peek_cookie (inhibitor);

        return (*cookie_ap == cookie_b);
}

static gboolean
_client_has_startup_id (const char *id,
                        CsmClient  *client,
                        const char *startup_id_a)
{
        const char *startup_id_b;

        startup_id_b = csm_client_peek_startup_id (client);
        if (IS_STRING_EMPTY (startup_id_b)) {
                return FALSE;
        }

        return (strcmp (startup_id_a, startup_id_b) == 0);
}

static void
app_condition_changed (CsmApp     *app,
                       gboolean    condition,
                       CsmManager *manager)
{
        CsmClient *client;

        g_debug ("CsmManager: app:%s condition changed condition:%d",
                 csm_app_peek_id (app),
                 condition);

        client = (CsmClient *)csm_store_find (manager->priv->clients,
                                              (CsmStoreFunc)_client_has_startup_id,
                                              (char *)csm_app_peek_startup_id (app));

        if (condition) {
                if (!csm_app_is_running (app) && client == NULL) {
                        start_app_or_warn (manager, app);
                } else {
                        g_debug ("CsmManager: not starting - app still running '%s'", csm_app_peek_id (app));
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

                        g_debug ("CsmManager: stopping client %s for app", csm_client_peek_id (client));

                        error = NULL;
                        res = csm_client_stop (client, &error);
                        if (! res) {
                                g_warning ("Not able to stop app client from its condition: %s",
                                           error->message);
                                g_error_free (error);
                        }
                } else {
                        g_debug ("CsmManager: stopping app %s", csm_app_peek_id (app));

                        /* If we don't have a client then we should try to kill the app ,
                         * if it is running */
                        error = NULL;
                        if (csm_app_is_running (app)) {
                                res = csm_app_stop (app, &error);
                                if (! res) {
                                         g_warning ("Not able to stop app from its condition: %s",
                                                    error->message);
                                         g_error_free (error);
                                }
                        }
                }
        }
}

static const char *
phase_num_to_name (guint phase)
{
        const char *name;

        switch (phase) {
        case CSM_MANAGER_PHASE_STARTUP:
                name = "STARTUP";
                break;
        case CSM_MANAGER_PHASE_EARLY_INITIALIZATION:
                name = "EARLY_INITIALIZATION";
                break;
        case CSM_MANAGER_PHASE_PRE_DISPLAY_SERVER:
                name = "PRE_DISPLAY_SERVER";
                break;
        case CSM_MANAGER_PHASE_INITIALIZATION:
                name = "INITIALIZATION";
                break;
        case CSM_MANAGER_PHASE_WINDOW_MANAGER:
                name = "WINDOW_MANAGER";
                break;
        case CSM_MANAGER_PHASE_PANEL:
                name = "PANEL";
                break;
        case CSM_MANAGER_PHASE_DESKTOP:
                name = "DESKTOP";
                break;
        case CSM_MANAGER_PHASE_APPLICATION:
                name = "APPLICATION";
                break;
        case CSM_MANAGER_PHASE_RUNNING:
                name = "RUNNING";
                break;
        case CSM_MANAGER_PHASE_QUERY_END_SESSION:
                name = "QUERY_END_SESSION";
                break;
        case CSM_MANAGER_PHASE_END_SESSION:
                name = "END_SESSION";
                break;
        case CSM_MANAGER_PHASE_EXIT:
                name = "EXIT";
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return name;
}

static void start_phase (CsmManager *manager);

static void
quit_request_failed (CsmSystem *system,
                        GError    *error,
                        gpointer   user_data)
{
        g_warning ("Using an MDM logout action to shutdown/reboot the system.");
        MdmLogoutAction fallback_action = GPOINTER_TO_INT (user_data);
        mdm_set_logout_action (fallback_action);
        gtk_main_quit ();
}

static void
csm_manager_quit (CsmManager *manager)
{
        /* See the comment in request_reboot() for some more details about how
         * this works. */

        switch (manager->priv->logout_type) {
        case CSM_MANAGER_LOGOUT_LOGOUT:
                gtk_main_quit ();
                break;
        case CSM_MANAGER_LOGOUT_REBOOT:
        case CSM_MANAGER_LOGOUT_REBOOT_INTERACT:
                g_warning ("Requesting system restart...");
                mdm_set_logout_action (MDM_LOGOUT_ACTION_NONE);
                g_signal_connect (manager->priv->system,
                                  "request-failed",
                                  G_CALLBACK (quit_request_failed),
                                  GINT_TO_POINTER (MDM_LOGOUT_ACTION_REBOOT));
                csm_system_attempt_restart (manager->priv->system);
                break;
        case CSM_MANAGER_LOGOUT_REBOOT_MDM:
                mdm_set_logout_action (MDM_LOGOUT_ACTION_REBOOT);
                gtk_main_quit ();
                break;
        case CSM_MANAGER_LOGOUT_SHUTDOWN:
        case CSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:  
                g_warning ("Requesting system shutdown...");
                mdm_set_logout_action (MDM_LOGOUT_ACTION_NONE);
                g_signal_connect (manager->priv->system,
                                  "request-failed",
                                  G_CALLBACK (quit_request_failed),
                                  GINT_TO_POINTER (MDM_LOGOUT_ACTION_SHUTDOWN));
                csm_system_attempt_stop (manager->priv->system);
                break;
        case CSM_MANAGER_LOGOUT_SHUTDOWN_MDM:
                mdm_set_logout_action (MDM_LOGOUT_ACTION_SHUTDOWN);
                gtk_main_quit ();
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
end_phase (CsmManager *manager)
{
        gboolean start_next_phase = TRUE;

        g_debug ("CsmManager: ending phase %s\n",
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
        case CSM_MANAGER_PHASE_STARTUP:
        case CSM_MANAGER_PHASE_EARLY_INITIALIZATION:
        case CSM_MANAGER_PHASE_PRE_DISPLAY_SERVER:
        case CSM_MANAGER_PHASE_INITIALIZATION:
        case CSM_MANAGER_PHASE_WINDOW_MANAGER:
        case CSM_MANAGER_PHASE_PANEL:
        case CSM_MANAGER_PHASE_DESKTOP:
        case CSM_MANAGER_PHASE_APPLICATION:
                break;
        case CSM_MANAGER_PHASE_RUNNING:
                if (_log_out_is_locked_down (manager)) {
                        g_warning ("Unable to logout: Logout has been locked down");
                        start_next_phase = FALSE;
                }
                break;
        case CSM_MANAGER_PHASE_QUERY_END_SESSION:
                break;
        case CSM_MANAGER_PHASE_END_SESSION:
                maybe_play_logout_sound (manager);
                maybe_save_session (manager);
                break;
        case CSM_MANAGER_PHASE_EXIT:
                start_next_phase = FALSE;
                csm_manager_quit (manager);
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
app_event_during_startup (CsmManager *manager,
                          CsmApp     *app)
{
        if (!(manager->priv->phase < CSM_MANAGER_PHASE_APPLICATION))
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
_restart_app (CsmManager *manager,
              CsmApp     *app)
{
        GError *error = NULL;

        if (!csm_app_restart (app, &error)) {
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
app_died (CsmApp     *app,
          int         signal,
          CsmManager *manager)
{
        g_warning ("Application '%s' killed by signal %d", csm_app_peek_app_id (app), signal);

        if (csm_app_peek_autorestart (app)) {
                g_debug ("Component '%s' is autorestart, ignoring died signal",
                         csm_app_peek_app_id (app));
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
app_exited (CsmApp     *app,
            guchar      exit_code,
            CsmManager *manager)
{
        g_debug ("App %s exited with %d", csm_app_peek_app_id (app), exit_code);

        /* Consider that non-success exit status means "crash" for required components */
        if (exit_code != 0 && is_app_required (manager, app)) {
                if (csm_app_peek_autorestart (app)) {
                        g_debug ("Component '%s' is autorestart, ignoring non-successful exit",
                                 csm_app_peek_app_id (app));
                        return;
                }

                _restart_app (manager, app);
        } else {
                app_event_during_startup (manager, app);
        }
}

static void
app_registered (CsmApp     *app,
                CsmManager *manager)
{
        g_debug ("App %s registered", csm_app_peek_app_id (app));

        app_event_during_startup (manager, app);
}

static gboolean
on_phase_timeout (CsmManager *manager)
{
        GSList *a;

        manager->priv->phase_timeout_id = 0;

        switch (manager->priv->phase) {
        case CSM_MANAGER_PHASE_STARTUP:
        case CSM_MANAGER_PHASE_EARLY_INITIALIZATION:
        case CSM_MANAGER_PHASE_PRE_DISPLAY_SERVER:
        case CSM_MANAGER_PHASE_INITIALIZATION:
        case CSM_MANAGER_PHASE_WINDOW_MANAGER:
        case CSM_MANAGER_PHASE_PANEL:
        case CSM_MANAGER_PHASE_DESKTOP:
        case CSM_MANAGER_PHASE_APPLICATION:
                for (a = manager->priv->pending_apps; a; a = a->next) {
                        CsmApp *app = a->data;
                        g_warning ("Application '%s' failed to register before timeout",
                                   csm_app_peek_app_id (app));
                        if (is_app_required (manager, app))
                                on_required_app_failure (manager, app);
                }
                break;
        case CSM_MANAGER_PHASE_RUNNING:
                break;
        case CSM_MANAGER_PHASE_QUERY_END_SESSION:
        case CSM_MANAGER_PHASE_END_SESSION:
                break;
        case CSM_MANAGER_PHASE_EXIT:
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        end_phase (manager);

        return FALSE;
}

static gboolean
_autostart_delay_timeout (CsmApp *app)
{
        GError *error = NULL;
        gboolean res;

        if (!csm_app_peek_is_disabled (app)
            && !csm_app_peek_is_conditionally_disabled (app)) {
                res = csm_app_start (app, &error);
                if (!res) {
                        if (error != NULL) {
                                g_warning ("Could not launch application '%s': %s",
                                           csm_app_peek_app_id (app),
                                           error->message);
                                g_error_free (error);
                        }
                }
        }

        g_object_unref (app);

        return FALSE;
}

static gboolean
_start_app (const char *id,
            CsmApp     *app,
            CsmManager *manager)
{
        int      delay;

        if (csm_app_peek_phase (app) != manager->priv->phase) {
                goto out;
        }

        /* Keep track of app autostart condition in order to react
         * accordingly in the future. */
        g_signal_connect (app,
                          "condition-changed",
                          G_CALLBACK (app_condition_changed),
                          manager);

        if (csm_app_peek_is_disabled (app)
            || csm_app_peek_is_conditionally_disabled (app)) {
                g_debug ("CsmManager: Skipping disabled app: %s", id);
                goto out;
        }

        delay = csm_app_peek_autostart_delay (app);
        if (delay > 0) {
                g_timeout_add_seconds (delay,
                                       (GSourceFunc)_autostart_delay_timeout,
                                       g_object_ref (app));
                g_debug ("CsmManager: %s is scheduled to start in %d seconds", id, delay);
                goto out;
        }

        if (!start_app_or_warn (manager, app))
                goto out;

        if (manager->priv->phase < CSM_MANAGER_PHASE_APPLICATION) {
                /* Historical note - apparently,
                 * e.g. gnome-settings-daemon used to "daemonize", and
                 * so cinnamon-session assumes process exit means "ok
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
do_phase_startup (CsmManager *manager)
{
        csm_store_foreach (manager->priv->apps,
                           (CsmStoreFunc)_start_app,
                           manager);

        if (manager->priv->pending_apps != NULL) {
                if (manager->priv->phase < CSM_MANAGER_PHASE_APPLICATION) {
                        manager->priv->phase_timeout_id = g_timeout_add_seconds (CSM_MANAGER_PHASE_TIMEOUT,
                                                                                 (GSourceFunc)on_phase_timeout,
                                                                                 manager);
                }
        } else {
                end_phase (manager);
        }
}

typedef struct {
        CsmManager *manager;
        guint       flags;
} ClientEndSessionData;


static gboolean
_client_end_session (CsmClient            *client,
                     ClientEndSessionData *data)
{
        gboolean ret;
        GError  *error;

        error = NULL;
        ret = csm_client_end_session (client, data->flags, &error);
        if (! ret) {
                g_warning ("Unable to query client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("CsmManager: adding client to end-session clients: %s", csm_client_peek_id (client));
                data->manager->priv->query_clients = g_slist_prepend (data->manager->priv->query_clients,
                                                                      client);
        }

        return FALSE;
}

static gboolean
_client_end_session_helper (const char           *id,
                            CsmClient            *client,
                            ClientEndSessionData *data)
{
        return _client_end_session (client, data);
}

static void
do_phase_end_session (CsmManager *manager)
{
        ClientEndSessionData data;

        data.manager = manager;
        data.flags = 0;

        if (manager->priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        if (csm_manager_get_autosave_enabled (manager)) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_SAVE;
        }

        if (manager->priv->phase_timeout_id > 0) {
                g_source_remove (manager->priv->phase_timeout_id);
                manager->priv->phase_timeout_id = 0;
        }

        if (csm_store_size (manager->priv->clients) > 0) {
                manager->priv->phase_timeout_id = g_timeout_add_seconds (CSM_MANAGER_PHASE_TIMEOUT,
                                                                         (GSourceFunc)on_phase_timeout,
                                                                         manager);

                csm_store_foreach (manager->priv->clients,
                                   (CsmStoreFunc)_client_end_session_helper,
                                   &data);
        } else {
                end_phase (manager);
        }
}

static void
do_phase_end_session_part_2 (CsmManager *manager)
{
        ClientEndSessionData data;

        data.manager = manager;
        data.flags = 0;

        if (manager->priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        if (csm_manager_get_autosave_enabled (manager)) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_SAVE;
        }
        data.flags |= CSM_CLIENT_END_SESSION_FLAG_LAST;

        /* keep the timeout that was started at the beginning of the
         * CSM_MANAGER_PHASE_END_SESSION phase */

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
              CsmClient  *client,
              gpointer    user_data)
{
        gboolean ret;
        GError  *error;

        error = NULL;
        ret = csm_client_stop (client, &error);
        if (! ret) {
                g_warning ("Unable to stop client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("CsmManager: stopped client: %s", csm_client_peek_id (client));
        }

        return FALSE;
}

static void
maybe_restart_user_bus (CsmManager *manager)
{
        CsmSystem *system;
        g_autoptr(GVariant) reply = NULL;
        g_autoptr(GError) error = NULL;
        const char *user_unit = "/usr/lib/systemd/user/sockets.target.wants/dbus.socket";

        if (!g_file_test (user_unit, G_FILE_TEST_EXISTS))
                return;

        if (manager->priv->dbus_disconnected)
                return;

        system = csm_get_system ();

        if (!csm_system_is_last_session_for_user (system))
                return;

        g_debug ("CsmManager: restarting user bus");

        reply = g_dbus_connection_call_sync (manager->priv->connection,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "TryRestartUnit",
                                             g_variant_new ("(ss)", "dbus.service", "replace"),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             &error);

        if (error != NULL) {
                g_debug ("CsmManager: restarting user bus failed: %s", error->message);
                g_error_free (error);
        }
}

static void
do_phase_exit (CsmManager *manager)
{
        if (csm_store_size (manager->priv->clients) > 0) {
                csm_store_foreach (manager->priv->clients,
                                   (CsmStoreFunc)_client_stop,
                                   NULL);
        }
        // maybe_restart_user_bus (manager);
        end_phase (manager);
}

static gboolean
_client_query_end_session (const char           *id,
                           CsmClient            *client,
                           ClientEndSessionData *data)
{
        gboolean ret;
        GError  *error;

        error = NULL;
        ret = csm_client_query_end_session (client, data->flags, &error);
        if (! ret) {
                g_warning ("Unable to query client: %s", error->message);
                g_error_free (error);
                /* FIXME: what should we do if we can't communicate with client? */
        } else {
                g_debug ("CsmManager: adding client to query clients: %s", csm_client_peek_id (client));
                data->manager->priv->query_clients = g_slist_prepend (data->manager->priv->query_clients,
                                                                      client);
        }

        return FALSE;
}

static gboolean
inhibitor_has_flag (gpointer      key,
                    CsmInhibitor *inhibitor,
                    gpointer      data)
{
        guint flag;
        guint flags;

        flag = GPOINTER_TO_UINT (data);

        flags = csm_inhibitor_peek_flags (inhibitor);

        return (flags & flag);
}

static gboolean
csm_manager_is_logout_inhibited (CsmManager *manager)
{
        CsmInhibitor *inhibitor;

        if (manager->priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                return FALSE;
        }

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (CsmInhibitor *)csm_store_find (manager->priv->inhibitors,
                                                    (CsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (CSM_INHIBITOR_FLAG_LOGOUT));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
csm_manager_is_idle_inhibited (CsmManager *manager)
{
        CsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (CsmInhibitor *)csm_store_find (manager->priv->inhibitors,
                                                    (CsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (CSM_INHIBITOR_FLAG_IDLE));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
_client_cancel_end_session (const char *id,
                            CsmClient  *client,
                            CsmManager *manager)
{
        gboolean res;
        GError  *error;

        error = NULL;
        res = csm_client_cancel_end_session (client, &error);
        if (! res) {
                g_warning ("Unable to cancel end session: %s", error->message);
                g_error_free (error);
        }

        return FALSE;
}

static gboolean
inhibitor_is_jit (gpointer      key,
                  CsmInhibitor *inhibitor,
                  CsmManager   *manager)
{
        gboolean    matches;
        const char *id;

        id = csm_inhibitor_peek_client_id (inhibitor);

        matches = (id != NULL && id[0] != '\0');

        return matches;
}

static void
cancel_end_session (CsmManager *manager)
{
        /* just ignore if received outside of shutdown */
        if (manager->priv->phase < CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        /* switch back to running phase */
        g_debug ("CsmManager: Cancelling the end of session");

        /* remove the dialog before we remove the inhibitors, else the dialog
         * will activate itself automatically when the last inhibitor will be
         * removed */
        if (manager->priv->inhibit_dialog)
                gtk_widget_destroy (GTK_WIDGET (manager->priv->inhibit_dialog));
        manager->priv->inhibit_dialog = NULL;

        /* clear all JIT inhibitors */
        csm_store_foreach_remove (manager->priv->inhibitors,
                                  (CsmStoreFunc)inhibitor_is_jit,
                                  (gpointer)manager);

        csm_store_foreach (manager->priv->clients,
                           (CsmStoreFunc)_client_cancel_end_session,
                           NULL);

        csm_manager_set_phase (manager, CSM_MANAGER_PHASE_RUNNING);
        manager->priv->logout_mode = CSM_MANAGER_LOGOUT_MODE_NORMAL;

        manager->priv->logout_type = CSM_MANAGER_LOGOUT_NONE;
        mdm_set_logout_action (MDM_LOGOUT_ACTION_NONE);

        start_phase (manager);
}

static gboolean
process_is_running (const char * name)
{
        int num_processes;
        gchar *command = g_strdup_printf ("pidof %s | wc -l", name);
        FILE *fp = popen(command, "r");
        fscanf(fp, "%d", &num_processes);
        pclose(fp);
        g_free (command);

        if (num_processes > 0) {
                return TRUE;
        } else {
                return FALSE;
        }
}

static gboolean
sleep_lock_is_enabled (CsmManager *manager)
{
        return g_settings_get_boolean (manager->priv->power_settings,
                                       KEY_LOCK_ON_SUSPEND);
}

static void
manager_perhaps_lock (CsmManager *manager)
{
        GError   *error;
        gboolean  ret;

        /* only lock if the user has selected 'lock-on-suspend' in power prefs */
        if (!sleep_lock_is_enabled (manager)) {
                return;
        }

        /* do this sync to ensure it's on the screen when we start suspending */
        error = NULL;
        ret = g_spawn_command_line_sync ("cinnamon-screensaver-command --lock", NULL, NULL, NULL, &error);
        if (!ret) {
                g_warning ("Couldn't lock screen: %s", error->message);
                g_error_free (error);
        }
}

static void
manager_switch_user (GdkDisplay *display,
                     CsmManager *manager)
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

        if (process_is_running("mdm")) {
                command = g_strdup_printf ("%s %s",
                                           MDM_FLEXISERVER_COMMAND,
                                           MDM_FLEXISERVER_ARGS);

                error = NULL;
                context = (GAppLaunchContext*) gdk_display_get_app_launch_context (display);
                app = g_app_info_create_from_commandline (command, MDM_FLEXISERVER_COMMAND, 0, &error);

                if (app) {
                        g_app_info_launch (app, NULL, context, &error);
                        g_object_unref (app);
                }

                g_free (command);
                g_object_unref (context);

                if (error) {
                        g_debug ("CsmManager: Unable to start MDM greeter: %s", error->message);
                        g_error_free (error);
                }
        } else if (process_is_running("gdm") || process_is_running("gdm3")) {
                command = g_strdup_printf ("%s %s",
                                           GDM_FLEXISERVER_COMMAND,
                                           GDM_FLEXISERVER_ARGS);

                error = NULL;
                context = (GAppLaunchContext*) gdk_display_get_app_launch_context (display);
                app = g_app_info_create_from_commandline (command, GDM_FLEXISERVER_COMMAND, 0, &error);

                if (app) {
                        manager_perhaps_lock (manager);
                        g_app_info_launch (app, NULL, context, &error);
                        g_object_unref (app);
                }

                g_free (command);
                g_object_unref (context);

                if (error) {
                        g_debug ("CsmManager: Unable to start GDM greeter: %s", error->message);
                        g_error_free (error);
                }
        } else if (g_getenv ("XDG_SEAT_PATH")) {
                GDBusProxyFlags flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START;
                GDBusProxy *proxy = NULL;
                error = NULL;

                proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                                      flags,
                                                      NULL,
                                                      "org.freedesktop.DisplayManager",
                                                      g_getenv ("XDG_SEAT_PATH"),
                                                      "org.freedesktop.DisplayManager.Seat",
                                                      NULL,
                                                      &error);
                if (proxy != NULL) {
                        manager_perhaps_lock (manager);
                        g_dbus_proxy_call (proxy,
                                           "SwitchToGreeter",
                                           g_variant_new ("()"),
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           NULL,
                                           NULL);
                        g_object_unref (proxy);
                } else {
                        g_debug ("CsmManager: Unable to start LightDM greeter: %s", error->message);
                        g_error_free (error);
                }
        }
}

static void
manager_attempt_hibernate (CsmManager *manager)
{
        /* lock the screen before we try anything.  If it all fails, at least the screen is locked
         * (if preferences dictate it) */
        manager_perhaps_lock (manager);

        if (csm_system_can_hibernate (manager->priv->system)) {
                csm_system_hibernate (manager->priv->system);
        }
}

static void
manager_attempt_suspend (CsmManager *manager)
{
        /* lock the screen before we try anything.  If it all fails, at least the screen is locked
         * (if preferences dictate it) */
        manager_perhaps_lock (manager);

        if (g_settings_get_boolean (manager->priv->settings, KEY_PREFER_HYBRID_SLEEP) &&
            csm_system_can_hybrid_sleep (manager->priv->system)) {
                csm_system_hybrid_sleep (manager->priv->system);
        } else if (csm_system_can_suspend (manager->priv->system)) {
                csm_system_suspend (manager->priv->system);
        }
}

static void
do_inhibit_dialog_action (GdkDisplay *display,
                          CsmManager *manager,
                          int         action)
{
        switch (action) {
        case CSM_LOGOUT_ACTION_SWITCH_USER:
                manager_switch_user (display, manager);
                break;
        case CSM_LOGOUT_ACTION_HIBERNATE:
                manager_attempt_hibernate (manager);
                break;
        case CSM_LOGOUT_ACTION_SLEEP:
                manager_attempt_suspend (manager);
                break;
        case CSM_LOGOUT_ACTION_SHUTDOWN:
        case CSM_LOGOUT_ACTION_REBOOT:
        case CSM_LOGOUT_ACTION_LOGOUT:
                manager->priv->logout_mode = CSM_MANAGER_LOGOUT_MODE_FORCE;
                end_phase (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
inhibit_dialog_response (CsmInhibitDialog *dialog,
                         guint             response_id,
                         CsmManager       *manager)
{
        GdkDisplay *display;
        int action;

        g_debug ("CsmManager: Inhibit dialog response: %d", response_id);

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
                if (action == CSM_LOGOUT_ACTION_LOGOUT
                    || action == CSM_LOGOUT_ACTION_SHUTDOWN
                    || action == CSM_LOGOUT_ACTION_REBOOT) {
                        cancel_end_session (manager);
                }
                break;
        case GTK_RESPONSE_ACCEPT:
                g_debug ("CsmManager: doing action %d", action);
                do_inhibit_dialog_action (display, manager, action);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
end_session_or_show_fallback_dialog (CsmManager *manager)
{
        CsmLogoutAction action;

        if (! csm_manager_is_logout_inhibited (manager)) {
                end_phase (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("CsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        switch (manager->priv->logout_type) {
        case CSM_MANAGER_LOGOUT_LOGOUT:
                action = CSM_LOGOUT_ACTION_LOGOUT;
                break;
        case CSM_MANAGER_LOGOUT_REBOOT:
        case CSM_MANAGER_LOGOUT_REBOOT_INTERACT:
        case CSM_MANAGER_LOGOUT_REBOOT_MDM:
                action = CSM_LOGOUT_ACTION_REBOOT;
                break;
        case CSM_MANAGER_LOGOUT_SHUTDOWN:
        case CSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT:
        case CSM_MANAGER_LOGOUT_SHUTDOWN_MDM:
                action = CSM_LOGOUT_ACTION_SHUTDOWN;
                break;
        default:
                g_warning ("Unexpected logout type %d when creating inhibit dialog",
                           manager->priv->logout_type);
                action = CSM_LOGOUT_ACTION_LOGOUT;
                break;
        }

        /* Note: CSM_LOGOUT_ACTION_SHUTDOWN and CSM_LOGOUT_ACTION_REBOOT are
         * actually handled the same way as CSM_LOGOUT_ACTION_LOGOUT in the
         * inhibit dialog; the action, if the button is clicked, will be to
         * simply go to the next phase. */
        manager->priv->inhibit_dialog = csm_inhibit_dialog_new (manager->priv->inhibitors,
                                                                manager->priv->clients,
                                                                action);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
query_end_session_complete (CsmManager *manager)
{

        g_debug ("CsmManager: query end session complete");

        /* Remove the timeout since this can be called from outside the timer
         * and we don't want to have it called twice */
        if (manager->priv->query_timeout_id > 0) {
                g_source_remove (manager->priv->query_timeout_id);
                manager->priv->query_timeout_id = 0;
        }
        end_session_or_show_fallback_dialog (manager);
}

static guint32
generate_cookie (void)
{
        guint32 cookie;

        cookie = (guint32)g_random_int_range (1, G_MAXINT32);

        return cookie;
}

static guint32
_generate_unique_cookie (CsmManager *manager)
{
        guint32 cookie;

        do {
                cookie = generate_cookie ();
        } while (csm_store_find (manager->priv->inhibitors, (CsmStoreFunc)_find_by_cookie, &cookie) != NULL);

        return cookie;
}

static gboolean
_on_query_end_session_timeout (CsmManager *manager)
{
        GSList *l;

        manager->priv->query_timeout_id = 0;

        g_debug ("CsmManager: query end session timed out");

        for (l = manager->priv->query_clients; l != NULL; l = l->next) {
                guint         cookie;
                CsmInhibitor *inhibitor;
                const char   *bus_name;
                char         *app_id;

                g_warning ("Client '%s' failed to reply before timeout",
                           csm_client_peek_id (l->data));

                /* Don't add "not responding" inhibitors if logout is forced
                 */
                if (manager->priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                        continue;
                }

                /* Add JIT inhibit for unresponsive client */
                if (CSM_IS_DBUS_CLIENT (l->data)) {
                        bus_name = csm_dbus_client_get_bus_name (l->data);
                } else {
                        bus_name = NULL;
                }

                app_id = g_strdup (csm_client_peek_app_id (l->data));
                if (IS_STRING_EMPTY (app_id)) {
                        /* XSMP clients don't give us an app id unless we start them */
                        g_free (app_id);
                        app_id = csm_client_get_app_name (l->data);
                }

                cookie = _generate_unique_cookie (manager);
                inhibitor = csm_inhibitor_new_for_client (csm_client_peek_id (l->data),
                                                          app_id,
                                                          CSM_INHIBITOR_FLAG_LOGOUT,
                                                          _("Not responding"),
                                                          bus_name,
                                                          cookie);
                g_free (app_id);
                csm_store_add (manager->priv->inhibitors, csm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        }

        g_slist_free (manager->priv->query_clients);
        manager->priv->query_clients = NULL;

        query_end_session_complete (manager);

        return FALSE;
}

static void
do_phase_query_end_session (CsmManager *manager)
{
        ClientEndSessionData data;

        data.manager = manager;
        data.flags = 0;

        if (manager->priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                data.flags |= CSM_CLIENT_END_SESSION_FLAG_FORCEFUL;
        }
        /* We only query if an app is ready to log out, so we don't use
         * CSM_CLIENT_END_SESSION_FLAG_SAVE here.
         */

        debug_clients (manager);
        g_debug ("CsmManager: sending query-end-session to clients (logout mode: %s)",
                 manager->priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_NORMAL? "normal" :
                 manager->priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE? "forceful":
                 "no confirmation");
        csm_store_foreach (manager->priv->clients,
                           (CsmStoreFunc)_client_query_end_session,
                           &data);

        /* This phase doesn't time out unless logout is forced. Typically, this
         * separate timer is only used to show UI. */
        manager->priv->query_timeout_id = g_timeout_add_seconds (1, (GSourceFunc)_on_query_end_session_timeout, manager);
}

static void
update_idle (CsmManager *manager)
{
        if (csm_manager_is_idle_inhibited (manager)) {
                csm_presence_set_idle_enabled (manager->priv->presence, FALSE);
        } else {
                csm_presence_set_idle_enabled (manager->priv->presence, TRUE);
        }
}

static void
start_phase (CsmManager *manager)
{

        g_debug ("CsmManager: starting phase %s\n",
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
        case CSM_MANAGER_PHASE_STARTUP:
        case CSM_MANAGER_PHASE_EARLY_INITIALIZATION:
        case CSM_MANAGER_PHASE_PRE_DISPLAY_SERVER:
        case CSM_MANAGER_PHASE_INITIALIZATION:
        case CSM_MANAGER_PHASE_WINDOW_MANAGER:
        case CSM_MANAGER_PHASE_PANEL:
        case CSM_MANAGER_PHASE_DESKTOP:
        case CSM_MANAGER_PHASE_APPLICATION:
                do_phase_startup (manager);
                break;
        case CSM_MANAGER_PHASE_RUNNING:
                csm_xsmp_server_start_accepting_new_clients (manager->priv->xsmp_server);
                csm_exported_manager_emit_session_running (manager->priv->skeleton);
                update_idle (manager);
                break;
        case CSM_MANAGER_PHASE_QUERY_END_SESSION:
                csm_xsmp_server_stop_accepting_new_clients (manager->priv->xsmp_server);
                do_phase_query_end_session (manager);
                break;
        case CSM_MANAGER_PHASE_END_SESSION:
                do_phase_end_session (manager);
                break;
        case CSM_MANAGER_PHASE_EXIT:
                do_phase_exit (manager);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static gboolean
_debug_app_for_phase (const char *id,
                      CsmApp     *app,
                      gpointer    data)
{
        guint phase;

        phase = GPOINTER_TO_UINT (data);

        if (csm_app_peek_phase (app) != phase) {
                return FALSE;
        }

        g_debug ("CsmManager:\tID: %s\tapp-id:%s\tis-disabled:%d\tis-conditionally-disabled:%d\tis-delayed:%d",
                 csm_app_peek_id (app),
                 csm_app_peek_app_id (app),
                 csm_app_peek_is_disabled (app),
                 csm_app_peek_is_conditionally_disabled (app),
                 (csm_app_peek_autostart_delay (app) > 0));

        return FALSE;
}

static void
debug_app_summary (CsmManager *manager)
{
        guint phase;

        g_debug ("CsmManager: App startup summary");
        for (phase = CSM_MANAGER_PHASE_EARLY_INITIALIZATION; phase < CSM_MANAGER_PHASE_RUNNING; phase++) {
                g_debug ("CsmManager: Phase %s", phase_num_to_name (phase));
                csm_store_foreach (manager->priv->apps,
                                   (CsmStoreFunc)_debug_app_for_phase,
                                   GUINT_TO_POINTER (phase));
        }
}

void
csm_manager_start (CsmManager *manager)
{
        g_debug ("CsmManager: CSM starting to manage");

        g_return_if_fail (CSM_IS_MANAGER (manager));

        csm_xsmp_server_start (manager->priv->xsmp_server);
        csm_manager_set_phase (manager, CSM_MANAGER_PHASE_EARLY_INITIALIZATION);
        debug_app_summary (manager);
        start_phase (manager);
}

const char *
_csm_manager_get_default_session (CsmManager     *manager)
{
        return g_settings_get_string (manager->priv->session_settings,
                                      KEY_SESSION_NAME);
}

void
_csm_manager_set_active_session (CsmManager     *manager,
                                 const char     *session_name,
                                 gboolean        is_fallback)
{
        g_free (manager->priv->session_name);
        manager->priv->session_name = g_strdup (session_name);
        manager->priv->is_fallback_session = is_fallback;

        csm_exported_manager_set_session_name (manager->priv->skeleton,
                                               session_name);
}

static gboolean
_app_has_app_id (const char   *id,
                 CsmApp       *app,
                 const char   *app_id_a)
{
        const char *app_id_b;

        app_id_b = csm_app_peek_app_id (app);
        return (app_id_b != NULL && strcmp (app_id_a, app_id_b) == 0);
}

static CsmApp *
find_app_for_app_id (CsmManager *manager,
                     const char *app_id)
{
        CsmApp *app;
        app = (CsmApp *)csm_store_find (manager->priv->apps,
                                        (CsmStoreFunc)_app_has_app_id,
                                        (char *)app_id);
        return app;
}

static gboolean
inhibitor_has_client_id (gpointer      key,
                         CsmInhibitor *inhibitor,
                         const char   *client_id_a)
{
        gboolean    matches;
        const char *client_id_b;

        client_id_b = csm_inhibitor_peek_client_id (inhibitor);

        matches = FALSE;
        if (! IS_STRING_EMPTY (client_id_a) && ! IS_STRING_EMPTY (client_id_b)) {
                matches = (strcmp (client_id_a, client_id_b) == 0);
                if (matches) {
                        g_debug ("CsmManager: removing JIT inhibitor for %s for reason '%s'",
                                 csm_inhibitor_peek_client_id (inhibitor),
                                 csm_inhibitor_peek_reason (inhibitor));
                }
        }

        return matches;
}

static gboolean
_app_has_startup_id (const char *id,
                     CsmApp     *app,
                     const char *startup_id_a)
{
        const char *startup_id_b;

        startup_id_b = csm_app_peek_startup_id (app);

        if (IS_STRING_EMPTY (startup_id_b)) {
                return FALSE;
        }

        return (strcmp (startup_id_a, startup_id_b) == 0);
}

static CsmApp *
find_app_for_startup_id (CsmManager *manager,
                        const char *startup_id)
{
        CsmApp *found_app;
        GSList *a;

        found_app = NULL;

        /* If we're starting up the session, try to match the new client
         * with one pending apps for the current phase. If not, try to match
         * with any of the autostarted apps. */
        if (manager->priv->phase < CSM_MANAGER_PHASE_APPLICATION) {
                for (a = manager->priv->pending_apps; a != NULL; a = a->next) {
                        CsmApp *app = CSM_APP (a->data);

                        if (strcmp (startup_id, csm_app_peek_startup_id (app)) == 0) {
                                found_app = app;
                                goto out;
                        }
                }
        } else {
                CsmApp *app;

                app = (CsmApp *)csm_store_find (manager->priv->apps,
                                                (CsmStoreFunc)_app_has_startup_id,
                                                (char *)startup_id);
                if (app != NULL) {
                        found_app = app;
                        goto out;
                }
        }
 out:
        return found_app;
}

static gboolean
csm_manager_setenv (CsmExportedManager    *skeleton,
                    GDBusMethodInvocation *invocation,
                    const gchar           *variable,
                    const gchar           *value,
                    CsmManager            *manager)
{
        if (!CSM_IS_MANAGER (manager)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "CSM_IS_MANAGER failed on csm_manager_setenv");

                return TRUE;
        }

        if (manager->priv->phase > CSM_MANAGER_PHASE_INITIALIZATION) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                                                       "Setenv interface is only available during the Initialization phase");

                return TRUE;
        }

        csm_util_setenv (variable, value);

        csm_exported_manager_complete_setenv (skeleton, invocation);

        return TRUE;
}

static gboolean
csm_manager_initialization_error (CsmExportedManager    *skeleton,
                                  GDBusMethodInvocation *invocation,
                                  const char            *message,
                                  gboolean               fatal,
                                  CsmManager            *manager)
{
        if (!CSM_IS_MANAGER (manager)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "CSM_IS_MANAGER failed on csm_manager_initialization_error");

                return TRUE;
        }

        if (manager->priv->phase > CSM_MANAGER_PHASE_INITIALIZATION) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
                                                       "InitializationError interface is only available during the Initialization phase");

                return TRUE;
        }

        csm_util_init_error (fatal, "%s", message);

        csm_exported_manager_complete_initialization_error (skeleton, invocation);

        return TRUE;
}

static gboolean
csm_manager_register_client (CsmExportedManager      *skeleton,
                             GDBusMethodInvocation   *invocation,
                             const char              *app_id,
                             const char              *startup_id,
                             CsmManager              *manager)
{
        char      *new_startup_id;
        const char *sender;
        CsmClient *client;
        CsmApp    *app;

        if (!CSM_IS_MANAGER (manager)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "CSM_IS_MANAGER failed on csm_manager_register_client");

                return TRUE;
        }

        app = NULL;
        client = NULL;

        g_debug ("CsmManager: RegisterClient %s", startup_id);

        if (manager->priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                g_debug ("Unable to register client: shutting down");

                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "Unable to register client");

                return TRUE;
        }

        if (IS_STRING_EMPTY (startup_id)) {
                new_startup_id = csm_util_generate_startup_id ();
        } else {
                client = (CsmClient *)csm_store_find (manager->priv->clients,
                                                      (CsmStoreFunc)_client_has_startup_id,
                                                      (char *)startup_id);
                /* We can't have two clients with the same startup id. */
                if (client != NULL) {
                        g_debug ("Unable to register client: already registered");

                        g_dbus_method_invocation_return_error (invocation,
                                                               CSM_MANAGER_ERROR,
                                                               CSM_MANAGER_ERROR_ALREADY_REGISTERED,
                                                               "Unable to register client");

                        return TRUE;
                }

                new_startup_id = g_strdup (startup_id);
        }

        g_debug ("CsmManager: Adding new client %s to session", new_startup_id);

        if (app == NULL && !IS_STRING_EMPTY (startup_id)) {
                app = find_app_for_startup_id (manager, startup_id);
        }
        if (app == NULL && !IS_STRING_EMPTY (app_id)) {
                /* try to associate this app id with a known app */
                app = find_app_for_app_id (manager, app_id);
        }

        sender = g_dbus_method_invocation_get_sender (invocation);
        client = csm_dbus_client_new (new_startup_id, sender);

        if (client == NULL) {
                g_debug ("Unable to create client");

                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "Unable to register client");

                return TRUE;
        }

        csm_store_add (manager->priv->clients, csm_client_peek_id (client), G_OBJECT (client));
        /* the store will own the ref */
        g_object_unref (client);

        if (app != NULL) {
                csm_client_set_app_id (client, csm_app_peek_app_id (app));
                csm_app_registered (app);
        } else {
                /* if an app id is specified store it in the client
                   so we can save it later */
                csm_client_set_app_id (client, app_id);
        }

        csm_client_set_status (client, CSM_CLIENT_REGISTERED);

        g_assert (new_startup_id != NULL);
        g_free (new_startup_id);

        csm_exported_manager_complete_register_client (skeleton,
                                                       invocation,
                                                       csm_client_peek_id (client));

        return TRUE;
}

static gboolean
csm_manager_unregister_client (CsmExportedManager     *skeleton,
                               GDBusMethodInvocation  *invocation,
                               const char             *client_id,
                               CsmManager             *manager)
{
        CsmClient *client;

        if (!CSM_IS_MANAGER (manager)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "CSM_IS_MANAGER failed on csm_manager_register_client");

                return TRUE;
        }

        g_debug ("CsmManager: UnregisterClient %s", client_id);

        client = (CsmClient *)csm_store_lookup (manager->priv->clients, client_id);
        if (client == NULL) {
                g_debug ("Unable to unregister client: not registered");

                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_NOT_REGISTERED,
                                                       "Unable to unregister client");

                return TRUE;
        }

        /* don't disconnect client here, only change the status.
           Wait until it leaves the bus before disconnecting it */
        csm_client_set_status (client, CSM_CLIENT_UNREGISTERED);

        csm_exported_manager_complete_unregister_client (skeleton,
                                                         invocation);

        return TRUE;
}

static gboolean
csm_manager_inhibit (CsmExportedManager      *skeleton,
                     GDBusMethodInvocation   *invocation,
                     const char              *app_id,
                     guint                    toplevel_xid,
                     const char              *reason,
                     guint                    flags,
                     CsmManager              *manager)
{
        CsmInhibitor *inhibitor;
        guint         cookie;

        g_debug ("CsmManager: Inhibit xid=%u app_id=%s reason=%s flags=%u",
                 toplevel_xid,
                 app_id,
                 reason,
                 flags);

        if (manager->priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_FORCE) {
                g_debug ("CsmManager: Unable to inhibit: Forced logout cannot be inhibited");

                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "Forced logout cannot be inhibited");

                return TRUE;
        }

        if (IS_STRING_EMPTY (app_id)) {
                g_debug ("CsmManager: Unable to inhibit: Application ID not specified");

                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "Application ID not specified");

                return TRUE;
        }

        if (IS_STRING_EMPTY (reason)) {
                g_debug ("CsmManager: Unable to inhibit: Reason not specific");

                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "Reason not specified");

                return TRUE;
        }

        if (flags == 0) {
                g_debug ("CsmManager: Unable to inhibit: Invalid inhibit flags");

                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "Invalid inhibit flags");

                return TRUE;
        }

        cookie = _generate_unique_cookie (manager);
        inhibitor = csm_inhibitor_new (app_id,
                                       toplevel_xid,
                                       flags,
                                       reason,
                                       g_dbus_method_invocation_get_sender (invocation),
                                       cookie);
        csm_store_add (manager->priv->inhibitors, csm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
        g_object_unref (inhibitor);

        csm_exported_manager_complete_inhibit (skeleton,
                                               invocation,
                                               cookie);

        return TRUE;
}

static gboolean
csm_manager_uninhibit (CsmExportedManager    *skeleton,
                       GDBusMethodInvocation *invocation,
                       guint                  cookie,
                       CsmManager            *manager)
{
        CsmInhibitor *inhibitor;

        g_debug ("CsmManager: Uninhibit %u", cookie);

        inhibitor = (CsmInhibitor *)csm_store_find (manager->priv->inhibitors,
                                                    (CsmStoreFunc)_find_by_cookie,
                                                    &cookie);
        if (inhibitor == NULL) {
                g_debug ("Unable to uninhibit: Invalid cookie");

                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_GENERAL,
                                                       "Unable to uninhibit: Invalid cookie");

                return TRUE;
        }

        g_debug ("CsmManager: removing inhibitor %s %u reason '%s' %u connection %s",
                 csm_inhibitor_peek_app_id (inhibitor),
                 csm_inhibitor_peek_toplevel_xid (inhibitor),
                 csm_inhibitor_peek_reason (inhibitor),
                 csm_inhibitor_peek_flags (inhibitor),
                 csm_inhibitor_peek_bus_name (inhibitor));

        csm_store_remove (manager->priv->inhibitors, csm_inhibitor_peek_id (inhibitor));

        csm_exported_manager_complete_uninhibit (skeleton,
                                                 invocation);

        return TRUE;
}

static gboolean
csm_manager_is_inhibited (CsmExportedManager    *skeleton,
                          GDBusMethodInvocation *invocation,
                          guint                  flags,
                          CsmManager            *manager)
{
        gboolean is_inhibited;

        is_inhibited = FALSE;

        if (manager->priv->inhibitors == NULL
            || csm_store_size (manager->priv->inhibitors) == 0) {
                is_inhibited = FALSE;
        } else {
                CsmInhibitor *inhibitor;

                inhibitor = (CsmInhibitor *) csm_store_find (manager->priv->inhibitors,
                                                             (CsmStoreFunc)inhibitor_has_flag,
                                                             GUINT_TO_POINTER (flags));

                is_inhibited = inhibitor != NULL;
        }

        csm_exported_manager_complete_is_inhibited (skeleton,
                                                    invocation,
                                                    is_inhibited);

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

static gboolean
csm_manager_get_clients (CsmExportedManager     *skeleton,
                         GDBusMethodInvocation  *invocation,
                         CsmManager             *manager)
{
        GPtrArray *clients;

        clients = g_ptr_array_new_with_free_func (g_free);

        csm_store_foreach (manager->priv->clients,
                           (CsmStoreFunc)listify_store_ids,
                           &clients);

        g_ptr_array_add (clients, NULL);

        csm_exported_manager_complete_get_clients (skeleton,
                                                   invocation,
                                                   (const gchar * const *) clients->pdata);
        g_ptr_array_unref (clients);

        return TRUE;
}

static gboolean
csm_manager_get_inhibitors (CsmExportedManager     *skeleton,
                            GDBusMethodInvocation  *invocation,
                            CsmManager             *manager)
{
        GPtrArray *inhibitors;

        inhibitors = g_ptr_array_new_with_free_func (g_free);

        csm_store_foreach (manager->priv->inhibitors,
                           (CsmStoreFunc)listify_store_ids,
                           &inhibitors);

        g_ptr_array_add (inhibitors, NULL);

        csm_exported_manager_complete_get_inhibitors (skeleton,
                                                      invocation,
                                                      (const gchar * const *) inhibitors->pdata);
        g_ptr_array_unref (inhibitors);

        return TRUE;
}

static gboolean
_app_has_autostart_condition (const char *id,
                              CsmApp     *app,
                              const char *condition)
{
        gboolean has;
        gboolean disabled;

        has = csm_app_has_autostart_condition (app, condition);
        disabled = csm_app_peek_is_disabled (app);

        return has && !disabled;
}

static gboolean
csm_manager_is_autostart_condition_handled (CsmExportedManager    *skeleton,
                                            GDBusMethodInvocation *invocation,
                                            const char            *condition,
                                            CsmManager            *manager)
{
        CsmApp *app;
        gboolean handled;

        app = (CsmApp *) csm_store_find (manager->priv->apps,(
                                         CsmStoreFunc) _app_has_autostart_condition,
                                         (char *)condition);

        if (app != NULL) {
                handled = TRUE;
        } else {
                handled = FALSE;
        }

        csm_exported_manager_complete_is_autostart_condition_handled (skeleton,
                                                                      invocation,
                                                                      handled);

        return TRUE;
}

static gboolean
csm_manager_shutdown (CsmExportedManager    *skeleton,
                      GDBusMethodInvocation *invocation,
                      CsmManager            *manager)
{
        g_debug ("CsmManager: Shutdown called");

        if (manager->priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "Shutdown interface is only available during the Running phase");

                return TRUE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_LOCKED_DOWN,
                                                       "Logout has been locked down");

                return TRUE;
        }

        show_fallback_shutdown_dialog (manager, FALSE);

        csm_exported_manager_complete_shutdown (skeleton,
                                                invocation);

        return TRUE;
}

static gboolean
csm_manager_reboot (CsmExportedManager     *skeleton,
                    GDBusMethodInvocation  *invocation,
                    CsmManager  *manager)
{
        g_debug ("CsmManager: Reboot called");

        if (manager->priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "Reboot interface is only available during the Running phase");

                return TRUE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_LOCKED_DOWN,
                                                       "Logout has been locked down");

                return TRUE;
        }
 
        show_fallback_shutdown_dialog (manager, TRUE);

        csm_exported_manager_complete_reboot (skeleton,
                                              invocation);

        return TRUE;
}

static gboolean
csm_manager_can_shutdown (CsmExportedManager    *skeleton,
                          GDBusMethodInvocation *invocation,
                          CsmManager            *manager)
{
        gboolean shutdown_available;

        g_debug ("CsmManager: CanShutdown called");

        shutdown_available = !_log_out_is_locked_down (manager) &&
                             (csm_system_can_stop (manager->priv->system)
                              || csm_system_can_restart (manager->priv->system)
                              || csm_system_can_suspend (manager->priv->system)
                              || csm_system_can_hibernate (manager->priv->system));

        csm_exported_manager_complete_can_shutdown (skeleton,
                                                    invocation,
                                                    shutdown_available);

        return TRUE;
}

static gboolean
csm_manager_logout_dbus (CsmExportedManager    *skeleton,
                         GDBusMethodInvocation *invocation,
                         guint                  logout_mode,
                         CsmManager            *manager)
{
        g_debug ("CsmManager: Logout called");

        if (manager->priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "Logout interface is only available during the Running phase");

                return TRUE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_LOCKED_DOWN,
                                                       "Logout has been locked down");

                return TRUE;
        }

        switch (logout_mode) {
        case CSM_MANAGER_LOGOUT_MODE_NORMAL:
        case CSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
        case CSM_MANAGER_LOGOUT_MODE_FORCE:
                user_logout (manager, logout_mode);
                break;
        default:
                g_debug ("Unknown logout mode option");

                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_INVALID_OPTION,
                                                       "Unknown logout mode flag");

                return TRUE;
        }

        csm_exported_manager_complete_logout (skeleton,
                                              invocation);

        return TRUE;
}

static gboolean
csm_manager_is_session_running (CsmExportedManager    *skeleton,
                                GDBusMethodInvocation *invocation,
                                CsmManager            *manager)
{

        csm_exported_manager_complete_is_session_running (skeleton,
                                                          invocation,
                                                          (manager->priv->phase == CSM_MANAGER_PHASE_RUNNING));

        return TRUE;
}

static gboolean
csm_manager_request_shutdown (CsmExportedManager    *skeleton,
                              GDBusMethodInvocation *invocation,
                              CsmManager            *manager)
{
        g_debug ("CsmManager: RequestShutdown called");

        if (manager->priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "RequestShutdown interface is only available during the Running phase");

                return TRUE;
        }

        request_shutdown (manager);

        csm_exported_manager_complete_request_shutdown (skeleton,
                                                        invocation);

        return TRUE;
}

static gboolean
csm_manager_request_reboot (CsmExportedManager     *skeleton,
                            GDBusMethodInvocation  *invocation,
                            CsmManager *manager)
{
        g_debug ("CsmManager: RequestReboot called");

        if (manager->priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_dbus_method_invocation_return_error (invocation,
                                                       CSM_MANAGER_ERROR,
                                                       CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                                                       "RequestReboot interface is only available during the Running phase");

                return TRUE;
        }

        request_reboot (manager);

        csm_exported_manager_complete_request_reboot (skeleton,
                                                      invocation);

        return TRUE;
}

static void
_disconnect_client (CsmManager *manager,
                    CsmClient  *client)
{
        gboolean              is_condition_client;
        CsmApp               *app;
        const char           *app_id;
        const char           *startup_id;
        gboolean              app_restart;
        CsmClientRestartStyle client_restart_hint;

        g_debug ("CsmManager: disconnect client: %s", csm_client_peek_id (client));

        /* take a ref so it doesn't get finalized */
        g_object_ref (client);

        csm_client_set_status (client, CSM_CLIENT_FINISHED);

        is_condition_client = FALSE;
        if (g_slist_find (manager->priv->condition_clients, client)) {
                manager->priv->condition_clients = g_slist_remove (manager->priv->condition_clients, client);

                is_condition_client = TRUE;
        }

        /* remove any inhibitors for this client */
        csm_store_foreach_remove (manager->priv->inhibitors,
                                  (CsmStoreFunc)inhibitor_has_client_id,
                                  (gpointer)csm_client_peek_id (client));

        app = NULL;

        /* first try to match on startup ID */
        startup_id = csm_client_peek_startup_id (client);
        if (! IS_STRING_EMPTY (startup_id)) {
                app = find_app_for_startup_id (manager, startup_id);

        }

        /* then try to find matching app-id */
        if (app == NULL) {
                app_id = csm_client_peek_app_id (client);
                if (! IS_STRING_EMPTY (app_id)) {
                        g_debug ("CsmManager: disconnect for app '%s'", app_id);
                        app = find_app_for_app_id (manager, app_id);
                }
        }

        if (manager->priv->phase == CSM_MANAGER_PHASE_QUERY_END_SESSION) {
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

        if (manager->priv->dbus_disconnected && CSM_IS_DBUS_CLIENT (client)) {
                g_debug ("CsmManager: dbus disconnected, not restarting application");
                goto out;
        }

        if (app == NULL) {
                g_debug ("CsmManager: unable to find application for client - not restarting");
                goto out;
        }

        if (manager->priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                g_debug ("CsmManager: in shutdown, not restarting application");
                goto out;
        }

        app_restart = csm_app_peek_autorestart (app);
        client_restart_hint = csm_client_peek_restart_style_hint (client);

        /* allow legacy clients to override the app info */
        if (! app_restart
            && client_restart_hint != CSM_CLIENT_RESTART_IMMEDIATELY) {
                g_debug ("CsmManager: autorestart not set, not restarting application");
                goto out;
        }

        if (is_condition_client) {
                g_debug ("CsmManager: app conditionally disabled, not restarting application");
                goto out;
        }

        g_debug ("CsmManager: restarting app");

        _restart_app (manager, app);

 out:
        g_object_unref (client);
}

typedef struct {
        const char *service_name;
        CsmManager *manager;
} RemoveClientData;

static gboolean
_disconnect_dbus_client (const char       *id,
                         CsmClient        *client,
                         RemoveClientData *data)
{
        const char *name;

        if (! CSM_IS_DBUS_CLIENT (client)) {
                return FALSE;
        }

        /* If no service name, then we simply disconnect all clients */
        if (!data->service_name) {
                _disconnect_client (data->manager, client);
                return TRUE;
        }

        name = csm_dbus_client_get_bus_name (CSM_DBUS_CLIENT (client));
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
 * @manager: a #CsmManager
 * @service_name: a service name
 *
 * Disconnects clients that own @service_name.
 *
 * If @service_name is NULL, then disconnects all clients for the connection.
 */
static void
remove_clients_for_connection (CsmManager *manager,
                               const char *service_name)
{
        RemoveClientData data;

        data.service_name = service_name;
        data.manager = manager;

        /* disconnect dbus clients for name */
        csm_store_foreach_remove (manager->priv->clients,
                                  (CsmStoreFunc)_disconnect_dbus_client,
                                  &data);

        if (manager->priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION
            && csm_store_size (manager->priv->clients) == 0) {
                g_debug ("CsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

static gboolean
inhibitor_has_bus_name (gpointer          key,
                        CsmInhibitor     *inhibitor,
                        RemoveClientData *data)
{
        gboolean    matches;
        const char *bus_name_b;

        bus_name_b = csm_inhibitor_peek_bus_name (inhibitor);

        matches = FALSE;
        if (! IS_STRING_EMPTY (data->service_name) && ! IS_STRING_EMPTY (bus_name_b)) {
                matches = (strcmp (data->service_name, bus_name_b) == 0);
                if (matches) {
                        g_debug ("CsmManager: removing inhibitor from %s for reason '%s' on connection %s",
                                 csm_inhibitor_peek_app_id (inhibitor),
                                 csm_inhibitor_peek_reason (inhibitor),
                                 csm_inhibitor_peek_bus_name (inhibitor));
                }
        }

        return matches;
}

static void
remove_inhibitors_for_connection (CsmManager *manager,
                                  const char *service_name)
{
        RemoveClientData data;

        data.service_name = service_name;
        data.manager = manager;

        debug_inhibitors (manager);

        csm_store_foreach_remove (manager->priv->inhibitors,
                                  (CsmStoreFunc)inhibitor_has_bus_name,
                                  &data);
}

static void
on_dbus_proxy_signal (GDBusProxy *proxy,
                      gchar      *sender_name,
                      gchar      *signal_name,
                      GVariant   *parameters,
                      gpointer    user_data)
{
    CsmManager *manager;
    gchar *name, *old_owner, *new_owner;

    manager = CSM_MANAGER (user_data);

    if (g_strcmp0 (signal_name, "NameOwnerChanged") != 0) {
            return;
    }

    g_variant_get (parameters, "(sss)",
                   &name,
                   &old_owner,
                   &new_owner);

    if (strlen (new_owner) == 0 && strlen (old_owner) > 0) {
                /* service removed */
                remove_inhibitors_for_connection (manager, old_owner);
                remove_clients_for_connection (manager, old_owner);
    } else if (strlen (old_owner) == 0 && strlen (new_owner) > 0) {
            /* service added */

            /* use this if we support automatically registering
             * well known bus names */
    }

    g_free (name);
    g_free (old_owner);
    g_free (new_owner);
}

static void
on_bus_connection_closed (GDBusConnection *connection,
                          gboolean         remote_peer_vanished,
                          GError          *error,
                          gpointer         user_data)
{
        CsmManager *manager;

        manager = CSM_MANAGER (user_data);

        g_debug ("CsmManager: dbus disconnected; disconnecting dbus clients...");

        manager->priv->dbus_disconnected = TRUE;
        remove_clients_for_connection (manager, NULL);
}

typedef struct
{
    const gchar  *signal_name;
    gpointer      callback;
} SkeletonSignal;

static SkeletonSignal skeleton_signals[] = {
    // signal name                              callback
    { "handle-setenv",                          csm_manager_setenv },
    { "handle-initialization-error",            csm_manager_initialization_error },
    { "handle-register-client",                 csm_manager_register_client },
    { "handle-unregister-client",               csm_manager_unregister_client },
    { "handle-inhibit",                         csm_manager_inhibit },
    { "handle-uninhibit",                       csm_manager_uninhibit },
    { "handle-is-inhibited",                    csm_manager_is_inhibited },
    { "handle-get-clients",                     csm_manager_get_clients },
    { "handle-get-inhibitors",                  csm_manager_get_inhibitors },
    { "handle-is-autostart-condition-handled",  csm_manager_is_autostart_condition_handled },
    { "handle-shutdown",                        csm_manager_shutdown },
    { "handle-reboot",                          csm_manager_reboot },
    { "handle-can-shutdown",                    csm_manager_can_shutdown },
    { "handle-logout",                          csm_manager_logout_dbus },
    { "handle-is-session-running",              csm_manager_is_session_running },
    { "handle-request-shutdown",                csm_manager_request_shutdown },
    { "handle-request-reboot",                  csm_manager_request_reboot }
};

static gboolean
register_manager (CsmManager *manager)
{
        CsmExportedManager *skeleton;
        GError *error = NULL;
        GDBusConnection *connection;
        gint i;

        error = NULL;
        manager->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        g_signal_connect (manager->priv->connection,
                          "closed",
                          G_CALLBACK (on_bus_connection_closed),
                           manager);

        manager->priv->dbus_disconnected = FALSE;

        manager->priv->bus_proxy = g_dbus_proxy_new_sync (manager->priv->connection,
                                                          G_DBUS_PROXY_FLAGS_NONE,
                                                          NULL,
                                                          "org.freedesktop.DBus",
                                                          "/org/freedesktop/DBus",
                                                          "org.freedesktop.DBus",
                                                          NULL,
                                                          &error);

        g_signal_connect (manager->priv->bus_proxy,
                          "g-signal",
                          G_CALLBACK (on_dbus_proxy_signal),
                          manager);

        skeleton = csm_exported_manager_skeleton_new ();
        manager->priv->skeleton = skeleton;

        g_debug ("exporting manager skeleton");

        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          manager->priv->connection,
                                          CSM_MANAGER_DBUS_PATH,
                                          &error);

        if (error != NULL) {
                g_critical ("error exporting manager skeleton: %s", error->message);
                g_error_free (error);

                exit(1);
        }

        for (i = 0; i < G_N_ELEMENTS (skeleton_signals); i++) {
                SkeletonSignal sig = skeleton_signals[i];

                g_signal_connect (skeleton,
                                  sig.signal_name,
                                  G_CALLBACK (sig.callback),
                                  manager);
        }

        return TRUE;
}


static void
csm_manager_set_failsafe (CsmManager *manager,
                          gboolean    enabled)
{
        g_return_if_fail (CSM_IS_MANAGER (manager));

        manager->priv->failsafe = enabled;
}

gboolean
csm_manager_get_failsafe (CsmManager *manager)
{
        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

	return manager->priv->failsafe;
}

static void
on_client_disconnected (CsmClient  *client,
                        CsmManager *manager)
{
        g_debug ("CsmManager: disconnect client");
        _disconnect_client (manager, client);
        csm_store_remove (manager->priv->clients, csm_client_peek_id (client));
        if (manager->priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION
            && csm_store_size (manager->priv->clients) == 0) {
                g_debug ("CsmManager: last client disconnected - exiting");
                end_phase (manager);
        }
}

static gboolean
on_xsmp_client_register_request (CsmXSMPClient *client,
                                 char         **id,
                                 CsmManager    *manager)
{
        gboolean handled;
        char    *new_id;
        CsmApp  *app;

        handled = TRUE;
        new_id = NULL;

        if (manager->priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                goto out;
        }

        if (IS_STRING_EMPTY (*id)) {
                new_id = csm_util_generate_startup_id ();
        } else {
                CsmClient *client;

                client = (CsmClient *)csm_store_find (manager->priv->clients,
                                                      (CsmStoreFunc)_client_has_startup_id,
                                                      *id);
                /* We can't have two clients with the same id. */
                if (client != NULL) {
                        goto out;
                }

                new_id = g_strdup (*id);
        }

        g_debug ("CsmManager: Adding new client %s to session", new_id);

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
                csm_client_set_app_id (CSM_CLIENT (client), csm_app_peek_app_id (app));
                csm_app_registered (app);
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

static void
_finished_playing_logout_sound (ca_context *c, uint32_t id, int error, void *userdata) 
{
    g_warning ("Finished playing logout sound");
    CsmManager *manager = (CsmManager *) userdata;
    manager->priv->logout_sound_is_playing = FALSE;    
    g_warning ("Resuming logout sequence...");
}

static void
maybe_play_logout_sound (CsmManager *manager)
{
    GSettings *settings = g_settings_new ("org.cinnamon.sounds");
        gboolean enabled = g_settings_get_boolean(settings, "logout-enabled");
        gchar *sound = g_settings_get_string (settings, "logout-file");
        if (enabled) {
            if (sound) {
                if (g_file_test (sound, G_FILE_TEST_EXISTS)) {
                    g_warning ("Playing logout sound '%s'", sound);
                    manager->priv->logout_sound_is_playing = TRUE;
                    ca_context_create (&manager->priv->ca);
                    ca_context_set_driver (manager->priv->ca, "pulse");
                    ca_context_change_props (manager->priv->ca, 0, CA_PROP_APPLICATION_ID, "org.gnome.VolumeControl", NULL);
                    ca_proplist *proplist = NULL;
                    ca_proplist_create(&proplist);
                    ca_proplist_sets(proplist, CA_PROP_MEDIA_FILENAME, sound);
                    int result = ca_context_play_full(manager->priv->ca, 0, proplist, _finished_playing_logout_sound, manager); 
                    if (result != CA_SUCCESS) {
                        g_warning ("Logout sound failed to play, skipping.");
                        manager->priv->logout_sound_is_playing = FALSE;
                    }
                }
            }
        }
        g_free(sound);
        g_object_unref (settings);    

        while (manager->priv->logout_sound_is_playing) {
            sleep(1);
        }
}

static void
maybe_save_session (CsmManager *manager)
{
        GError *error;

        if (csm_system_is_login_session (manager->priv->system))
                return;

        /* We only allow session saving when session is running or when
         * logging out */
        if (manager->priv->phase != CSM_MANAGER_PHASE_RUNNING &&
            manager->priv->phase != CSM_MANAGER_PHASE_END_SESSION) {
                return;
        }

        if (!csm_manager_get_autosave_enabled (manager)) {
                csm_session_save_clear ();
                return;
        }

        error = NULL;
        csm_session_save (manager->priv->clients, &error);

        if (error) {
                g_warning ("Error saving session: %s", error->message);
                g_error_free (error);
        }
}

static void
_handle_client_end_session_response (CsmManager *manager,
                                     CsmClient  *client,
                                     gboolean    is_ok,
                                     gboolean    do_last,
                                     gboolean    cancel,
                                     const char *reason)
{
        /* just ignore if received outside of shutdown */
        if (manager->priv->phase < CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                return;
        }

        g_debug ("CsmManager: Response from end session request: is-ok=%d do-last=%d cancel=%d reason=%s", is_ok, do_last, cancel, reason ? reason :"");

        if (cancel) {
                cancel_end_session (manager);
                return;
        }

        manager->priv->query_clients = g_slist_remove (manager->priv->query_clients, client);

        if (! is_ok && manager->priv->logout_mode != CSM_MANAGER_LOGOUT_MODE_FORCE) {
                guint         cookie;
                CsmInhibitor *inhibitor;
                char         *app_id;
                const char   *bus_name;

                /* FIXME: do we support updating the reason? */

                /* Create JIT inhibit */
                if (CSM_IS_DBUS_CLIENT (client)) {
                        bus_name = csm_dbus_client_get_bus_name (CSM_DBUS_CLIENT (client));
                } else {
                        bus_name = NULL;
                }

                app_id = g_strdup (csm_client_peek_app_id (client));
                if (IS_STRING_EMPTY (app_id)) {
                        /* XSMP clients don't give us an app id unless we start them */
                        g_free (app_id);
                        app_id = csm_client_get_app_name (client);
                }

                cookie = _generate_unique_cookie (manager);
                inhibitor = csm_inhibitor_new_for_client (csm_client_peek_id (client),
                                                          app_id,
                                                          CSM_INHIBITOR_FLAG_LOGOUT,
                                                          reason != NULL ? reason : _("Not responding"),
                                                          bus_name,
                                                          cookie);
                g_free (app_id);
                csm_store_add (manager->priv->inhibitors, csm_inhibitor_peek_id (inhibitor), G_OBJECT (inhibitor));
                g_object_unref (inhibitor);
        } else {
                csm_store_foreach_remove (manager->priv->inhibitors,
                                          (CsmStoreFunc)inhibitor_has_client_id,
                                          (gpointer)csm_client_peek_id (client));
        }

        if (manager->priv->phase == CSM_MANAGER_PHASE_QUERY_END_SESSION) { 
                if (manager->priv->query_clients == NULL) {
                        query_end_session_complete (manager);
                }
        } else if (manager->priv->phase == CSM_MANAGER_PHASE_END_SESSION) {
                if (do_last) {
                        /* This only makes sense if we're in part 1 of
                         * CSM_MANAGER_PHASE_END_SESSION. Doing this in part 2
                         * can only happen because of a buggy client that loops
                         * wanting to be last again and again. The phase
                         * timeout will take care of this issue. */
                        manager->priv->next_query_clients = g_slist_prepend (manager->priv->next_query_clients,
                                                                             client);
                }

                /* we can continue to the next step if all clients have replied
                 * and if there's no inhibitor */
                if (manager->priv->query_clients != NULL
                    || csm_manager_is_logout_inhibited (manager)) {
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
on_client_end_session_response (CsmClient  *client,
                                gboolean    is_ok,
                                gboolean    do_last,
                                gboolean    cancel,
                                const char *reason,
                                CsmManager *manager)
{
        _handle_client_end_session_response (manager,
                                             client,
                                             is_ok,
                                             do_last,
                                             cancel,
                                             reason);
}

gboolean
csm_manager_logout (CsmManager *manager,
                    guint       logout_mode,
                    GError    **error)
{
        g_debug ("CsmManager: Logout called");

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

        if (manager->priv->phase != CSM_MANAGER_PHASE_RUNNING) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_NOT_IN_RUNNING,
                             "Logout interface is only available during the Running phase");
                return FALSE;
        }

        if (_log_out_is_locked_down (manager)) {
                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_LOCKED_DOWN,
                             "Logout has been locked down");
                return FALSE;
        }

        switch (logout_mode) {
        case CSM_MANAGER_LOGOUT_MODE_NORMAL:
        case CSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION:
        case CSM_MANAGER_LOGOUT_MODE_FORCE:
                user_logout (manager, logout_mode);
                break;

        default:
                g_debug ("Unknown logout mode option");

                g_set_error (error,
                             CSM_MANAGER_ERROR,
                             CSM_MANAGER_ERROR_INVALID_OPTION,
                             "Unknown logout mode flag");
                return FALSE;
        }

        return TRUE;
}


static void
on_xsmp_client_logout_request (CsmXSMPClient *client,
                               gboolean       show_dialog,
                               CsmManager    *manager)
{
        GError *error;
        int     logout_mode;

        if (show_dialog) {
                logout_mode = CSM_MANAGER_LOGOUT_MODE_NORMAL;
        } else {
                logout_mode = CSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION;
        }

        error = NULL;

        csm_manager_logout (manager, logout_mode, &error);
        if (error != NULL) {
                g_warning ("Unable to logout: %s", error->message);
                g_error_free (error);
        }
}

static void
on_store_client_added (CsmStore   *store,
                       const char *id,
                       CsmManager *manager)
{
        CsmClient *client;

        g_debug ("CsmManager: Client added: %s", id);

        client = (CsmClient *)csm_store_lookup (store, id);

        /* a bit hacky */
        if (CSM_IS_XSMP_CLIENT (client)) {
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

        g_signal_connect (client,
                          "disconnected",
                          G_CALLBACK (on_client_disconnected),
                          manager);

        csm_exported_manager_emit_client_added (manager->priv->skeleton, id);
}

static void
on_store_client_removed (CsmStore   *store,
                         const char *id,
                         CsmManager *manager)
{
        g_debug ("CsmManager: Client removed: %s", id);

        csm_exported_manager_emit_client_removed (manager->priv->skeleton, id);
}

static void
csm_manager_set_client_store (CsmManager *manager,
                              CsmStore   *store)
{
        g_return_if_fail (CSM_IS_MANAGER (manager));

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


        g_debug ("CsmManager: setting client store %p", store);

        manager->priv->clients = store;

        if (manager->priv->clients != NULL) {
            if (manager->priv->xsmp_server)
                    g_object_unref (manager->priv->xsmp_server);

                manager->priv->xsmp_server = csm_xsmp_server_new (store);

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
csm_manager_set_property (GObject       *object,
                          guint          prop_id,
                          const GValue  *value,
                          GParamSpec    *pspec)
{
        CsmManager *self;

        self = CSM_MANAGER (object);

        switch (prop_id) {
        case PROP_FAILSAFE:
                csm_manager_set_failsafe (self, g_value_get_boolean (value));
                break;
         case PROP_FALLBACK:
                self->priv->is_fallback_session = g_value_get_boolean (value);
                break;
         case PROP_CLIENT_STORE:
                csm_manager_set_client_store (self, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_manager_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
        CsmManager *self;

        self = CSM_MANAGER (object);

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
                    CsmApp     *app,
                    const char *service)
{
        return csm_app_provides (app, service);
}

static GObject *
csm_manager_constructor (GType                  type,
                         guint                  n_construct_properties,
                         GObjectConstructParam *construct_properties)
{
        CsmManager *manager;

        manager = CSM_MANAGER (G_OBJECT_CLASS (csm_manager_parent_class)->constructor (type,
                                                                                       n_construct_properties,
                                                                                       construct_properties));
        return G_OBJECT (manager);
}

static void
update_inhibited_actions (CsmManager *manager,
                          CsmInhibitorFlag new_inhibited_actions)
{
        if (manager->priv->inhibited_actions == new_inhibited_actions)
                return;

        manager->priv->inhibited_actions = new_inhibited_actions;

        g_debug ("CsmManager: new inhibit flag: %d", new_inhibited_actions);

        csm_exported_manager_set_inhibited_actions (manager->priv->skeleton,
                                                    manager->priv->inhibited_actions);

        g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (manager->priv->skeleton));
}

static void
on_store_inhibitor_added (CsmStore   *store,
                          const char *id,
                          CsmManager *manager)
{
        CsmInhibitor *i;
        CsmInhibitorFlag new_inhibited_actions;

        g_debug ("CsmManager: Inhibitor added: %s", id);

        i = CSM_INHIBITOR (csm_store_lookup (store, id));
        csm_system_add_inhibitor (manager->priv->system, id,
                                  csm_inhibitor_peek_flags (i));

        new_inhibited_actions = manager->priv->inhibited_actions | csm_inhibitor_peek_flags (i);
        update_inhibited_actions (manager, new_inhibited_actions);

        csm_exported_manager_emit_inhibitor_added (manager->priv->skeleton, id);

        update_idle (manager);
}

static gboolean
collect_inhibition_flags (const char *id,
                          GObject    *object,
                          gpointer    user_data)
{
        CsmInhibitorFlag *new_inhibited_actions = user_data;

        *new_inhibited_actions |= csm_inhibitor_peek_flags (CSM_INHIBITOR (object));

        return FALSE;
}

static void
on_store_inhibitor_removed (CsmStore   *store,
                            const char *id,
                            CsmManager *manager)
{
        CsmInhibitorFlag new_inhibited_actions;

        g_debug ("CsmManager: Inhibitor removed: %s", id);

        csm_system_remove_inhibitor (manager->priv->system, id);

        new_inhibited_actions = 0;
        csm_store_foreach (manager->priv->inhibitors,
                           collect_inhibition_flags,
                           &new_inhibited_actions);
        update_inhibited_actions (manager, new_inhibited_actions);

        csm_exported_manager_emit_inhibitor_removed (manager->priv->skeleton, id);

        update_idle (manager);
}

static void
csm_manager_dispose (GObject *object)
{
        CsmManager *manager = CSM_MANAGER (object);

        g_debug ("CsmManager: disposing manager");

        g_clear_object (&manager->priv->xsmp_server);

        g_clear_object (&manager->priv->bus_proxy);

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

        if (manager->priv->power_settings) {
                g_object_unref (manager->priv->power_settings);
                manager->priv->power_settings = NULL;
        }

        if (manager->priv->lockdown_settings) {
                g_object_unref (manager->priv->lockdown_settings);
                manager->priv->lockdown_settings = NULL;
        }

        if (manager->priv->system != NULL) {
                g_object_unref (manager->priv->system);
                manager->priv->system = NULL;
        }

        G_OBJECT_CLASS (csm_manager_parent_class)->dispose (object);
}

static void
csm_manager_class_init (CsmManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = csm_manager_get_property;
        object_class->set_property = csm_manager_set_property;
        object_class->constructor = csm_manager_constructor;
        object_class->finalize = csm_manager_finalize;
        object_class->dispose = csm_manager_dispose;

        signals [PHASE_CHANGED] =
                g_signal_new ("phase-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmManagerClass, phase_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_FAILSAFE,
                                         g_param_spec_boolean ("failsafe",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        /**
         * CsmManager::session-name
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
         * CsmManager::fallback
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
                                                              CSM_TYPE_STORE,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (CsmManagerPrivate));
}

static void
on_presence_status_changed (CsmPresence  *presence,
                            guint         status,
                            CsmManager   *manager)
{
        CsmSystem *system;

        system = csm_get_system ();
        csm_system_set_session_idle (system,
                                     (status == CSM_PRESENCE_STATUS_IDLE));
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
csm_manager_init (CsmManager *manager)
{

        manager->priv = CSM_MANAGER_GET_PRIVATE (manager);

        manager->priv->settings = g_settings_new (CSM_MANAGER_SCHEMA);
        manager->priv->session_settings = g_settings_new (SESSION_SCHEMA);
        manager->priv->power_settings = g_settings_new (POWER_SETTINGS_SCHEMA);
        manager->priv->lockdown_settings = g_settings_new (LOCKDOWN_SCHEMA);

        manager->priv->inhibitors = csm_store_new ();
        g_signal_connect (manager->priv->inhibitors,
                          "added",
                          G_CALLBACK (on_store_inhibitor_added),
                          manager);
        g_signal_connect (manager->priv->inhibitors,
                          "removed",
                          G_CALLBACK (on_store_inhibitor_removed),
                          manager);

        manager->priv->apps = csm_store_new ();

        manager->priv->presence = csm_presence_new ();
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

        manager->priv->system = csm_get_system ();
}

static void
csm_manager_finalize (GObject *object)
{
        CsmManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CSM_IS_MANAGER (object));

        manager = CSM_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        if (manager->priv->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (manager->priv->skeleton),
                                                                    manager->priv->connection);
                g_clear_object (&manager->priv->skeleton);
        }

        g_clear_object (&manager->priv->connection);

        G_OBJECT_CLASS (csm_manager_parent_class)->finalize (object);
}

CsmManager *
csm_manager_get (void)
{
        return manager_object;
}

CsmManager *
csm_manager_new (CsmStore *client_store,
                 gboolean  failsafe)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (CSM_TYPE_MANAGER,
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

        return CSM_MANAGER (manager_object);
}

static gboolean
csm_manager_is_switch_user_inhibited (CsmManager *manager)
{
        CsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (CsmInhibitor *)csm_store_find (manager->priv->inhibitors,
                                                    (CsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (CSM_INHIBITOR_FLAG_SWITCH_USER));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static gboolean
csm_manager_is_suspend_inhibited (CsmManager *manager)
{
        CsmInhibitor *inhibitor;

        if (manager->priv->inhibitors == NULL) {
                return FALSE;
        }

        inhibitor = (CsmInhibitor *)csm_store_find (manager->priv->inhibitors,
                                                    (CsmStoreFunc)inhibitor_has_flag,
                                                    GUINT_TO_POINTER (CSM_INHIBITOR_FLAG_SUSPEND));
        if (inhibitor == NULL) {
                return FALSE;
        }
        return TRUE;
}

static void
request_reboot (CsmManager *manager)
{
        g_debug ("CsmManager: requesting reboot");

        /* FIXME: We need to support a more structured shutdown here,
         * but that's blocking on an improved ConsoleKit api.
         *
         * See https://bugzilla.gnome.org/show_bug.cgi?id=585614
         */
        manager->priv->logout_type = CSM_MANAGER_LOGOUT_REBOOT_INTERACT;
        end_phase (manager);
}

static void
request_shutdown (CsmManager *manager)
{
        g_debug ("CsmManager: requesting shutdown");

        /* See the comment in request_reboot() for some more details about
         * what work needs to be done here. */
        manager->priv->logout_type = CSM_MANAGER_LOGOUT_SHUTDOWN_INTERACT;
        end_phase (manager);
}

static void
request_suspend (CsmManager *manager)
{
        g_debug ("CsmManager: requesting suspend");

        if (! csm_manager_is_suspend_inhibited (manager)) {
                manager_attempt_suspend (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("CsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = csm_inhibit_dialog_new (manager->priv->inhibitors,
                                                                manager->priv->clients,
                                                                CSM_LOGOUT_ACTION_SLEEP);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
request_hibernate (CsmManager *manager)
{
        g_debug ("CsmManager: requesting hibernate");

        /* hibernate uses suspend inhibit */
        if (! csm_manager_is_suspend_inhibited (manager)) {
                manager_attempt_hibernate (manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("CsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = csm_inhibit_dialog_new (manager->priv->inhibitors,
                                                                manager->priv->clients,
                                                                CSM_LOGOUT_ACTION_HIBERNATE);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}


static void
request_logout (CsmManager           *manager,
                CsmManagerLogoutMode  mode)
{
        g_debug ("CsmManager: requesting logout");

        manager->priv->logout_mode = mode;
        manager->priv->logout_type = CSM_MANAGER_LOGOUT_LOGOUT;

        end_phase (manager);
}

static void
request_switch_user (GdkDisplay *display,
                     CsmManager *manager)
{
        g_debug ("CsmManager: requesting user switch");

	/* See comment in manager_switch_user() to understand why we do this in
	 * both functions. */
	if (_switch_user_is_locked_down (manager)) {
		g_warning ("Unable to switch user: User switching has been locked down");
		return;
	}

        if (! csm_manager_is_switch_user_inhibited (manager)) {
                manager_switch_user (display, manager);
                return;
        }

        if (manager->priv->inhibit_dialog != NULL) {
                g_debug ("CsmManager: inhibit dialog already up");
                gtk_window_present (GTK_WINDOW (manager->priv->inhibit_dialog));
                return;
        }

        manager->priv->inhibit_dialog = csm_inhibit_dialog_new (manager->priv->inhibitors,
                                                                manager->priv->clients,
                                                                CSM_LOGOUT_ACTION_SWITCH_USER);

        g_signal_connect (manager->priv->inhibit_dialog,
                          "response",
                          G_CALLBACK (inhibit_dialog_response),
                          manager);
        gtk_widget_show (manager->priv->inhibit_dialog);
}

static void
logout_dialog_response (CsmLogoutDialog *logout_dialog,
                        guint            response_id,
                        CsmManager      *manager)
{
        GdkDisplay *display;

        /* We should only be here if mode has already have been set from
         * show_fallback_shutdown/logout_dialog
         */
        g_assert (manager->priv->logout_mode == CSM_MANAGER_LOGOUT_MODE_NORMAL);

        g_debug ("CsmManager: Logout dialog response: %d", response_id);

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
        case CSM_LOGOUT_RESPONSE_SWITCH_USER:
                request_switch_user (display, manager);
                break;
        case CSM_LOGOUT_RESPONSE_HIBERNATE:
                request_hibernate (manager);
                break;
        case CSM_LOGOUT_RESPONSE_SLEEP:
                request_suspend (manager);
                break;
        case CSM_LOGOUT_RESPONSE_SHUTDOWN:
                request_shutdown (manager);
                break;
        case CSM_LOGOUT_RESPONSE_REBOOT:
                request_reboot (manager);
                break;
        case CSM_LOGOUT_RESPONSE_LOGOUT:
                /* We've already gotten confirmation from the user so
                 * initiate the logout in NO_CONFIRMATION mode.
                 */
                request_logout (manager, CSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static void
show_fallback_shutdown_dialog (CsmManager *manager,
                               gboolean    is_reboot)
{
        GtkWidget *dialog;

        if (manager->priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        manager->priv->logout_mode = CSM_MANAGER_LOGOUT_MODE_NORMAL;

        dialog = csm_get_shutdown_dialog (gdk_screen_get_default (),
                                          gtk_get_current_event_time (),
                                          is_reboot ?
                                          CSM_DIALOG_LOGOUT_TYPE_REBOOT :
                                          CSM_DIALOG_LOGOUT_TYPE_SHUTDOWN);

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (logout_dialog_response),
                          manager);
        gtk_window_present (GTK_WINDOW (dialog));
}

static void
show_fallback_logout_dialog (CsmManager *manager)
{
        GtkWidget *dialog;

        if (manager->priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        manager->priv->logout_mode = CSM_MANAGER_LOGOUT_MODE_NORMAL;

        dialog = csm_get_logout_dialog (gdk_screen_get_default (),
                                        gtk_get_current_event_time ());

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (logout_dialog_response),
                          manager);
        gtk_window_present (GTK_WINDOW (dialog));
}

static void
user_logout (CsmManager           *manager,
             CsmManagerLogoutMode  mode)
{
        gboolean logout_prompt;

        if (manager->priv->phase >= CSM_MANAGER_PHASE_QUERY_END_SESSION) {
                /* Already shutting down, nothing more to do */
                return;
        }

        logout_prompt = g_settings_get_boolean (manager->priv->settings,
                                                KEY_LOGOUT_PROMPT);

        /* If the shell isn't running, and this isn't a non-interative logout request,
         * and the user has their settings configured to show a confirmation dialog for
         * logout, then go ahead and show the fallback confirmation dialog now.
         *
         * If the shell is running, then the confirmation dialog and inhibitor dialog are
         * combined, so we'll show it at a later stage in the logout process.
         */
        if (mode == CSM_MANAGER_LOGOUT_MODE_NORMAL && logout_prompt) {
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
csm_manager_set_phase (CsmManager      *manager,
                       CsmManagerPhase  phase)
{
        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);
        manager->priv->phase = phase;
        return (TRUE);
}

static gboolean
_log_out_is_locked_down (CsmManager *manager)
{
        return g_settings_get_boolean (manager->priv->lockdown_settings,
                                       KEY_DISABLE_LOG_OUT);
}

static gboolean
_switch_user_is_locked_down (CsmManager *manager)
{
        return g_settings_get_boolean (manager->priv->lockdown_settings,
                                       KEY_DISABLE_USER_SWITCHING);
}


static void
append_app (CsmManager *manager,
            CsmApp     *app,
            const char *provides,
            gboolean    is_required)
{
        const char *id;
        const char *app_id;
        CsmApp     *dup;

        id = csm_app_peek_id (app);
        if (IS_STRING_EMPTY (id)) {
                g_debug ("CsmManager: not adding app: no id");
                return;
        }

        dup = (CsmApp *)csm_store_lookup (manager->priv->apps, id);
        if (dup != NULL) {
                g_debug ("CsmManager: not adding app: already added");
                return;
        }

        app_id = csm_app_peek_app_id (app);
        if (IS_STRING_EMPTY (app_id)) {
                g_debug ("CsmManager: not adding app: no app-id");
                return;
        }

        dup = find_app_for_app_id (manager, app_id);
        if (dup != NULL) {
                g_debug ("CsmManager: not adding app: app-id '%s' already exists", app_id);

                if (provides && CSM_IS_AUTOSTART_APP (dup))
                        csm_autostart_app_add_provides (CSM_AUTOSTART_APP (dup), provides);

                if (is_required &&
                    !g_slist_find (manager->priv->required_apps, dup)) {
                        g_debug ("CsmManager: making app '%s' required", csm_app_peek_app_id (dup));
                        manager->priv->required_apps = g_slist_prepend (manager->priv->required_apps, dup);
                }

                return;
        }

        csm_store_add (manager->priv->apps, id, G_OBJECT (app));
        if (is_required) {
                g_debug ("CsmManager: adding required app %s", csm_app_peek_app_id (app));
                manager->priv->required_apps = g_slist_prepend (manager->priv->required_apps, app);
        }
}

static gboolean
add_autostart_app_internal (CsmManager *manager,
                            const char *path,
                            const char *provides,
                            gboolean    is_required)
{
        CsmApp  *app;
        char   **internal_provides;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (path != NULL, FALSE);

        /* Note: if we cannot add the app because its service is already
         * provided, because its app-id is taken, or because of any other
         * reason meaning there is already an app playing its role, then we
         * should make sure that relevant properties (like
         * provides/is_required) are set in the pre-existing app if needed. */

        /* first check to see if service is already provided */
        if (provides != NULL) {
                CsmApp *dup;

                dup = (CsmApp *)csm_store_find (manager->priv->apps,
                                                (CsmStoreFunc)_find_app_provides,
                                                (char *)provides);
                if (dup != NULL) {
                        g_debug ("CsmManager: service '%s' is already provided", provides);

                        if (is_required &&
                            !g_slist_find (manager->priv->required_apps, dup)) {
                                g_debug ("CsmManager: making app '%s' required", csm_app_peek_app_id (dup));
                                manager->priv->required_apps = g_slist_prepend (manager->priv->required_apps, dup);
                        }

                        return FALSE;
                }
        }

        app = csm_autostart_app_new (path);

        if (app == NULL) {
                return FALSE;
        }

        internal_provides = csm_app_get_provides (app);
        if (internal_provides) {
                int i;
                gboolean provided = FALSE;

                for (i = 0; internal_provides[i] != NULL; i++) {
                        CsmApp *dup;

                        dup = (CsmApp *)csm_store_find (manager->priv->apps,
                                                        (CsmStoreFunc)_find_app_provides,
                                                        (char *)internal_provides[i]);
                        if (dup != NULL) {
                                g_debug ("CsmManager: service '%s' is already provided", internal_provides[i]);

                                if (is_required &&
                                    !g_slist_find (manager->priv->required_apps, dup)) {
                                        g_debug ("CsmManager: making app '%s' required", csm_app_peek_app_id (dup));
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
                csm_autostart_app_add_provides (CSM_AUTOSTART_APP (app), provides);

        g_debug ("CsmManager: read %s", path);
        append_app (manager, app, provides, is_required);
        g_object_unref (app);

        return TRUE;
}

gboolean
csm_manager_add_autostart_app (CsmManager *manager,
                               const char *path,
                               const char *provides)
{
        return add_autostart_app_internal (manager,
                                           path,
                                           provides,
                                           FALSE);
}

/**
 * csm_manager_add_required_app:
 * @manager: a #CsmManager
 * @path: Path to desktop file
 * @provides: What the component provides, as a space separated list
 *
 * Similar to csm_manager_add_autostart_app(), except marks the
 * component as being required; we then try harder to ensure
 * it's running and inform the user if we can't.
 *
 */
gboolean
csm_manager_add_required_app (CsmManager *manager,
                              const char *path,
                              const char *provides)
{
        return add_autostart_app_internal (manager,
                                           path,
                                           provides,
                                           TRUE);
}


gboolean
csm_manager_add_autostart_apps_from_dir (CsmManager *manager,
                                         const char *path)
{
        GDir       *dir;
        const char *name;

        g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (path != NULL, FALSE);

        g_debug ("CsmManager: *** Adding autostart apps for %s", path);

        dir = g_dir_open (path, 0, NULL);
        if (dir == NULL) {
                return FALSE;
        }

        while ((name = g_dir_read_name (dir))) {
                char *desktop_file;

                if (!g_str_has_suffix (name, ".desktop") ||
                    csm_manager_get_app_is_blacklisted (manager, name)) {
                        continue;
                }

                desktop_file = g_build_filename (path, name, NULL);
                csm_manager_add_autostart_app (manager, desktop_file, NULL);
                g_free (desktop_file);
        }

        g_dir_close (dir);

        return TRUE;
}


gboolean
csm_manager_get_app_is_blacklisted (CsmManager *manager,
                                    const char *name)
{
    g_return_val_if_fail (CSM_IS_MANAGER (manager), FALSE);

    gchar **gs_blacklist = g_settings_get_strv(manager->priv->settings, KEY_BLACKLIST);
    GList *list = NULL;
    gboolean ret = FALSE;

    int i;
    for (i = 0; i < g_strv_length (gs_blacklist); i++)
        list = g_list_append (list, g_strdup (gs_blacklist[i]));

    GList *l;

    for (l = list; l != NULL; l = l->next) {
        gchar *ptr = g_strstr_len (name, -1, l->data);
        if (ptr != NULL) {
            ret = TRUE;
            break;
        }
    }

    g_list_free_full (list, g_free);
    g_strfreev (gs_blacklist);
    return ret;
}

gboolean
csm_manager_get_autosave_enabled (CsmManager *manager)
{
    return g_settings_get_boolean (manager->priv->settings,
                                   KEY_AUTOSAVE);
}
