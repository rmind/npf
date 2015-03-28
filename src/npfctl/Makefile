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

LDFLAGS=	-lnpf -lcdb -lprop -lcrypto -lpcap -lutil -ly

#
# System specific flags
#

SYSNAME:=	$(shell uname -s)

ifeq ($(SYSNAME),Linux)
CFLAGS+=	-Dgetprogname\(\)=\"$(BIN)\"
CFLAGS+=	-D_POSIX_C_SOURCE=200809L
CFLAGS+=	-D_BSD_SOURCE -D__FAVOR_BSD=1
endif

#
# Objects to compile and binary to produce.
#

OBJS=		npfctl.o npf_var.o npf_data.o npf_build.o npf_extmod.o
OBJS+=		npf_bpf_comp.o npf_show.o
OBJS+=		npf_parse.o npf_scan.o

MANS=		npfctl.8 npf.conf.5

#
# Targets
#

all: $(OBJS)
	$(CC) $^ -o $(BIN) $(LDFLAGS)

npf_parse.c:
	$(YACC) -d -o npf_parse.c npf_parse.y

npf_scan.c:
	$(LEX) -o npf_scan.c npf_scan.l

npf_scan.o:
	$(CC) $(CFLAGS) -Wno-sign-compare -c npf_scan.c -o npf_scan.o

install:
	#mkdir -p $(IMANDIR) && install -c $(MANS) $(IMANDIR)

clean:
	libtool --mode=clean rm
	rm -rf .libs *.{l,}o *.la stand/*.{l,}o stand/*.la

.PHONY: all install clean
