
#ifndef _EM_FOLDER_VIEW_H
#define _EM_FOLDER_VIEW_H

struct _MessageList;
struct _EMFormatHTMLDisplay;
struct _CamelFolder;
struct _CamelMedium;

typedef struct _EMFolderView EMFolderView;
typedef struct _EMFolderViewClass EMFolderViewClass;

struct _EMFolderView {
	GtkVBox parent;

	struct _MessageList *list;
	struct _EMFormatHTMLDisplay *preview;

	struct _CamelFolder *folder;
	char *folder_uri;

	struct _EMFolderViewPrivate *priv;
};

struct _EMFolderViewClass {
	GtkVBoxClass parent_class;

	void (*set_folder)(EMFolderView *emfv, const char *uri);
	void (*set_message)(EMFolderView *emfv, const char *uid);
};

GType em_folder_view_get_type(void);

/* how do you hook into uicontainer ??? */

GtkWidget *em_folder_view_new(void);

void em_folder_view_set_folder(EMFolderView *emfv, const char *uri);
void em_folder_view_set_message(EMFolderView *emfv, const char *uid);

int em_folder_view_print(EMFolderView *emfv, int preview);

#endif /* ! _EM_FOLDER_VIEW_H */
