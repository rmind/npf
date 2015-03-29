#
# Standalone NPF makefile: npfctl utility.
# This file is in the Public Domain.
#

BIN=		npfctl

CFLAGS=		-std=gnu99 -O2 -flto -g -Wall -Wextra -Werror
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
CFLAGS+=	-I. -D__RCSID\(x\)= -D__dead= -D__printflike\(x,y\)=

CFLAGS+=	-Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
CFLAGS+=	-Wno-unused

ifeq ($(DEBUG),1)
CFLAGS+=	-O1 -DDEBUG -fno-omit-frame-pointer
else
CFLAGS+=	-DNDEBUG
LDFLAGS=	-flto
endif

LDFLAGS=	-lnpf -lcdb -lprop -lcrypto -lpcap

#
# System specific flags
#

SYSNAME:=	$(shell uname -s)

ifeq ($(SYSNAME),Linux)
CFLAGS+=	-Dgetprogname\(\)=\"$(BIN)\"
CFLAGS+=	-D_POSIX_C_SOURCE=200809L
CFLAGS+=	-D_BSD_SOURCE -D__FAVOR_BSD=1

LDFLAGS+=	-ldl
endif

#
# Objects to compile and binary to produce.
#

OBJS=		npfctl.o npf_var.o npf_data.o npf_build.o npf_extmod.o
OBJS+=		npf_bpf_comp.o npf_show.o
OBJS+=		npf_parse.o npf_scan.o

MAN5=		npf.conf.5
MAN8=		npfctl.8

install:	IBINDIR=	$(DESTDIR)/$(BINDIR)
install:	IMAN5DIR=	$(DESTDIR)/$(MANDIR)/man5/
install:	IMAN8DIR=	$(DESTDIR)/$(MANDIR)/man8/

#
# Targets
#

all: $(OBJS)
	$(CC) $^ -o $(BIN) $(LDFLAGS)

npf_parse.c:
	$(YACC) -d -o npf_parse.c npf_parse.y

npf_scan.o:
	$(LEX) -o npf_scan.c npf_scan.l
	$(CC) $(CFLAGS) -Wno-sign-compare -c npf_scan.c -o npf_scan.o

install:
	mkdir -p $(IBINDIR) && install -c $(BIN) $(IBINDIR)
	mkdir -p $(IMAN5DIR) && install -c $(MAN5) $(IMAN5DIR)
	mkdir -p $(IMAN8DIR) && install -c $(MAN8) $(IMAN8DIR)

clean:
	rm -f $(BIN) *.o npf_parse.c npf_scan.c

.PHONY: all install clean
