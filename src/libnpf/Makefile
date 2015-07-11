#
# Standalone NPF makefile: user library to configure NPF.
# This file is in the Public Domain.
#

CFLAGS=		-std=gnu99 -O2 -g -Wall -Wextra -Werror
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
CFLAGS+=	-I . -I ../kern/stand -D__KERNEL_RCSID\(x,y\)=
CFLAGS+=	-D_NPF_STANDALONE

CFLAGS+=	-Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
CFLAGS+=	-Wno-unused

ifeq ($(DEBUG),1)
CFLAGS+=	-O1 -DDEBUG -fno-omit-frame-pointer
else
CFLAGS+=	-DNDEBUG
endif

LDFLAGS=	-lprop

#
# System specific flags
#

SYSNAME:=	$(shell uname -s)

ifeq ($(SYSNAME),Linux)
CFLAGS+=	-D_POSIX_C_SOURCE=200809L
CFLAGS+=	-D_BSD_SOURCE -D__FAVOR_BSD=1
endif

#
# Objects to compile
#

ILIBDIR=	$(DESTDIR)/$(LIBDIR)
IINCDIR=	$(DESTDIR)/$(INCDIR)

OBJS=		npf.o

#
# Flags for the library target
#

LIB=		libnpf

INCS=		npf.h
MANS=		libnpf.3

$(LIB).la:	LDFLAGS+=	-rpath $(LIBDIR)
install/%.la:	ILIBDIR=	$(DESTDIR)/$(LIBDIR)
install:	IINCDIR=	$(DESTDIR)/$(INCDIR)
install:	IMANDIR=	$(DESTDIR)/$(MANDIR)/man3/

#
# Targets
#

lib: $(LIB).la

%.lo: %.c
	libtool --mode=compile --tag CC $(CC) $(CFLAGS) -c $<

$(LIB).la: $(shell echo $(OBJS) | sed 's/\.o/\.lo/g')
	libtool --mode=link --tag CC $(CC) $(LDFLAGS) -o $@ $(notdir $^)

install/%.la: %.la
	mkdir -p $(ILIBDIR)
	libtool --mode=install install -c $(notdir $@) $(ILIBDIR)/$(notdir $@)

install: $(addprefix install/,$(LIB).la)
	libtool --mode=finish $(LIBDIR)
	mkdir -p $(IINCDIR) && install -c $(INCS) $(IINCDIR)
	mkdir -p $(IMANDIR) && install -c $(MANS) $(IMANDIR)

clean:
	libtool --mode=clean rm
	@ rm -rf .libs *.o *.lo *.la
