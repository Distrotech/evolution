/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* component-factory.c
 *
 * Authors: Ettore Perazzoli <ettore@helixcode.com>
 *
 * Copyright (C) 2000  Helix Code, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <bonobo.h>

#include "camel.h"

#include "Evolution.h"
#include "evolution-storage.h"

#include "evolution-shell-component.h"
#include "folder-browser.h"
#include "mail.h"		/* YUCK FIXME */
#include "mail-tools.h"
#include "mail-ops.h"
#include "e-util/e-gui-utils.h"
#include "e-util/e-setup.h"

#include "component-factory.h"

CamelFolder *drafts_folder = NULL;

static void create_vfolder_storage (EvolutionShellComponent *shell_component);
static void create_imap_storage (EvolutionShellComponent *shell_component);
static void create_news_storage (EvolutionShellComponent *shell_component);

#define COMPONENT_FACTORY_ID "OAFIID:evolution-shell-component-factory:evolution-mail:0ea887d5-622b-4b8c-b525-18aa1cbe18a6"

static BonoboGenericFactory *factory = NULL;
static gint running_objects = 0;

static const EvolutionShellComponentFolderType folder_types[] = {
	{ "mail", "evolution-inbox.png" },
	{ NULL, NULL }
};

/* GROSS HACK: for passing to other parts of the program */
/*EvolutionShellClient *global_shell_client = NULL;*/

/* EvolutionShellComponent methods and signals.  */

static EvolutionShellComponentResult
create_view (EvolutionShellComponent *shell_component,
	     const char *physical_uri,
	     const char *folder_type,
	     BonoboControl **control_return,
	     void *closure)
{
	BonoboControl *control;
	GtkWidget *folder_browser_widget;

	if (g_strcasecmp (folder_type, "mail") != 0)
		return EVOLUTION_SHELL_COMPONENT_UNSUPPORTEDTYPE;

	control = folder_browser_factory_new_control (physical_uri);
	if (!control)
		return EVOLUTION_SHELL_COMPONENT_NOTFOUND;

	folder_browser_widget = bonobo_control_get_widget (control);

	g_assert (folder_browser_widget != NULL);
	g_assert (IS_FOLDER_BROWSER (folder_browser_widget));

	*control_return = control;

	return EVOLUTION_SHELL_COMPONENT_OK;
}

static void
create_folder (EvolutionShellComponent *shell_component,
	       const char *physical_uri,
	       const char *type,
	       const Evolution_ShellComponentListener listener,
	       void *closure)
{
	mail_do_create_folder (listener, physical_uri, type);
}

static void
owner_set_cb (EvolutionShellComponent *shell_component,
	      EvolutionShellClient *shell_client,
	      gpointer user_data)
{
	CamelException *ex;
	CamelStore *store;
	char *url;
	
	g_print ("evolution-mail: Yeeeh! We have an owner!\n");	/* FIXME */
	
	/* GROSS HACK */
	/*global_shell_client = shell_client;*/
	
	ex = camel_exception_new ();
	url = g_strdup_printf ("mbox://%s/local/Drafts", evolution_dir);
	store = camel_session_get_store (session, url, ex);
	g_free (url);
	if (!camel_exception_is_set (ex)) {
		drafts_folder = camel_store_get_folder (store, "mbox", TRUE, ex);
		gtk_object_unref (GTK_OBJECT (store));
	} else {
		drafts_folder = NULL;
	}
	camel_exception_free (ex);
	
	create_vfolder_storage (shell_component);
	create_imap_storage (shell_component);
	create_news_storage (shell_component);
}

static void
owner_unset_cb (EvolutionShellComponent *shell_component, gpointer user_data)
{
	mail_operations_terminate ();
	gtk_main_quit ();
}

static void
factory_destroy (BonoboEmbeddable *embeddable,
		 gpointer          dummy)
{
	running_objects--;
	if (running_objects > 0)
		return;

	if (factory)
		bonobo_object_unref (BONOBO_OBJECT (factory));
	else
		g_warning ("Serious ref counting error");
	factory = NULL;

	gtk_main_quit ();
}

