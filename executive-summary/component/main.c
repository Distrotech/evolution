/* 
 * main.c: The core of the executive summary component.
 *
 * Copyright (C) 2000 Ximian, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * published by the Free Software Foundation; either version 2 of the
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
 * Author: Iain Holmes  <iain@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <liboaf/liboaf.h>
#include <glade/glade.h>

#ifdef GTKHTML_HAVE_GCONF
#include <gconf/gconf.h>
#endif

#include "gal/widgets/e-gui-utils.h"
#include "gal/widgets/e-cursors.h"

#include <libgnomevfs/gnome-vfs.h>
#include "component-factory.h"

int
main (int argc,
      char **argv)
{
  CORBA_ORB orb;
  
  bindtextdomain (PACKAGE, EVOLUTION_LOCALEDIR);
  textdomain (PACKAGE);

  gnome_init_with_popt_table ("evolution-executive-summary", VERSION,
			      argc, argv, oaf_popt_options, 0, NULL);
  orb = oaf_init (argc, argv);

  gdk_rgb_init ();
  glade_gnome_init ();
  if (bonobo_init (orb, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL) == FALSE) {
    g_error (_("Executive summary component could not initialize Bonobo.\n"
	       "If there was a warning message about the "
	       "RootPOA, it probably means\nyou compiled "
	       "Bonobo against GOAD instead of OAF."));
  }

#ifdef GTKHTML_HAVE_GCONF
  gconf_init (argc, argv, NULL);
#endif

  e_cursors_init ();

  component_factory_init ();

  gnome_vfs_init ();
  bonobo_main ();

  return 0;
}
