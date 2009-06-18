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
 *		Iain Holmes  <iain@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef EVOLUTION_INTELLIGENT_IMPORTER_H
#define EVOLUTION_INTELLIGENT_IMPORTER_H

#include <glib.h>
#include <bonobo/bonobo-object.h>
#include <importer/GNOME_Evolution_Importer.h>

G_BEGIN_DECLS

#define EVOLUTION_TYPE_INTELLIGENT_IMPORTER            (evolution_intelligent_importer_get_type ())
#define EVOLUTION_INTELLIGENT_IMPORTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVOLUTION_TYPE_INTELLIGENT_IMPORTER, EvolutionIntelligentImporter))
#define EVOLUTION_INTELLIGENT_IMPORTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EVOLUTION_TYPE_INTELLIGENT_IMPORTER, EvolutionIntelligentImporterClass))
#define EVOLUTION_IS_INTELLIGENT_IMPORTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVOLUTION_TYPE_INTELLIGENT_IMPORTER))
#define EVOLUTION_IS_INTELLIGENT_IMPORTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EVOLUTION_TYPE_INTELLIGENT_IMPORTER))

typedef struct _EvolutionIntelligentImporter EvolutionIntelligentImporter;
typedef struct _EvolutionIntelligentImporterPrivate EvolutionIntelligentImporterPrivate;
typedef struct _EvolutionIntelligentImporterClass EvolutionIntelligentImporterClass;

typedef gboolean (* EvolutionIntelligentImporterCanImportFn) (EvolutionIntelligentImporter *ii,
							      gpointer closure);
typedef void (* EvolutionIntelligentImporterImportDataFn) (EvolutionIntelligentImporter *ii,
							   gpointer closure);

struct _EvolutionIntelligentImporter {
  BonoboObject parent;

  EvolutionIntelligentImporterPrivate *priv;
};

struct _EvolutionIntelligentImporterClass {
  BonoboObjectClass parent_class;

  POA_GNOME_Evolution_IntelligentImporter__epv epv;
};

GType evolution_intelligent_importer_get_type (void);

EvolutionIntelligentImporter *evolution_intelligent_importer_new (EvolutionIntelligentImporterCanImportFn can_import_fn,
								  EvolutionIntelligentImporterImportDataFn import_data_fn,
								  const gchar *importername,
								  const gchar *message,
								  gpointer closure);

G_END_DECLS

#endif
