/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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


#ifndef __CSM_MANAGER_H
#define __CSM_MANAGER_H

#include <glib-object.h>

#include "csm-store.h"

G_BEGIN_DECLS

#define CSM_TYPE_MANAGER         (csm_manager_get_type ())
#define CSM_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CSM_TYPE_MANAGER, CsmManager))
#define CSM_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CSM_TYPE_MANAGER, CsmManagerClass))
#define CSM_IS_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CSM_TYPE_MANAGER))
#define CSM_IS_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CSM_TYPE_MANAGER))
#define CSM_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CSM_TYPE_MANAGER, CsmManagerClass))

typedef struct CsmManagerPrivate CsmManagerPrivate;

typedef struct
{
        GObject            parent;
        CsmManagerPrivate *priv;
} CsmManager;

typedef struct
{
        GObjectClass   parent_class;

        void          (* session_running)     (CsmManager      *manager);
        void          (* session_over)        (CsmManager      *manager);
        void          (* session_over_notice) (CsmManager      *manager);

        void          (* phase_changed)       (CsmManager      *manager,
                                               const char      *phase);

        void          (* client_added)        (CsmManager      *manager,
                                               const char      *id);
        void          (* client_removed)      (CsmManager      *manager,
                                               const char      *id);
        void          (* inhibitor_added)     (CsmManager      *manager,
                                               const char      *id);
        void          (* inhibitor_removed)   (CsmManager      *manager,
                                               const char      *id);
} CsmManagerClass;

typedef enum {
        /* csm's own startup/initialization phase */
        CSM_MANAGER_PHASE_STARTUP = 0,
        /* gnome-initial-setup */
        CSM_MANAGER_PHASE_EARLY_INITIALIZATION,
        /* gnome-keyring-daemon */
        CSM_MANAGER_PHASE_PRE_DISPLAY_SERVER,
        /* xrandr setup, gnome-settings-daemon, etc */
        CSM_MANAGER_PHASE_INITIALIZATION,
        /* window/compositing managers */
        CSM_MANAGER_PHASE_WINDOW_MANAGER,
        /* apps that will create _NET_WM_WINDOW_TYPE_PANEL windows */
        CSM_MANAGER_PHASE_PANEL,
        /* apps that will create _NET_WM_WINDOW_TYPE_DESKTOP windows */
        CSM_MANAGER_PHASE_DESKTOP,
        /* everything else */
        CSM_MANAGER_PHASE_APPLICATION,
        /* done launching */
        CSM_MANAGER_PHASE_RUNNING,
        /* shutting down */
        CSM_MANAGER_PHASE_QUERY_END_SESSION,
        CSM_MANAGER_PHASE_END_SESSION,
        CSM_MANAGER_PHASE_EXIT
} CsmManagerPhase;

typedef enum
{
        CSM_MANAGER_ERROR_GENERAL = 0,
        CSM_MANAGER_ERROR_NOT_IN_INITIALIZATION,
        CSM_MANAGER_ERROR_NOT_IN_RUNNING,
        CSM_MANAGER_ERROR_ALREADY_REGISTERED,
        CSM_MANAGER_ERROR_NOT_REGISTERED,
        CSM_MANAGER_ERROR_INVALID_OPTION,
        CSM_MANAGER_ERROR_LOCKED_DOWN,
        CSM_MANAGER_NUM_ERRORS
} CsmManagerError;

#define CSM_MANAGER_ERROR csm_manager_error_quark ()

typedef enum {
        CSM_MANAGER_LOGOUT_MODE_NORMAL = 0,
        CSM_MANAGER_LOGOUT_MODE_NO_CONFIRMATION,
        CSM_MANAGER_LOGOUT_MODE_FORCE
} CsmManagerLogoutMode;

GType               csm_manager_error_get_type                 (void);
#define CSM_MANAGER_TYPE_ERROR (csm_manager_error_get_type ())

GQuark              csm_manager_error_quark                    (void);
GType               csm_manager_get_type                       (void);

CsmManager *        csm_manager_new                            (CsmStore       *client_store,
                                                                gboolean        failsafe);
CsmManager *        csm_manager_get                            (void);

gboolean            csm_manager_get_failsafe                   (CsmManager     *manager);

gboolean            csm_manager_add_autostart_app              (CsmManager     *manager,
                                                                const char     *path,
                                                                const char     *provides);
gboolean            csm_manager_add_required_app               (CsmManager     *manager,
                                                                const char     *path,
                                                                const char     *provides);
gboolean            csm_manager_add_autostart_apps_from_dir    (CsmManager     *manager,
                                                                const char     *path);
gboolean            csm_manager_add_legacy_session_apps        (CsmManager     *manager,
                                                                const char     *path);

void                csm_manager_start                          (CsmManager     *manager);

const char *        _csm_manager_get_default_session           (CsmManager     *manager);

void                _csm_manager_set_active_session            (CsmManager     *manager,
                                                                const char     *session_name,
                                                                gboolean        is_fallback);

gboolean            csm_manager_set_phase                      (CsmManager     *manager,
                                                                CsmManagerPhase phase);

gboolean            csm_manager_logout                         (CsmManager *manager,
                                                                guint       logout_mode,
                                                                GError    **error);

gboolean            csm_manager_get_app_is_blacklisted         (CsmManager     *manager,
                                                                const gchar    *name);
gboolean            csm_manager_get_autosave_enabled           (CsmManager     *manager);

G_END_DECLS

#endif /* __CSM_MANAGER_H */
