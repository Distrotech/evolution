/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * gen-view-etspec
 * Copyright (C) 2004  Novell, Inc.
 * Author: Chris Toshok <toshok@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <libebook/e-contact.h>

int
main (int argc, char **argv)
{
	int i;
	FILE *fp;

	fp = fopen ("e-addressbook-view.etspec", "w");

	fprintf (fp, "<!-- generated file, do not edit -->\n");

	fprintf (fp, "<ETableSpecification draw-grid=\"true\" cursor-mode=\"line\">\n");

	for (i = 1; i <= E_CONTACT_LAST_SIMPLE_STRING; i ++) {
		fprintf (fp, "  <ETableColumn model_col= \"%d\" _title=\"%s\" expansion=\"1.0\" minimum_width=\"75\" resizable=\"true\" cell=\"string\" compare=\"string\"/>\n", i, e_contact_pretty_name (i));
	}

	fprintf (fp, 
		 "  <ETableState>\n"
		 "    <column source=\"1\"/>\n"
		 "    <column source=\"2\"/>\n"
		 "    <column source=\"8\"/>\n"
		 "    <column source=\"23\"/>\n"
		 "    <column source=\"37\"/>\n"
		 "    <grouping>\n"
		 "      <leaf column=\"0\" ascending=\"true\"/>\n"
		 "    </grouping>\n"
		 "  </ETableState>\n");

	fprintf (fp, "</ETableSpecification>\n");

	fclose (fp);

	return 0;
}
