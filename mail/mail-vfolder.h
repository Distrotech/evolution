
#ifndef _MAIL_VFOLDER_H
#define _MAIL_VFOLDER_H

#include "Evolution.h"
#include "evolution-storage.h"
#include "evolution-shell-component.h"

#include "camel/camel-folder.h"
#include "camel/camel-mime-message.h"
#include "filter/vfolder-rule.h"
#include "filter/filter-part.h"

void vfolder_create_storage (EvolutionShellComponent *shell_component);

CamelFolder *vfolder_uri_to_folder (const char *uri, CamelException *ex);
void vfolder_edit (void);
FilterPart *vfolder_create_part (const char *name);
FilterRule *vfolder_clone_rule (FilterRule *in);
void vfolder_gui_add_rule (VfolderRule *rule);
void vfolder_gui_add_from_message (CamelMimeMessage *msg, int flags, const char *source);
void vfolder_gui_add_from_mlist (CamelMimeMessage *msg, const char *mlist, const char *source);

/* for registering all open folders as potential vfolder sources */
void vfolder_register_source (CamelFolder *folder);

void vfolder_remove (const char *uri);

EvolutionStorage *mail_vfolder_get_vfolder_storage (void);

#endif
