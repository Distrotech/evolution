/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Copyright (C) 2001 Ximian, Inc.
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-charset-picker.h"
#include <gal/widgets/e-gui-utils.h>
#include <gal/unicode/gunicode.h>

#include <iconv.h>

#include <gtk/gtklabel.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtksignal.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <libgnomeui/gnome-stock.h>
#include <bonobo/bonobo-ui-node.h>
#include <bonobo/bonobo-ui-util.h>

typedef enum {
	E_CHARSET_UNKNOWN,
	E_CHARSET_BALTIC,
	E_CHARSET_CENTRAL_EUROPEAN,
	E_CHARSET_CHINESE,
	E_CHARSET_CYRILLIC,
	E_CHARSET_GREEK,
	E_CHARSET_JAPANESE,
	E_CHARSET_KOREAN,
	E_CHARSET_TURKISH,
	E_CHARSET_UNICODE,
	E_CHARSET_WESTERN_EUROPEAN
} ECharsetClass;

static const char *classnames[] = {
	N_("Unknown"),
	N_("Baltic"),
	N_("Central European"),
	N_("Chinese"),
	N_("Cyrillic"),
	N_("Greek"),
	N_("Japanese"),
	N_("Korean"),
	N_("Turkish"),
	N_("Unicode"),
	N_("Western European"),
};

typedef struct {
	char *name;
	ECharsetClass class;
	char *subclass;
} ECharset;

/* This list is based on what other mailers/browsers support. There's
 * not a lot of point in using, say, ISO-8859-3, if anything that can
 * read that can read UTF8 too.
 */
static ECharset charsets[] = {
	{ "ISO-8859-13", E_CHARSET_BALTIC, NULL },
	{ "ISO-8859-4", E_CHARSET_BALTIC, NULL },
	{ "ISO-8859-2", E_CHARSET_CENTRAL_EUROPEAN, NULL },
	{ "Big5", E_CHARSET_CHINESE, N_("Traditional") },
	{ "GB-2312", E_CHARSET_CHINESE, N_("Simplified") },
	{ "HZ", E_CHARSET_CHINESE, N_("Simplified") },
	{ "ISO-2022-CN", E_CHARSET_CHINESE, NULL },
	{ "KOI8-R", E_CHARSET_CYRILLIC, NULL },
	{ "Windows-1251", E_CHARSET_CYRILLIC, NULL },
	{ "KOI8-U", E_CHARSET_CYRILLIC, N_("Ukrainian") },
	{ "ISO-8859-5", E_CHARSET_CYRILLIC, NULL },
	{ "ISO-8859-7", E_CHARSET_GREEK, NULL },
	{ "ISO-2022-JP", E_CHARSET_JAPANESE, NULL },
	{ "EUC-JP", E_CHARSET_JAPANESE, NULL },
	{ "Shift_JIS", E_CHARSET_JAPANESE, NULL },
	{ "EUC-KR", E_CHARSET_KOREAN, NULL },
	{ "ISO-8859-9", E_CHARSET_TURKISH, NULL },
	{ "UTF-8", E_CHARSET_UNICODE, NULL },
	{ "UTF-7", E_CHARSET_UNICODE, NULL },
	{ "ISO-8859-1", E_CHARSET_WESTERN_EUROPEAN, NULL },
	{ "ISO-8859-15", E_CHARSET_WESTERN_EUROPEAN, N_("New") },
};
static const int num_charsets = sizeof (charsets) / sizeof (charsets[0]);

static void
select_item (GtkMenuShell *menu_shell, GtkWidget *item)
{
	gtk_menu_shell_select_item (menu_shell, item);
	gtk_menu_shell_deactivate (menu_shell);
}

static void
activate (GtkWidget *item, gpointer menu)
{
	gtk_object_set_data (GTK_OBJECT (menu), "activated_item", item);
}

