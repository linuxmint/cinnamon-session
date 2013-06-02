/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *    Ray Strode <rstrode@redhat.com>
 */

#ifndef __CSM_SHELL_H__
#define __CSM_SHELL_H__

#include <glib.h>
#include <glib-object.h>

#include "csm-store.h"

G_BEGIN_DECLS

#define CSM_TYPE_SHELL             (csm_shell_get_type ())
#define CSM_SHELL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), CSM_TYPE_SHELL, CsmShell))
#define CSM_SHELL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), CSM_TYPE_SHELL, CsmShellClass))
#define CSM_IS_SHELL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CSM_TYPE_SHELL))
#define CSM_IS_SHELL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), CSM_TYPE_SHELL))
#define CSM_SHELL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), CSM_TYPE_SHELL, CsmShellClass))
#define CSM_SHELL_ERROR            (csm_shell_error_quark ())

typedef struct _CsmShell        CsmShell;
typedef struct _CsmShellClass   CsmShellClass;
typedef struct _CsmShellPrivate CsmShellPrivate;

typedef enum
{
    CSM_SHELL_END_SESSION_DIALOG_TYPE_LOGOUT = 0,
    CSM_SHELL_END_SESSION_DIALOG_TYPE_SHUTDOWN,
    CSM_SHELL_END_SESSION_DIALOG_TYPE_RESTART,
} CsmShellEndSessionDialogType;

struct _CsmShell
{
        GObject               parent;

        CsmShellPrivate *priv;
};

struct _CsmShellClass
{
        GObjectClass parent_class;

        void (* end_session_dialog_opened)        (CsmShell *shell);
        void (* end_session_dialog_open_failed)   (CsmShell *shell);
        void (* end_session_dialog_closed)        (CsmShell *shell);
        void (* end_session_dialog_canceled)      (CsmShell *shell);

        void (* end_session_dialog_confirmed_logout)   (CsmShell *shell);
        void (* end_session_dialog_confirmed_shutdown) (CsmShell *shell);
        void (* end_session_dialog_confirmed_reboot)   (CsmShell *shell);

};

GType            csm_shell_get_type           (void);

CsmShell        *csm_shell_new                (void);

CsmShell        *csm_get_shell                (void);
gboolean         csm_shell_is_running         (CsmShell *shell);

gboolean         csm_shell_open_end_session_dialog (CsmShell *shell,
                                                    CsmShellEndSessionDialogType type,
                                                    CsmStore *inhibitors);

G_END_DECLS

#endif /* __CSM_SHELL_H__ */
