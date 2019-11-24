/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc.
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
 *
 * Author: Matthias Clasen
 */

#include "config.h"
#include "csm-systemd.h"

#ifdef HAVE_LOGIND

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#ifdef HAVE_ELOGIND
#include <elogind/sd-login.h>
#else
#include <systemd/sd-login.h>
#endif

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "csm-marshal.h"
#include "csm-system.h"

#define SD_NAME              "org.freedesktop.login1"
#define SD_PATH              "/org/freedesktop/login1"
#define SD_INTERFACE         "org.freedesktop.login1.Manager"
#define SD_SEAT_INTERFACE    "org.freedesktop.login1.Seat"
#define SD_SESSION_INTERFACE "org.freedesktop.login1.Session"

struct _CsmSystemdPrivate
{
        GDBusProxy      *sd_proxy;
        char            *session_id;
        gchar           *session_path;

        GSList          *inhibitors;
        gint             inhibit_fd;
};

static void csm_systemd_system_init (CsmSystemInterface *iface);

G_DEFINE_TYPE_WITH_CODE (CsmSystemd, csm_systemd, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CSM_TYPE_SYSTEM,
                                                csm_systemd_system_init))

static void
drop_system_inhibitor (CsmSystemd *manager)
{
        if (manager->priv->inhibit_fd != -1) {
                g_debug ("Dropping system inhibitor");
                close (manager->priv->inhibit_fd);
                manager->priv->inhibit_fd = -1;
        }
}

static void
csm_systemd_finalize (GObject *object)
{
        CsmSystemd *systemd = CSM_SYSTEMD (object);

        g_clear_object (&systemd->priv->sd_proxy);
        free (systemd->priv->session_id);
        g_free (systemd->priv->session_path);

        if (systemd->priv->inhibitors != NULL) {
                g_slist_free_full (systemd->priv->inhibitors, g_free);
        }
        drop_system_inhibitor (systemd);

        G_OBJECT_CLASS (csm_systemd_parent_class)->finalize (object);
}

static void
csm_systemd_class_init (CsmSystemdClass *manager_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (manager_class);

        object_class->finalize = csm_systemd_finalize;

        g_type_class_add_private (manager_class, sizeof (CsmSystemdPrivate));
}

static void
csm_systemd_init (CsmSystemd *manager)
{
        GError *error;
        GDBusConnection *bus;
        GVariant *res;

        manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
                                                     CSM_TYPE_SYSTEMD,
                                                     CsmSystemdPrivate);

        manager->priv->inhibit_fd = -1;

        error = NULL;

        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (bus == NULL) {
                g_warning ("Failed to connect to system bus: %s",
                           error->message);
                g_error_free (error);
        } else {
                manager->priv->sd_proxy =
                        g_dbus_proxy_new_sync (bus,
                                               0,
                                               NULL,
                                               SD_NAME,
                                               SD_PATH,
                                               SD_INTERFACE,
                                               NULL,
                                               &error);

                if (manager->priv->sd_proxy == NULL) {
                        g_warning ("Failed to connect to systemd: %s",
                                   error->message);
                        g_error_free (error);
                }

                g_object_unref (bus);
        }

        sd_pid_get_session (getpid (), &manager->priv->session_id);

        if (manager->priv->session_id == NULL) {
                g_warning ("Could not get session id for session. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return;
        }

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "GetSession",
                                      g_variant_new ("(s)", manager->priv->session_id),
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (res == NULL) {
                g_warning ("Could not get session id for session. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return;
        }

        g_variant_get (res, "(o)", &manager->priv->session_path);
        g_variant_unref (res);
}

static void
restart_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        CsmSystemd *manager = user_data;
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to restart system via systemd: %s", error->message);
                g_signal_emit_by_name (G_OBJECT (manager), "request-failed", NULL);
                g_error_free (error);
        } else {
                g_variant_unref (res);
        }
}

static void
csm_systemd_attempt_restart (CsmSystem *system)
{
        g_warning ("Attempting to restart using systemd...");

        CsmSystemd *manager = CSM_SYSTEMD (system);

        g_dbus_proxy_call (manager->priv->sd_proxy,
                           "Reboot",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           restart_done,
                           manager);
}

static void
stop_done (GObject      *source,
           GAsyncResult *result,
           gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        CsmSystemd *manager = user_data;
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to stop system via systemd: %s", error->message);
                g_signal_emit_by_name (G_OBJECT (manager), "request-failed", NULL);                
                g_error_free (error);
        } else {                
                g_variant_unref (res);
        }
}

static void
csm_systemd_attempt_stop (CsmSystem *system)
{
        g_warning ("Attempting to shutdown using systemd...");

        CsmSystemd *manager = CSM_SYSTEMD (system);

        g_dbus_proxy_call (manager->priv->sd_proxy,
                           "PowerOff",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           stop_done,
                           manager);
}

