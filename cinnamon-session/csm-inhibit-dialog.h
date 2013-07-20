/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <mccann@jhu.edu>
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

#ifndef __CSM_INHIBIT_DIALOG_H
#define __CSM_INHIBIT_DIALOG_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "csm-store.h"

G_BEGIN_DECLS

#define CSM_TYPE_INHIBIT_DIALOG         (csm_inhibit_dialog_get_type ())
#define CSM_INHIBIT_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CSM_TYPE_INHIBIT_DIALOG, CsmInhibitDialog))
#define CSM_INHIBIT_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CSM_TYPE_INHIBIT_DIALOG, CsmInhibitDialogClass))
#define CSM_IS_INHIBIT_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CSM_TYPE_INHIBIT_DIALOG))
#define CSM_IS_INHIBIT_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CSM_TYPE_INHIBIT_DIALOG))
#define CSM_INHIBIT_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CSM_TYPE_INHIBIT_DIALOG, CsmInhibitDialogClass))

typedef struct CsmInhibitDialogPrivate CsmInhibitDialogPrivate;

typedef enum
{
        CSM_LOGOUT_ACTION_LOGOUT,
        CSM_LOGOUT_ACTION_SWITCH_USER,
        CSM_LOGOUT_ACTION_SHUTDOWN,
        CSM_LOGOUT_ACTION_REBOOT,
        CSM_LOGOUT_ACTION_HIBERNATE,
        CSM_LOGOUT_ACTION_SLEEP
} CsmLogoutAction;

typedef struct
{
        GtkDialog                parent;
        CsmInhibitDialogPrivate *priv;
} CsmInhibitDialog;

typedef struct
{
        GtkDialogClass   parent_class;
} CsmInhibitDialogClass;

GType                  csm_inhibit_dialog_get_type           (void);

GtkWidget            * csm_inhibit_dialog_new                (CsmStore         *inhibitors,
                                                              CsmStore         *clients,
                                                              int               action);
GtkTreeModel         * csm_inhibit_dialog_get_model          (CsmInhibitDialog *dialog);

G_END_DECLS

#endif /* __CSM_INHIBIT_DIALOG_H */
