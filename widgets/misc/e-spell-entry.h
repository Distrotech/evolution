/*
 * e-spell-entry.h
 *
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
 */

#ifndef E_SPELL_ENTRY_H
#define E_SPELL_ENTRY_H

#include <gtk/gtk.h>

#define E_TYPE_SPELL_ENTRY            (e_spell_entry_get_type())
#define E_SPELL_ENTRY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), E_TYPE_SPELL_ENTRY, ESpellEntry))
#define E_SPELL_ENTRY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), E_TYPE_SPELL_ENTRY, ESpellEntryClass))
#define E_IS_SPELL_ENTRY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), E_TYPE_SPELL_ENTRY))
#define E_IS_SPELL_ENTRY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), E_TYPE_SPELL_ENTRY))
#define E_SPELL_ENTRY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), E_TYPE_SPELL_ENTRY, ESpellEntryClass))

G_BEGIN_DECLS

typedef struct _ESpellEntry		ESpellEntry;
typedef struct _ESpellEntryClass	ESpellEntryClass;
typedef struct _ESpellEntryPrivate	ESpellEntryPrivate;

struct _ESpellEntry
{
	GtkEntry parent_object;

	ESpellEntryPrivate *priv;
};

struct _ESpellEntryClass
{
	GtkEntryClass parent_class;
};

GType		e_spell_entry_get_type			(void);
GtkWidget *	e_spell_entry_new			(void);
void		e_spell_entry_set_languages		(ESpellEntry *spell_entry,
							 GList *languages);
gboolean	e_spell_entry_get_checking_enabled	(ESpellEntry *spell_entry);
void		e_spell_entry_set_checking_enabled	(ESpellEntry *spell_entry,
							 gboolean enable_checking);

G_END_DECLS

#endif /* E_SPELL_ENTRY_H */