static void
csm_systemd_set_session_idle (CsmSystem *system,
                              gboolean   is_idle)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);
        GDBusConnection *bus;

        g_debug ("Updating systemd idle status: %d", is_idle);
        bus = g_dbus_proxy_get_connection (manager->priv->sd_proxy);
        g_dbus_connection_call (bus,
                                SD_NAME,
                                manager->priv->session_path,
                                SD_SESSION_INTERFACE,
                                "SetIdleHint",
                                g_variant_new ("(b)", is_idle),
                                G_VARIANT_TYPE_BOOLEAN,
                                0,
                                G_MAXINT,
                                NULL, NULL, NULL);
}

static gboolean
csm_systemd_can_switch_user (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);
        gchar *seat;
        gint ret;

        sd_session_get_seat (manager->priv->session_id, &seat);
        ret = sd_seat_can_multi_session (seat);
        free (seat);

        return ret > 0;
}

static gboolean
csm_systemd_can_restart (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);
        gchar *rv;
        GVariant *res;
        gboolean can_restart;

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "CanReboot",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanReboot failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_restart = g_strcmp0 (rv, "yes") == 0 ||
                      g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_restart;
}

static gboolean
csm_systemd_can_stop (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);
        gchar *rv;
        GVariant *res;
        gboolean can_stop;

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "CanPowerOff",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanPowerOff failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_stop = g_strcmp0 (rv, "yes") == 0 ||
                   g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_stop;
}

static gboolean
csm_systemd_is_login_session (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);
        int res;
        gboolean ret;
        gchar *session_class = NULL;

        ret = FALSE;

        if (manager->priv->session_id == NULL) {
                return ret;
        }

        res = sd_session_get_class (manager->priv->session_id, &session_class);
        if (res < 0) {
                g_warning ("could not get session class: %s", strerror (-res));
                return FALSE;
        }
        ret = (g_strcmp0 (session_class, "greeter") == 0);
        free (session_class);

        return ret;
}

static gboolean
csm_systemd_can_hybrid_sleep (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);
        gchar *rv;
        GVariant *res;
        gboolean can_hybrid_sleep;

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "CanHybridSleep",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanHybridSleep failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_hybrid_sleep = g_strcmp0 (rv, "yes") == 0 ||
                      g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_hybrid_sleep;
}

static gboolean
csm_systemd_can_suspend (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);
        gchar *rv;
        GVariant *res;
        gboolean can_suspend;

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "CanSuspend",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanSuspend failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_suspend = g_strcmp0 (rv, "yes") == 0 ||
                      g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_suspend;
}

static gboolean
csm_systemd_can_hibernate (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);
        gchar *rv;
        GVariant *res;
        gboolean can_hibernate;

        res = g_dbus_proxy_call_sync (manager->priv->sd_proxy,
                                      "CanHibernate",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanHibernate failed. Check that logind is "
                           "properly installed and pam_systemd is getting used at login.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_hibernate = g_strcmp0 (rv, "yes") == 0 ||
                        g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_hibernate;
}

static void
hybrid_sleep_done (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to send system to hybrid sleep: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_unref (res);
        }
}

static void
suspend_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to suspend system: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_unref (res);
        }
}

static void
hibernate_done (GObject      *source,
                GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to hibernate system: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_unref (res);
        }
}

static void
csm_systemd_hybrid_sleep (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);

        g_dbus_proxy_call (manager->priv->sd_proxy,
                           "HybridSleep",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           hybrid_sleep_done,
                           manager);
}

static void
csm_systemd_suspend (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);

        g_dbus_proxy_call (manager->priv->sd_proxy,
                           "Suspend",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           hibernate_done,
                           manager);
}

static void
csm_systemd_hibernate (CsmSystem *system)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);

        g_dbus_proxy_call (manager->priv->sd_proxy,
                           "Hibernate",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           suspend_done,
                           manager);
}

static void
inhibit_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        CsmSystemd *manager = CSM_SYSTEMD (user_data);
        GError *error = NULL;
        GVariant *res;
        GUnixFDList *fd_list = NULL;
        gint idx;

        res = g_dbus_proxy_call_with_unix_fd_list_finish (proxy, &fd_list, result, &error);

        if (!res) {
                g_warning ("Unable to inhibit system: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_get (res, "(h)", &idx);
                manager->priv->inhibit_fd = g_unix_fd_list_get (fd_list, idx, &error);
                if (manager->priv->inhibit_fd == -1) {
                        g_warning ("Failed to receive system inhibitor fd: %s", error->message);
                        g_error_free (error);
                }
                g_debug ("System inhibitor fd is %d", manager->priv->inhibit_fd);
                g_object_unref (fd_list);
                g_variant_unref (res);
        }

        if (manager->priv->inhibitors == NULL) {
                drop_system_inhibitor (manager);
        }
}

