/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#ifndef __CSM_INHIBIT_DIALOG_INFO_H
#define __CSM_INHIBIT_DIALOG_INFO_H

#include <glib-object.h>
#include "csm-store.h"

G_BEGIN_DECLS

typedef struct {
  CsmStore        *inhibitors;
  CsmStore        *clients;
  int              action;
  int              count;
  GVariantBuilder *builder;
} InhibitDialogInfo;

typedef enum
{
        CSM_LOGOUT_ACTION_UNDEFINED,
        CSM_LOGOUT_ACTION_LOGOUT,
        CSM_LOGOUT_ACTION_SWITCH_USER,
        CSM_LOGOUT_ACTION_SHUTDOWN,
        CSM_LOGOUT_ACTION_REBOOT,
        CSM_LOGOUT_ACTION_HIBERNATE,
        CSM_LOGOUT_ACTION_SLEEP,

} CsmLogoutAction;

GVariant *build_inhibitor_list_for_dialog (CsmStore *inhibitors,
                                            CsmStore *clients,
                                            int       action);

G_END_DECLS

#endif /* __CSM_INHIBIT_DIALOG_INFO_H */
