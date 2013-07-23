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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 *
 * Authors:
 *	Colin Walters <walters@verbum.org>
 */

#ifndef __CSM_FAIL_WHALE_DIALOG_H__
#define __CSM_FAIL_WHALE_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CSM_TYPE_FAIL_WHALE_DIALOG         (csm_fail_whale_dialog_get_type ())
#define CSM_FAIL_WHALE_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CSM_TYPE_FAIL_WHALE_DIALOG, CsmFailWhaleDialog))
#define CSM_FAIL_WHALE_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CSM_TYPE_FAIL_WHALE_DIALOG, CsmFailWhaleDialogClass))
#define CSM_IS_FAIL_WHALE_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CSM_TYPE_FAIL_WHALE_DIALOG))
#define CSM_IS_FAIL_WHALE_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CSM_TYPE_FAIL_WHALE_DIALOG))
#define CSM_FAIL_WHALE_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CSM_TYPE_FAIL_WHALE_DIALOG, CsmFailWhaleDialogClass))

typedef struct _CsmFailWhaleDialog         CsmFailWhaleDialog;
typedef struct _CsmFailWhaleDialogClass    CsmFailWhaleDialogClass;
typedef struct _CsmFailWhaleDialogPrivate  CsmFailWhaleDialogPrivate;

struct _CsmFailWhaleDialog
{
        GtkWindow                  parent;

        CsmFailWhaleDialogPrivate *priv;
};

struct _CsmFailWhaleDialogClass
{
        GtkWindowClass  parent_class;
};

GType        csm_fail_whale_dialog_get_type   (void) G_GNUC_CONST;

void         csm_fail_whale_dialog_we_failed  (gboolean            debug_mode,
                                               gboolean            allow_logout);

G_END_DECLS

#endif /* __CSM_FAIL_WHALE_DIALOG_H__ */
