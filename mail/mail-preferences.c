/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtkhtml/gtkhtml-properties.h>

#include "mail-preferences.h"

static void mail_preferences_class_init (MailPreferencesClass *class);
static void mail_preferences_init       (MailPreferences *dialog);
static void mail_preferences_finalise   (GtkObject *obj);

static GtkVboxClass *parent_class = NULL;


GtkType
mail_preferences_get_type (void)
{
	static GtkType type = 0;
	
	if (!type) {
		GtkTypeInfo type_info = {
			"MailPreferences",
			sizeof (MailPreferences),
			sizeof (MailPreferencesClass),
			(GtkClassInitFunc) mail_preferences_class_init,
			(GtkObjectInitFunc) mail_preferences_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL
		};
		
		type = gtk_type_unique (gtk_vbox_get_type (), &type_info);
	}
	
	return type;
}

static void
mail_preferences_class_init (MailPreferencesClass *klass)
{
	GtkObjectClass *object_class;
	
	object_class = (GtkObjectClass *) klass;
	parent_class = gtk_type_class (gtk_vbox_get_type ());
	
	object_class->finalize = mail_preferences_finalise;
	/* override methods */
	
}

static void
mail_preferences_init (MailPreferences *preferences)
{
	preferences->gconf = gconf_client_get_default ();
}

static void
mail_preferences_finalise (GtkObject *obj)
{
	MailPreferences *preferences = (MailPreferences *) obj;
	
	gtk_object_unref (GTK_OBJECT (preferences->gui));
	gtk_object_unref (GTK_OBJECT (preferences->gconf));
	
        ((GtkObjectClass *)(parent_class))->finalize (obj);
}