static GtkWidget *
add_charset (GtkWidget *menu, ECharset *charset, gboolean free_name)
{
	GtkWidget *item;
	char *label;

	if (charset->subclass) {
		label = g_strdup_printf ("%s, %s (%s)",
					 _(classnames[charset->class]),
					 _(charset->subclass),
					 charset->name);
	} else {
		label = g_strdup_printf ("%s (%s)",
					 _(classnames[charset->class]),
					 charset->name);
	}

	item = gtk_menu_item_new_with_label (label);
	gtk_object_set_data_full (GTK_OBJECT (item), "charset",
				  charset->name, free_name ? g_free : NULL);
	g_free (label);

	gtk_widget_show (item);
	gtk_menu_append (GTK_MENU (menu), item);
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (activate), menu);

	return item;
}

static gboolean
add_other_charset (GtkWidget *menu, GtkWidget *other, char *new_charset) 
{
	ECharset charset = { NULL, E_CHARSET_UNKNOWN, NULL };
	GtkWidget *item;
	iconv_t ic;

	ic = iconv_open ("UTF-8", new_charset);
	if (ic == (iconv_t)-1) {
		GtkWidget *window = gtk_widget_get_ancestor (other, GTK_TYPE_WINDOW);
		e_notice (GTK_WINDOW (window), GNOME_MESSAGE_BOX_ERROR,
			  _("Unknown character set: %s"), new_charset);
		return FALSE;
	}
	iconv_close (ic);

	/* Temporarily remove the "Other..." item */
	gtk_object_ref (GTK_OBJECT (other));
	gtk_container_remove (GTK_CONTAINER (menu), other);

	/* Create new menu item */
	charset.name = new_charset;
	item = add_charset (menu, &charset, TRUE);

	/* And re-add "Other..." */
	gtk_menu_append (GTK_MENU (menu), other);
	gtk_object_unref (GTK_OBJECT (other));

	gtk_object_set_data_full (GTK_OBJECT (menu), "other_charset",
				  g_strdup (new_charset), g_free);

	gtk_object_set_data (GTK_OBJECT (menu), "activated_item", item);
	select_item (GTK_MENU_SHELL (menu), item);
	return TRUE;
}

static void
other_charset_callback (char *new_charset, gpointer data)
{
	char **out = data;

	*out = new_charset;
}

static void
activate_other (GtkWidget *item, gpointer menu)
{
	GtkWidget *window, *dialog;
	char *old_charset, *new_charset;

	window = gtk_widget_get_ancestor (item, GTK_TYPE_WINDOW);
	old_charset = gtk_object_get_data (GTK_OBJECT (menu), "other_charset");
	dialog = gnome_request_dialog (FALSE,
				       _("Enter the character set to use"),
				       old_charset, 0, other_charset_callback,
				       &new_charset, GTK_WINDOW (window));
	gnome_dialog_run_and_close (GNOME_DIALOG (dialog));

	if (new_charset) {
		if (add_other_charset (menu, item, new_charset))
			return;
		g_free (new_charset);
	}

	/* Revert to previous selection */
	select_item (GTK_MENU_SHELL (menu), gtk_object_get_data (GTK_OBJECT (menu), "activated_item"));
}

/**
 * e_charset_picker_new:
 * @default_charset: the default character set, or %NULL to use the
 * locale character set.
 *
 * This creates an option menu widget and fills it in with a selection
 * of available character sets. The @default_charset (or locale character
 * set if @default_charset is %NULL) will be listed first, and selected
 * by default (except that iso-8859-1 will always be used instead of
 * US-ASCII). Any other character sets of the same language class as
 * the default will be listed next, followed by the remaining character
 * sets, a separator, and an "Other..." menu item, which can be used to
 * select other charsets.
 *
 * Return value: an option menu widget, filled in and with signals
 * attached.
 */
