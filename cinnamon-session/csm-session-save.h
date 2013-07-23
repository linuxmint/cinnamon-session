/* csm-session-save.h
 * Copyright (C) 2008 Lucas Rocha.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#ifndef __CSM_SESSION_SAVE_H__
#define __CSM_SESSION_SAVE_H__

#include <glib.h>

#include "csm-store.h"

G_BEGIN_DECLS

void      csm_session_save                 (CsmStore  *client_store,
                                            GError   **error);
void      csm_session_save_clear           (void);

G_END_DECLS

#endif /* __CSM_SESSION_SAVE_H__ */
