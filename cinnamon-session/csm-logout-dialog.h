/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Vincent Untz
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#ifndef __CSM_LOGOUT_DIALOG_H__
#define __CSM_LOGOUT_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

enum
{
        CSM_LOGOUT_RESPONSE_LOGOUT,
        CSM_LOGOUT_RESPONSE_SWITCH_USER,
        CSM_LOGOUT_RESPONSE_SHUTDOWN,
        CSM_LOGOUT_RESPONSE_REBOOT,
        CSM_LOGOUT_RESPONSE_HIBERNATE,
        CSM_LOGOUT_RESPONSE_SLEEP
};

typedef enum {
        CSM_DIALOG_LOGOUT_TYPE_LOGOUT,
        CSM_DIALOG_LOGOUT_TYPE_SHUTDOWN,
        CSM_DIALOG_LOGOUT_TYPE_REBOOT
} CsmDialogLogoutType;

#define CSM_TYPE_LOGOUT_DIALOG         (csm_logout_dialog_get_type ())
#define CSM_LOGOUT_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CSM_TYPE_LOGOUT_DIALOG, CsmLogoutDialog))
#define CSM_LOGOUT_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CSM_TYPE_LOGOUT_DIALOG, CsmLogoutDialogClass))
#define CSM_IS_LOGOUT_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CSM_TYPE_LOGOUT_DIALOG))
#define CSM_IS_LOGOUT_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CSM_TYPE_LOGOUT_DIALOG))
#define CSM_LOGOUT_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CSM_TYPE_LOGOUT_DIALOG, CsmLogoutDialogClass))

typedef struct _CsmLogoutDialog         CsmLogoutDialog;
typedef struct _CsmLogoutDialogClass    CsmLogoutDialogClass;
typedef struct _CsmLogoutDialogPrivate  CsmLogoutDialogPrivate;

struct _CsmLogoutDialog
{
        GtkMessageDialog        parent;

        CsmLogoutDialogPrivate *priv;
};

struct _CsmLogoutDialogClass
{
        GtkMessageDialogClass  parent_class;
};

GType        csm_logout_dialog_get_type   (void) G_GNUC_CONST;

GtkWidget   *csm_get_logout_dialog        (GdkScreen           *screen,
                                           guint32              activate_time);
GtkWidget   *csm_get_shutdown_dialog      (GdkScreen           *screen,
                                           guint32              activate_time,
                                           CsmDialogLogoutType  type);

G_END_DECLS

#endif /* __CSM_LOGOUT_DIALOG_H__ */