GtkWidget *
e_charset_picker_new (const char *default_charset)
{
	GtkWidget *menu, *item;
	int def, i;
	char *locale_charset;

	g_get_charset (&locale_charset);
	if (!g_strcasecmp (locale_charset, "US-ASCII"))
		locale_charset = "iso-8859-1";

	if (!default_charset)
		default_charset = locale_charset;
	for (def = 0; def < num_charsets; def++) {
		if (!g_strcasecmp (charsets[def].name, default_charset))
			break;
	}

	menu = gtk_menu_new ();
	for (i = 0; i < num_charsets; i++) {
		item = add_charset (menu, &charsets[i], FALSE);
		if (i == def) {
			activate (item, menu);
			select_item (GTK_MENU_SHELL (menu), item);
		}
	}

	/* do the Unknown/Other section */
	gtk_menu_append (GTK_MENU (menu), gtk_menu_item_new ());

	if (def == num_charsets) {
		ECharset other = { NULL, E_CHARSET_UNKNOWN, NULL };

		/* Add an entry for @default_charset */
		other.name = g_strdup (default_charset);
		item = add_charset (menu, &other, TRUE);
		activate (item, menu);
		select_item (GTK_MENU_SHELL (menu), item);
		gtk_object_set_data_full (GTK_OBJECT (menu), "other_charset",
					  g_strdup (default_charset), g_free);
		def++;
	}

	item = gtk_menu_item_new_with_label (_("Other..."));
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (activate_other), menu);
	gtk_menu_append (GTK_MENU (menu), item);

	gtk_widget_show_all (menu);
	return menu;
}

/**
 * e_charset_picker_get_charset:
 * @menu: a character set menu from e_charset_picker_new()
 *
 * Return value: the currently-selected character set in @picker,
 * which must be freed with g_free().
 **/
char *
e_charset_picker_get_charset (GtkWidget *menu)
{
	GtkWidget *item;
	char *charset;

	g_return_val_if_fail (GTK_IS_MENU (menu), NULL);

	item = gtk_menu_get_active (GTK_MENU (menu));
	charset = gtk_object_get_data (GTK_OBJECT (item), "charset");

	return g_strdup (charset);
}

/**
 * e_charset_picker_dialog:
 * @title: title for the dialog box
 * @prompt: prompt string for the dialog box
 * @default_charset: as for e_charset_picker_new()
 * @parent: a parent window for the dialog box, or %NULL
 *
 * This creates a new dialog box with the given @title and @prompt and
 * a character set picker menu. It then runs the dialog and returns
 * the selected character set, or %NULL if the user clicked "Cancel".
 *
 * Return value: the selected character set (which must be freed with
 * g_free()), or %NULL.
 **/
char *
e_charset_picker_dialog (const char *title, const char *prompt,
			 const char *default_charset, GtkWindow *parent)
{
	GnomeDialog *dialog;
	GtkWidget *label, *omenu, *picker;
	int button;
	char *charset;

	dialog = GNOME_DIALOG (gnome_dialog_new (title, GNOME_STOCK_BUTTON_OK,
						 GNOME_STOCK_BUTTON_CANCEL,
						 NULL));
	if (parent)
		gnome_dialog_set_parent (dialog, parent);

	label = gtk_label_new (prompt);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
	picker = e_charset_picker_new (default_charset);
	omenu = gtk_option_menu_new ();
	gtk_option_menu_set_menu (GTK_OPTION_MENU (omenu), picker);

	gtk_container_set_border_width (GTK_CONTAINER (dialog->vbox), 4);
	gtk_box_pack_start (GTK_BOX (dialog->vbox), label, FALSE, FALSE, 4);
	gtk_box_pack_start (GTK_BOX (dialog->vbox), omenu, FALSE, FALSE, 4);

	gtk_widget_show_all (GTK_WIDGET (dialog));
	button = gnome_dialog_run (dialog);

	if (button == 0)
		charset = e_charset_picker_get_charset (picker);
	else
		charset = NULL;
	gnome_dialog_close (dialog);

	return charset;
}

