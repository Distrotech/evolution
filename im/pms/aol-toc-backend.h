#ifndef AOL_TOC_BACKEND_H
#define AOL_TOC_BACKEND_H

#include "e-messenger-backend.h"

#define AOL_TOC_BACKEND_TYPE             (e_messenger_backend_get_type())
#define AOL_TOC_BACKEND(obj)             (GTK_CHECK_CAST((obj), AOL_TOC_BACKEND_TYPE, AOLTOCBackend))
#define AOL_TOC_BACKEND_CLASS(klass)     (GTK_CHECK_CLASS_CAST((klass), AOL_TOC_BACKEND_TYPE, AOLTOCBackendClass))
#define AOL_TOC_IS_BACKEND(obj)          (GTK_CHECK_TYPE((obj), AOL_TOC_BACKEND_TYPE))
#define AOL_TOC_IS_BACKEND_CLASS(klass)  (GTK_CHECK_CLASS_TYPE((klass), AOL_TOC_BACKEND_TYPE))
#define AOL_TOC_BACKEND_GET_CLASS(obj)   (AOL_TOC_BACKEND_CLASS(GTK_OBJECT(obj)->klass))

typedef struct _AOLTOCBackend      AOLTOCBackend;
typedef struct _AOLTOCBackendPriv  AOLTOCBackendPriv;
typedef struct _AOLTOCBackendClass AOLTOCBackendClass;

struct _AOLTOCBackend {
	EMessengerBackend parent;

	AOLTOCBackendPriv *priv;
};

struct _AOLTOCBackendClass {
	EMessengerBackendClass parent_class;
};

GtkType aol_toc_backend_get_type(void);
EMessengerBackend *aol_toc_backend_new(void);

#endif /* AOL_TOC_BACKEND_H */
