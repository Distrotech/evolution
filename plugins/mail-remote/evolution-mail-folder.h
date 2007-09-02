/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Copyright (C) 2005  Novell, Inc.
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Michael Zucchi <notzed@novell.com>
 */

#ifndef _EVOLUTION_MAIL_FOLDER_H_
#define _EVOLUTION_MAIL_FOLDER_H_

#include <bonobo/bonobo-object.h>
#include "Evolution-DataServer-Mail.h"

#define EVOLUTION_MAIL_TYPE_FOLDER			(evolution_mail_folder_get_type ())
#define EVOLUTION_MAIL_FOLDER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), EVOLUTION_MAIL_TYPE_FOLDER, EvolutionMailFolder))
#define EVOLUTION_MAIL_FOLDER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), EVOLUTION_MAIL_TYPE_FOLDER, EvolutionMailFolderClass))
#define EVOLUTION_MAIL_IS_FOLDER(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVOLUTION_MAIL_TYPE_FOLDER))
#define EVOLUTION_MAIL_IS_FOLDER_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), EVOLUTION_MAIL_TYPE_FOLDER))

struct _EvolutionMailStore;

typedef struct _EvolutionMailFolder        EvolutionMailFolder;
typedef struct _EvolutionMailFolderClass   EvolutionMailFolderClass;

struct _EvolutionMailFolder {
	BonoboObject parent;

	struct _EvolutionMailStore *store;

	char *full_name;
	char *name;
};

struct _EvolutionMailFolderClass {
	BonoboObjectClass parent_class;

	POA_Evolution_Mail_Folder__epv epv;
};

GType           evolution_mail_folder_get_type(void);

EvolutionMailFolder *evolution_mail_folder_new(struct _EvolutionMailStore *ems, const char *name, const char *full_name);

void evolution_mail_folder_addlistener(EvolutionMailFolder *emf, Evolution_Mail_FolderListener listener);
void evolution_mail_folder_changed(EvolutionMailFolder *emf, Evolution_Mail_FolderChanges *changes);

struct _CamelFolder *evolution_mail_folder_get_folder(EvolutionMailFolder *emf, CORBA_Environment *ev);
int evolution_mail_folder_close_folder(EvolutionMailFolder *emf);

#endif /* _EVOLUTION_MAIL_FOLDER_H_ */