static BonoboObject *
factory_fn (BonoboGenericFactory *factory, void *closure)
{
	EvolutionShellComponent *shell_component;

	running_objects++;

	shell_component = evolution_shell_component_new (folder_types,
							 create_view,
							 create_folder,
							 NULL,
							 NULL,
							 NULL);

	gtk_signal_connect (GTK_OBJECT (shell_component), "destroy",
			    GTK_SIGNAL_FUNC (factory_destroy), NULL);
	gtk_signal_connect (GTK_OBJECT (shell_component), "owner_set",
			    GTK_SIGNAL_FUNC (owner_set_cb), NULL);
	gtk_signal_connect (GTK_OBJECT (shell_component), "owner_unset",
			    GTK_SIGNAL_FUNC (owner_unset_cb), NULL);

	return BONOBO_OBJECT (shell_component);
}

void
component_factory_init (void)
{
	if (factory != NULL)
		return;

	factory = bonobo_generic_factory_new (COMPONENT_FACTORY_ID, factory_fn, NULL);

	if (factory == NULL) {
		e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
			  _("Cannot initialize Evolution's mail component."));
		exit (1);
	}
}

/* FIXME: remove */
static void
create_vfolder_storage (EvolutionShellComponent *shell_component)
{
	void vfolder_create_storage(EvolutionShellComponent *shell_component);

	vfolder_create_storage(shell_component);
}

static void
create_imap_storage (EvolutionShellComponent *shell_component)
{
	const MailConfig *config;
	EvolutionShellClient *shell_client;
	Evolution_Shell corba_shell;
	EvolutionStorage *storage;
	char *source = NULL, *server, *p;

	config = mail_config_fetch ();
	if (config->sources) {
		const MailConfigService *s;
		s = (MailConfigService *)config->sources->data;
		source = s->url;
	}

	if (!source || strncasecmp (source, "imap://", 7))
		return;
	
	shell_client = evolution_shell_component_get_owner (shell_component);
	if (shell_client == NULL) {
		g_warning ("We have no shell!?");
		return;
	}

	corba_shell = bonobo_object_corba_objref (BONOBO_OBJECT (shell_client));

	if (!(server = strchr (source, '@'))) {
		g_free (source);
		return;
	}
	
	server++;
	for (p = server; *p && *p != '/'; p++);

	server = g_strndup (server, (gint)(p - server));
	
	storage = evolution_storage_new (server);
	g_free (server);

	if (evolution_storage_register_on_shell (storage, corba_shell) != EVOLUTION_STORAGE_OK) {
		g_warning ("Cannot register storage");
		g_free (source);
		return;
	}

	mail_do_scan_subfolders (source, TRUE, storage);
}

static void
create_news_storage (EvolutionShellComponent *shell_component)
{
	const MailConfig *config;
	EvolutionShellClient *shell_client;
	Evolution_Shell corba_shell;
	EvolutionStorage *storage;
	char *source=NULL, *server, *p;

	config = mail_config_fetch ();
	if (config->news) {
		const MailConfigService *s;
		s = (MailConfigService *)config->news->data;
		source = s->url;
	}

	if (!source || strncasecmp (source, "news://", 7))
		return;
	
	shell_client = evolution_shell_component_get_owner (shell_component);
	if (shell_client == NULL) {
		g_warning ("We have no shell!?");
		return;
	}

	corba_shell = bonobo_object_corba_objref (BONOBO_OBJECT (shell_client));

	server = source + 7;
	for (p = server; *p && *p != '/'; p++);

	server = g_strndup (server, (gint)(p - server));
	
	storage = evolution_storage_new (server);
	g_free (server);

	if (evolution_storage_register_on_shell (storage, corba_shell) != EVOLUTION_STORAGE_OK) {
		g_warning ("Cannot register storage");
		g_free (source);
		return;
	}

	mail_do_scan_subfolders (source, FALSE, storage);
}

