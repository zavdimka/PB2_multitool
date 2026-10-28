CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64
CPPFLAGS += -I../../common/files
LDLIBS += -pthread

SOURCES = multitoold.c scope_ring.c iio_scope.c remoteproc.c rpmsg_link.c openocd_bridge.c
OBJECTS = $(SOURCES:.c=.o)

all: multitoold multitool-openocd-remote

multitoold: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

multitool-openocd-remote: openocd_remote.o
	$(CC) $(LDFLAGS) -o $@ $<

check: multitoold
	./multitoold --self-test

clean:
	rm -f $(OBJECTS) openocd_remote.o multitoold multitool-openocd-remote

.PHONY: all check clean