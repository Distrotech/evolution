CFLAGS=-g -Wall -pipe
REALCFLAGS=$(shell glib-config --cflags) $(CFLAGS)

SRCS=calserv.c main.c commands.c auth.c
OBJS=$(addsuffix .o, $(basename $(SRCS)))
LIBS=$(shell glib-config --libs) $(REALCFLAGS)
all: calserv

calserv: $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) -c -o $@ $< $(REALCFLAGS)