/**
 * e_charset_picker_bonobo_ui_populate:
 * @uic: Bonobo UI Component
 * @path: menu path
 * @default_charset: the default character set, or %NULL to use the
 * locale character set.
 * @cb: Callback function
 * @user_data: data to be passed to the callback.
 *
 * This creates a Bonobo UI menu and fills it in with a selection
 * of available character sets. The @default_charset (or locale character
 * set if @default_charset is %NULL) will be listed first, and selected
 * by default (except that iso-8859-1 will always be used instead of
 * US-ASCII). Any other character sets of the same language class as
 * the default will be listed next, followed by the remaining character
 * sets.
 **/
void
e_charset_picker_bonobo_ui_populate (BonoboUIComponent *uic, const char *path,
				     const char *default_charset,
				     BonoboUIListenerFn cb, gpointer user_data)
{
	char *locale_charset, *encoded_label, *label;
	GString *menuitems;
	int def, i;
	
	g_get_charset (&locale_charset);
	if (!g_strcasecmp (locale_charset, "US-ASCII"))
		locale_charset = "iso-8859-1";
	
	if (!default_charset)
		default_charset = locale_charset;
	for (def = 0; def < num_charsets; def++) {
		if (!g_strcasecmp (charsets[def].name, default_charset))
			break;
	}
	
	label = g_strdup (_("Character Encoding"));
	encoded_label = bonobo_ui_util_encode_str (label);
	menuitems = g_string_new ("");
	g_string_sprintf (menuitems, "<submenu name=\"ECharsetPicker\" label=\"%s\">\n", encoded_label);
	g_free (encoded_label);
	g_free (label);
	
	for (i = 0; i < num_charsets; i++) {
		char *command, *label, *encoded_label;
		
		if (charsets[i].subclass) {
			label = g_strdup_printf ("%s, %s (%s)",
						 _(classnames[charsets[i].class]),
						 _(charsets[i].subclass),
						 charsets[i].name);
		} else {
			label = g_strdup_printf ("%s (%s)",
						 _(classnames[charsets[i].class]),
						 charsets[i].name);
		}
		
		encoded_label = bonobo_ui_util_encode_str (label);
		g_free (label);
		
		command = g_strdup_printf ("<cmd name=\"Charset-%s\" label=\"%s\" type=\"radio\""
					   " group=\"charset_picker\" state=\"%d\"/>\n",
					   charsets[i].name, encoded_label, i == def);
		
		bonobo_ui_component_set (uic, "/commands", command, NULL);
		g_free (command);
		
		g_string_sprintfa (menuitems, "  <menuitem name=\"Charset-%s\" verb=\"\"/>\n",
				   charsets[i].name);
		
		g_free (encoded_label);
		
		label = g_strdup_printf ("Charset-%s", charsets[i].name);
		bonobo_ui_component_add_listener (uic, label, cb, user_data);
		g_free (label);
	}
	
	if (def == num_charsets) {
		char *command, *label, *encoded_label;
		
		label = g_strdup_printf ("%s (%s)", _("Unknown"), default_charset);
		encoded_label = bonobo_ui_util_encode_str (label);
		g_free (label);
		
		command = g_strdup_printf ("<cmd name=\"Charset-%s\" label=\"%s\" type=\"radio\""
					   " group=\"charset_picker\" state=\"1\"/>\n",
					   default_charset, encoded_label);
		
		bonobo_ui_component_set (uic, "/commands", command, NULL);
		g_free (command);
		
		g_string_append (menuitems, "  <separator/>\n");
		g_string_sprintfa (menuitems, "  <menuitem name=\"Charset-%s\" verb=\"\"/>\n",
				   default_charset);
		
		g_free (encoded_label);
		
		label = g_strdup_printf ("Charset-%s", default_charset);
		bonobo_ui_component_add_listener (uic, label, cb, user_data);
		g_free (label);
	}
	
	g_string_append (menuitems, "</submenu>\n");
	
	bonobo_ui_component_set (uic, path, menuitems->str, NULL);
	g_string_free (menuitems, TRUE);
}
