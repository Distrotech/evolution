CFLAGS=-g -Wall -pipe
REALCFLAGS=$(shell glib-config --cflags) $(CFLAGS)

SRCS=calserv.c
OBJS=$(addsuffix .o, $(basename $(SRCS)))
LIBS=$(shell glib-config --libs)
all: calserv

calserv: $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) -c -o $@ $< $(REALCFLAGS)
