/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright 2000, Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* This file is a F*CKING MESS.  Shame to us!  */

#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <gtkhtml/gtkhtml.h>
#include <camel/camel.h>
#include <composer/e-msg-composer.h>
#include <shell/evolution-storage.h>
#include "mail-accounts.h"
#include "mail-account-editor.h"
#include "mail-callbacks.h"
#include "mail-config.h"
#include "mail-config-druid.h"
#include "mail-display-stream.h"
#include "mail-session.h"
#include "mail-types.h"

extern char *evolution_dir;

/* mail-identify */
char *mail_identify_mime_part (CamelMimePart *part, MailDisplay *md);

/* component factory for lack of a better place */
void mail_add_storage (CamelStore *store, const char *name, const char *uri);
void mail_load_storage_by_uri (GNOME_Evolution_Shell shell, const char *uri, const char *name);
/*takes a GSList of MailConfigServices */
void mail_load_storages (GNOME_Evolution_Shell shell, EAccountList *sources);

void mail_hash_storage (CamelService *store, EvolutionStorage *storage);
EvolutionStorage *mail_lookup_storage (CamelStore *store);
void mail_remove_storage_by_uri (const char *uri);
void mail_remove_storage (CamelStore *store);
void mail_storages_foreach (GHFunc func, gpointer data);
int  mail_storages_count (void);

gboolean evolution_folder_info_factory_init (void);

