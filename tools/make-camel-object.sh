#!/bin/bash

test -r $1 || { echo "'$1' is a bad input file" 1>&2 ; exit 1 }

cat $1 |sed -e 's,GTK_CHECK_,CAMEL_CHECK_,g' -e 's,GtkType,CamelType,g' -e 's,Standard Gtk,Standard Camel,g' \
    -e 's,gtk_type_new,camel_object_new,g' -e 's,GTK_OBJECT (so)->klass,CAMEL_OBJECT_GET_CLASS(so),g' \
    -e 's,gtk_type_class,camel_type_get_global_classfuncs,g' -e 's,GtkObject,CamelObject,g' \
    -e 's,gtk_object_,camel_object_,g' -e 's,GTK_OBJECT_TYPE,CAMEL_OBJECT_GET_TYPE,g' \
    -e 's,GTK_OBJECT,CAMEL_OBJECT,g' -e 's,gtk_type_name,camel_type_to_name,g' \
    -e 's,_type = 0,_type = CAMEL_INVALID_TYPE,g' -e 's,!\([a-zA-Z_]*\)_type,\1_type == CAMEL_INVALID_TYPE,g' \
    -e 's,gtk_type_unique,camel_type_register,g' -e 's,GTK_OBJECT(so)->klass,CAMEL_OBJECT_GET_CLASS(so),g' \
    -e 's,GtkClassInitFunc,CamelObjectClassInitFunc,g' > $1.new
mv -i $1 $1.old
mv $1.new $1