static void
mail_preferences_construct (MailPreferences *pref)
{
	GtkWidget *toplevel, *menu;
	const char *text;
	GladeXML *gui;
	
	gui = glade_xml_new (EVOLUTION_GLADEDIR "/mail-config.glade", "preferences_tab");
	pref->gui = gui;
	
	/* get our toplevel widget */
	toplevel = glade_xml_get_widget (gui, "toplevel");
	
	/* reparent */
	gtk_widget_reparent (toplevel, GTK_VBOX (pref));
	
	/* General tab */
	
	/* Message Display */
	pref->timeout_toggle = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "chkMarkTimeout"));
	gtk_toggle_button_set_active (pref->timeout_toggle, mail_config_do_seen_timeout ());
	
	pref->timeout = GTK_SPIN_BUTTON (glade_xml_get_widget (gui, "spinMarkTimeout"));
	gtk_spin_button_set_value (pref->timeout, (1.0 * mail_config_get_mark_as_seen_timeout ()) / 1000.0);
	
	pref->charset = GTK_OPTION_MENU (glade_xml_get_widget (gui, "omenuCharset"));
	menu = e_charset_picker_new (mail_config_get_default_charset ());
	gtk_option_menu_set_menu (pref->charset, GTK_WIDGET (menu));
	
	pref->citation_highlight = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "chkHighlightCitations"));
	gtk_toggle_button_set_active (pref->citation_highlight, mail_config_get_citation_highlight ());
	
	pref->citation_color = GNOME_COLOR_PICKER (glade_xml_get_widget (gui, "colorpickerCitations"));
	colorpicker_set_color (pref->citation_color, mail_config_get_citation_color ());
	
	/* Deleting Mail */
	pref->empty_trash = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "chkEmptyTrashOnExit"));
	gtk_toggle_button_set_active (pref->empty_trash, mail_config_get_empty_trash_on_exit ());
	
	pref->confirm_expunge = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "chkConfirmExpunge"));
	gtk_toggle_button_set_active (pref->confirm_expunge, mail_config_get_confirm_expunge ());
	
	/* New Mail Notification */
	pref->notify_not = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "radNotifyNot"));
	gtk_toggle_button_set_active (pref->notify_not, mail_config_get_new_mail_notify () == MAIL_CONFIG_NOTIFY_NOT);
	
	pref->notify_beep = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "radNotifyBeep"));
	gtk_toggle_button_set_active (pref->notify_beep, mail_config_get_new_mail_notify () == MAIL_CONFIG_NOTIFY_BEEP);
	
	pref->notify_play_sound = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "radNotifyPlaySound"));
	gtk_toggle_button_set_active (pref->notify_play_sound,
				      mail_config_get_new_mail_notify () == MAIL_CONFIG_NOTIFY_PLAY_SOUND);
	
	pref->notify_sound_file = GNOME_FILE_ENTRY (glade_xml_get_widget (gui, "fileNotifyPlaySound"));
	text = mail_config_get_new_mail_notify_sound_file ();
	gtk_entry_set_text (GTK_ENTRY (gnome_file_entry_gtk_entry (pref->notify_sound_file)),
			    text ? text : "");
	
	/* HTML Mail tab */
	
	/* Loading Images */
	pref->images_never = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "radImagesNever"));
	gtk_toggle_button_set_active (pref->images_never, mail_config_get_http_mode () == MAIL_CONFIG_HTTP_NEVER);
	
	pref->images_sometimes = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "radImagesSometimes"));
	gtk_toggle_button_set_active (pref->images_sometimes, mail_config_get_http_mode () == MAIL_CONFIG_HTTP_SOMETIMES);
	
	pref->images_always = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "radImagesAlways"));
	gtk_toggle_button_set_active (pref->images_always, mail_config_get_http_mode () == MAIL_CONFIG_HTTP_ALWAYS);
	
	/* ... */
	/* FIXME: use the gtkhtml interfaces for these settings when lewing gets around to adding them */
	pref->show_animated = GTK_TOGGLE_BUTTON (glade_xml_new (gui, "chkShowAnimatedImages"));
	gtk_toggle_button_set_active (pref->show_animated,
				      gconf_client_get_bool (pref->gconf, GTK_HTML_GCONF_DIR "/animations", NULL));
	
	pref->autodetect_links = GTK_TOGGLE_BUTTON (glade_xml_new (gui, "chkAutoDetectLinks"));
	gtk_toggle_button_set_active (pref->autodetect_links,
				      gconf_client_get_bool (pref->gconf, GTK_HTML_GCONF_DIR "/magic_links", NULL));
	
	pref->prompt_unwanted_html = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "chkPromptWantHTML"));
	gtk_toggle_button_set_active (pref->prompt_unwanted_html, mail_config_get_confirm_unwanted_html ());
	
	/* Security tab */
	
	/* Pretty Good Privacy */
	pref->pgp_path = GNOME_FILE_ENTRY (glade_xml_get_widget (gui, "filePgpPath"));
	text = mail_config_get_pgp_path ();
	gtk_entry_set_text (GTK_ENTRY (gnome_file_entry_gtk_entry (dialog->pgp_path)),
			    text ? text : "");
	gnome_file_entry_set_default_path (dialog->pgp_path, mail_config_get_pgp_path ());
	
	/* FIXME: what about a label colour tab? */
}


GtkWidget *
mail_preferences_new (void)
{
	MailPreferences *new;
	
	new = (MailPreferences *) gtk_type_new (mail_preferences_get_type ());
	mail_preferences_construct (new);
	
	return (GtkWidget *) new;
}


