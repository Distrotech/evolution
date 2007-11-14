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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <bonobo/bonobo-i18n.h>
#include <bonobo/bonobo-exception.h>
#include "evolution-mail-folderlistener.h"

#include "evolution-mail-marshal.h"
#include "e-corba-utils.h"

#define PARENT_TYPE bonobo_object_get_type ()

static BonoboObjectClass *parent_class = NULL;

#define _PRIVATE(o) (g_type_instance_get_private ((GTypeInstance *)o, evolution_mail_folderlistener_get_type()))

struct _EvolutionMailFolderListenerPrivate {
	int dummy;
};

enum {
	EML_CHANGED,
	EML_LAST_SIGNAL
};

static guint eml_signals[EML_LAST_SIGNAL];

/* GObject methods */

static void
impl_dispose (GObject *object)
{
	struct _EvolutionMailFolderListenerPrivate *p = _PRIVATE(object);

	p = p;

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	d(printf("EvolutionMailFolderListener finalised!\n"));

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Evolution.Mail.Listener */
static const char *change_type_name(int type)
{
	switch (type) {
	case Evolution_Mail_ADDED:
		return "added";
		break;
	case Evolution_Mail_CHANGED:
		return "changed";
		break;
	case Evolution_Mail_REMOVED:
		return "removed";
		break;
	default:
		return "";
	}
}

static void
impl_changed(PortableServer_Servant _servant,
	     const Evolution_Mail_Folder folder,
	     const Evolution_Mail_FolderChanges * changes,
	     CORBA_Environment * ev)
{
	EvolutionMailFolderListener *eml = (EvolutionMailFolderListener *)bonobo_object_from_servant(_servant);
	int i, j;

	d(printf("folder changed!\n"));
	for (i=0;i<changes->_length;i++) {
		d(printf(" %d %s", changes->_buffer[i].messages._length, change_type_name(changes->_buffer[i].type)));
		for (j=0;j<changes->_buffer[i].messages._length;j++) {
			d(printf(" %s %s\n", changes->_buffer[i].messages._buffer[j].uid, changes->_buffer[i].messages._buffer[j].subject));
		}
	}

	g_signal_emit(eml, eml_signals[EML_CHANGED], 0, folder, changes);
}

/* Initialization */

static void
evolution_mail_folderlistener_class_init (EvolutionMailFolderListenerClass *klass)
{
	POA_Evolution_Mail_FolderListener__epv *epv = &klass->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	epv->changed = impl_changed;

	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;

	g_type_class_add_private(klass, sizeof(struct _EvolutionMailFolderListenerPrivate));

	eml_signals[EML_CHANGED] =
		g_signal_new("changed",
			     G_OBJECT_CLASS_TYPE (klass),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET (EvolutionMailFolderListenerClass, changed),
			     NULL, NULL,
			     evolution_mail_marshal_VOID__POINTER_POINTER,
			     G_TYPE_NONE, 2,
			     G_TYPE_POINTER, G_TYPE_POINTER);
}

static void
evolution_mail_folderlistener_init (EvolutionMailFolderListener *ems, EvolutionMailFolderListenerClass *klass)
{
	struct _EvolutionMailFolderListenerPrivate *p = _PRIVATE(ems);

	p = p;
}

EvolutionMailFolderListener *
evolution_mail_folderlistener_new(void)
{
	EvolutionMailFolderListener *eml;

	eml = g_object_new(evolution_mail_folderlistener_get_type(), NULL);

	return eml;
}

BONOBO_TYPE_FUNC_FULL (EvolutionMailFolderListener, Evolution_Mail_FolderListener, PARENT_TYPE, evolution_mail_folderlistener)
