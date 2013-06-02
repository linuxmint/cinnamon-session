/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc.
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
 *	Colin Walters <walters@verbum.org>
 */

#ifndef __GSM_FAIL_WHALE_DIALOG_H__
#define __GSM_FAIL_WHALE_DIALOG_H__

#include <gtk/gtk.h>

#include "gsm-shell-extensions.h"

G_BEGIN_DECLS

#define GSM_TYPE_FAIL_WHALE_DIALOG         (gsm_fail_whale_dialog_get_type ())
#define GSM_FAIL_WHALE_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSM_TYPE_FAIL_WHALE_DIALOG, GsmFailWhaleDialog))
#define GSM_FAIL_WHALE_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSM_TYPE_FAIL_WHALE_DIALOG, GsmFailWhaleDialogClass))
#define GSM_IS_FAIL_WHALE_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSM_TYPE_FAIL_WHALE_DIALOG))
#define GSM_IS_FAIL_WHALE_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSM_TYPE_FAIL_WHALE_DIALOG))
#define GSM_FAIL_WHALE_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSM_TYPE_FAIL_WHALE_DIALOG, GsmFailWhaleDialogClass))

typedef struct _GsmFailWhaleDialog         GsmFailWhaleDialog;
typedef struct _GsmFailWhaleDialogClass    GsmFailWhaleDialogClass;
typedef struct _GsmFailWhaleDialogPrivate  GsmFailWhaleDialogPrivate;

struct _GsmFailWhaleDialog
{
        GtkWindow                  parent;

        GsmFailWhaleDialogPrivate *priv;
};

struct _GsmFailWhaleDialogClass
{
        GtkWindowClass  parent_class;
};

GType        gsm_fail_whale_dialog_get_type   (void) G_GNUC_CONST;

void         gsm_fail_whale_dialog_we_failed  (gboolean            debug_mode,
                                               gboolean            allow_logout,
                                               GsmShellExtensions *extensions);

G_END_DECLS

#endif /* __GSM_FAIL_WHALE_DIALOG_H__ */
