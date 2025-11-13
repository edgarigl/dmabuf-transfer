CC ?= gcc
CFLAGS = -Wall -O2
TARGETS = server client

LDLIBS += -lxengnttab

OBJS = memfd.o sk.o xen-dmabuf.o safeio.o

all: $(TARGETS)

server: server.o ${OBJS}

client: client.o ${OBJS}

clean:
	rm -f $(TARGETS) ${OBJS}
	rm -f server.o client.o

.PHONY: all clean