void
mail_preferences_apply (MailPreferences *prefs)
{
	GtkWidget *entry, *menu;
	CamelPgpType type;
	char *string;
	guint32 rgb;
	int val;
	
	/* General tab */
	
	/* Message Display */
	mail_config_set_do_timeout (gtk_toggle_button_get_active (prefs->timeout_toggle));
	
	val = (int) (gtk_spin_button_get_value_as_float (prefs->timeout) * 1000);
	mail_config_set_mark_as_seen_timeout (val);
	
	menu = gtk_option_menu_get_menu (prefs->charset);
	string = e_charset_picker_get_charset (menu);
	if (string) {
		mail_config_set_default_charset (string);
		g_free (string);
	}
	
	mail_config_set_citation_highlight (gtk_toggle_button_get_active (prefs->citation_highlight));
	
	rgb = colorpicker_get_color (prefs->citation_color);
	mail_config_set_citation_color (rgb);
	
	/* Deleting Mail */
	mail_config_set_empty_trash (gtk_toggle_button_get_active (prefs->empty_trash));
	
	mail_config_set_confirm_expinge (gtk_toggle_button_get_active (prefs->confirm_expunge));
	
	/* New Mail Notification */
	if (gtk_toggle_button_get_active (prefs->notify_not))
		mail_config_set_new_mail_notify (MAIL_CONFIG_NOTIFY_NOT);
	else if (gtk_toggle_button_get_active (prefs->notify_beep))
		mail_config_set_new_mail_notify (MAIL_CONFIG_NOTIFY_BEEP);
	else
		mail_config_set_new_mail_notify (MAIL_CONFIG_NOTIFY_PLAY_SOUND);
	
	entry = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (prefs->notify_sound_file));
	string = gtk_entry_get_text (GTK_ENTRY (entry));
	mail_config_set_new_mail_notify_sound_file (string);
	
	/* HTML Mail */
	if (gtk_toggle_button_get_active (prefs->images_always))
		mail_config_set_http_mode (MAIL_CONFIG_HTTP_ALWAYS);
	else if (gtk_toggle_button_get_active (prefs->notify_sometimes))
		mail_config_set_http_mode (MAIL_CONFIG_HTTP_SOMETIMES);
	else
		mail_config_set_http_mode (MAIL_CONFIG_HTTP_NEVER);
	
	gconf_client_set_bool (prefs->gconf, GTK_HTML_GCONF_DIR "/animations",
			       gtk_toggle_button_get_active (prefs->show_animations), NULL);
	
	gconf_client_set_bool (prefs->gconf, GTK_HTML_GCONF_DIR "/magic_links",
			       gtk_toggle_button_get_active (prefs->autodetect_links), NULL);
	
	mail_config_set_confirm_unwanted_html (gtk_toggle_button_get_active (prefs->prompt_unwanted_html));
	
	/* Security */
	entry = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (prefs->notify_sound_file));
	string = gtk_entry_get_text (GTK_ENTRY (entry));
	
	type = string && *string ? mail_config_pgp_type_detect_from_path (string) : CAMEL_PGP_TYPE_NONE;
	mail_config_set_pgp_path (string && *string ? string : NULL);
	mail_config_set_pgp_type (type);
}


/* Implementation of the factory for the configuration control.  */

#define CONFIG_CONTROL_FACTORY_ID "OAFIID:GNOME_Evolution_Mail_Preferences_ConfigControlFactory"

static BonoboGenericFactory *factory = NULL;

static void
config_control_destroy_callback (EvolutionConfigControl *config_control, void *data)
{
	MailPreferences *preference = (MailPreferences *) data;
	
	/* nothing to do? */
}

static void
config_control_apply_callback (EvolutionConfigControl *config_control, void *data)
{
	MailPreferences *preferences = (MailPreferences *) data;
	
	mail_preferences_apply (preferences);
}


static BonoboObject *
config_control_factory_fn (BonoboGenericFactory *factory, void *data)
{
	GNOME_Evolution_Shell shell = (GNOME_Evolution_Shell) data;
	EvolutionConfigControl *control;
	MailPreferences *preferences;
	
	preferences = mail_preferences_new ();
	
	control = evolution_config_control_new (preferences);
	gtk_signal_connect (GTK_OBJECT (control), "apply",
			    GTK_SIGNAL_FUNC (config_control_apply_callback), preferences);
	gtk_signal_connect (GTK_OBJECT (control), "destroy",
			    GTK_SIGNAL_FUNC (config_control_destroy_callback), preferences);
	
	gtk_widget_unref (preferences);
	
	return BONOBO_OBJECT (control);
}

gboolean
mail_preferences_register_factory (GNOME_Evolution_Shell shell)
{
	g_return_val_if_fail (shell != CORBA_OBJECT_NIL, FALSE);
	
	factory = bonobo_generic_factory_new (CONFIG_CONTROL_FACTORY_ID,
					      config_control_factory_fn,
					      shell);
	
	if (factory != NULL) {
		return TRUE;
	} else {
		g_warning ("Cannot register factory %s", CONFIG_CONTROL_FACTORY_ID);
		return FALSE;
	}
}