static void
csm_systemd_add_inhibitor (CsmSystem        *system,
                           const gchar      *id,
                           CsmInhibitorFlag  flag)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);

        if ((flag & CSM_INHIBITOR_FLAG_SUSPEND) == 0)
                return;

        if (manager->priv->inhibitors == NULL) {
                g_debug ("Adding system inhibitor");
                g_dbus_proxy_call_with_unix_fd_list (manager->priv->sd_proxy,
                                                     "Inhibit",
                                                     g_variant_new ("(ssss)",
                                                                    "sleep:shutdown",
                                                                    g_get_user_name (),
                                                                    "user session inhibited",
                                                                    "block"),
                                                     0,
                                                     G_MAXINT,
                                                     NULL,
                                                     NULL,
                                                     inhibit_done,
                                                     manager);
        }
        manager->priv->inhibitors = g_slist_prepend (manager->priv->inhibitors, g_strdup (id));
}

static void
csm_systemd_remove_inhibitor (CsmSystem   *system,
                              const gchar *id)
{
        CsmSystemd *manager = CSM_SYSTEMD (system);
        GSList *l;

        l = g_slist_find_custom (manager->priv->inhibitors, id, (GCompareFunc)g_strcmp0);
        if (l == NULL)
                return;

        g_free (l->data);
        manager->priv->inhibitors = g_slist_delete_link (manager->priv->inhibitors, l);
        if (manager->priv->inhibitors == NULL) {
                drop_system_inhibitor (manager);
        }
}

static gboolean
csm_systemd_is_last_session_for_user (CsmSystem *system)
{
        char **sessions = NULL;
        char *session = NULL;
        gboolean is_last_session;
        int ret, i;

        ret = sd_pid_get_session (getpid (), &session);

        if (ret != 0) {
                return FALSE;
        }

        ret = sd_uid_get_sessions (getuid (), FALSE, &sessions);

        if (ret <= 0) {
                return FALSE;
        }

        is_last_session = TRUE;
        for (i = 0; sessions[i]; i++) {
                char *state = NULL;
                char *type = NULL;

                if (g_strcmp0 (sessions[i], session) == 0)
                        continue;

                ret = sd_session_get_state (sessions[i], &state);

                if (ret != 0)
                        continue;

                if (g_strcmp0 (state, "closing") == 0) {
                        free (state);
                        continue;
                }
                free (state);

                ret = sd_session_get_type (sessions[i], &type);

                if (ret != 0)
                        continue;

                if (g_strcmp0 (type, "x11") != 0 &&
                    g_strcmp0 (type, "wayland") != 0) {
                        free (type);
                        continue;
                }

                is_last_session = FALSE;
        }

        for (i = 0; sessions[i]; i++)
                free (sessions[i]);
        free (sessions);

        return is_last_session;
}

static void
csm_systemd_system_init (CsmSystemInterface *iface)
{
        iface->can_switch_user = csm_systemd_can_switch_user;
        iface->can_stop = csm_systemd_can_stop;
        iface->can_restart = csm_systemd_can_restart;
        iface->can_hybrid_sleep = csm_systemd_can_hybrid_sleep;
        iface->can_suspend = csm_systemd_can_suspend;
        iface->can_hibernate = csm_systemd_can_hibernate;
        iface->attempt_stop = csm_systemd_attempt_stop;
        iface->attempt_restart = csm_systemd_attempt_restart;
        iface->hybrid_sleep = csm_systemd_hybrid_sleep;
        iface->suspend = csm_systemd_suspend;
        iface->hibernate = csm_systemd_hibernate;
        iface->set_session_idle = csm_systemd_set_session_idle;
        iface->is_login_session = csm_systemd_is_login_session;
        iface->add_inhibitor = csm_systemd_add_inhibitor;
        iface->remove_inhibitor = csm_systemd_remove_inhibitor;
        iface->is_last_session_for_user = csm_systemd_is_last_session_for_user;
}

CsmSystemd *
csm_systemd_new (void)
{
        CsmSystemd *manager;

        /* logind is not running ? */
        if (access("/run/systemd/seats/", F_OK) < 0) // sd_booted ()
                return NULL;

        manager = g_object_new (CSM_TYPE_SYSTEMD, NULL);

        return manager;
}

#else /* HAVE_LOGIND */

CsmSystemd *
csm_systemd_new (void)
{
        return NULL;
}

#endif /* HAVE_LOGIND */
