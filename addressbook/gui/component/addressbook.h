#ifndef __ADDRESSBOOK_H__
#define __ADDRESSBOOK_H__

#include <bonobo/bonobo-control.h>
#include <e-util/e-config-listener.h>
#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-moniker-util.h>
#include <ebook/e-book.h>

/* use this instead of e_book_load_uri everywhere where you want the
   authentication to be handled for you. */
void       addressbook_load_uri             (EBook *book, const char *uri, EBookCallback cb, gpointer closure);
void       addressbook_load_default_book    (EBook *book, EBookCallback open_response, gpointer closure);

BonoboControl *addressbook_new_control  (void);

#endif /* __ADDRESSBOOK_H__ */
