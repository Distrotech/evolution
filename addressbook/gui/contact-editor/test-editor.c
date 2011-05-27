/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Chris Lahey <clahey@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <gtk/gtk.h>
#include "e-contact-editor.h"
#include "ebook/e-card.h"

#define TEST_VCARD \
"BEGIN:VCARD\n" \
"FN:Nat\n" \
"N:Friedman;Nat;D;Mr.\n" \
"BDAY:1977-08-06\n" \
"TEL;WORK:617 679 1984\n" \
"TEL;CELL:123 456 7890\n" \
"EMAIL;INTERNET:nat@nat.org\n" \
"EMAIL;INTERNET:nat@ximian.com\n" \
"ADR;WORK;POSTAL:P.O. Box 101;;;Any Town;CA;91921-1234;\n" \
"ADR;HOME;POSTAL;INTL:P.O. Box 202;;;Any Town 2;MI;12344-4321;USA\n" \
"END:VCARD\n"

static gchar *
read_file (gchar *name)
{
	gint  len;
	gchar buff[65536];
	gchar line[1024];
	FILE *f;

	f = fopen (name, "r");
	if (f == NULL)
		g_error ("Unable to open %s!\n", name);

	len  = 0;
	while (fgets (line, sizeof (line), f) != NULL) {
		strcpy (buff + len, line);
		len += strlen (line);
	}

	fclose (f);

	return g_strdup (buff);
}

/* Callback used when a contact editor is closed */
static void
editor_closed_cb (EContactEditor *ce, gpointer data)
{
	static gint count = 2;

	count--;
	g_object_unref (ce);

	if (count == 0)
		exit (0);
}

gint
main (gint argc, gchar *argv[])
{
	gchar *cardstr;
	EContactEditor *ce;

	gtk_init (&argc, &argv);

	cardstr = NULL;
	if (argc == 2)
		cardstr = read_file (argv[1]);

	if (cardstr == NULL)
		cardstr = TEST_VCARD;

	ce = e_contact_editor_new (
		NULL, e_card_new_with_default_charset (
		cardstr, "ISO-8859-1"), TRUE, FALSE);
	g_signal_connect (
		ce, "editor_closed",
		G_CALLBACK (editor_closed_cb), NULL);

	ce = e_contact_editor_new (
		NULL, e_card_new_with_default_charset (
		cardstr, "ISO-8859-1"), TRUE, FALSE);
	g_signal_connect (
		ce, "editor_closed",
		G_CALLBACK (editor_closed_cb), NULL);

	gtk_main ();

	/* Not reached. */
	return 0;
}
