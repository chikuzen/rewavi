SRCS = rewavi.c resilence.c common.c
OBJS = $(SRCS:%.c=%.o)
TARGET = rewavi.exe resilence.exe

CC = $(CROSS)gcc
CFLAGS = -Wall -std=gnu99 -I.
OCFLAGS = -Os -mfpmath=sse -msse -ffast-math
XCFLAGS =
LDFLAGS = -L.
SFLAGS = -Wl,-s
XLDFLAGS =
LIBS = -lavifil32

.PHONY: all clean

all: $(TARGET)

rewavi.exe: rewavi.o common.o
	$(CC) -o $@ $(SFLAGS) $(LDFLAGS) $(XLDFLAGS) $^ $(LIBS)

resilence.exe: resilence.o common.o
	$(CC) -o $@ $(SFLAGS) $(LDFLAGS) $(XLDFLAGS) $^

%.o: %.c common.h .depend
	$(CC) -c -o $@ $(CFLAGS) $(OCFLAGS) $(XCFLAGS) $<

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS), $(CC) $(SRC) $(CFLAGS) -MT $(SRC:%.c=%.o) -MM >> .depend;)

clean:
	$(RM) *.exe *.o .depend