/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* init.c
 *
 * Copyright (C) 2003 Ximian, Inc.
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
 * Author: Gilbert Fang <gilbert.fang@sun.com> Sun Microsystem Inc., 2003
 *
 */

#include <config.h>

#include <atk/atkregistry.h>

#include <stdio.h>

/* Static functions */

static gboolean initialized = FALSE;

extern void gnome_accessibility_module_init     (void);
extern void gnome_accessibility_module_shutdown (void);

void
e_a11y_init (void)
{
	if (initialized)
		return;

	/* atk registry stuff here, need implementation*/

	initialized = TRUE;

	fprintf (stderr, "Evolution Accessibility Support Extension Module initialized\n");
}

void
gnome_accessibility_module_init (void)
{
	e_a11y_init();
}

void
gnome_accessibility_module_shutdown (void)
{
	if (!initialized)
		return;
	
	/* atk un-registry stuff here need implementation*/

	initialized = FALSE;
	fprintf (stderr, "Evolution Accessibilty Support Extension Module shutdown\n");

}

int
gtk_module_init (gint *argc, char** argv[])
{
  e_a11y_init ();

  return 0;
}
