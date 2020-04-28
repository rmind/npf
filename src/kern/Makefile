#
# Standalone NPF makefile: the kernel component as a library.
# This file is in the Public Domain.
#

SYSNAME:=	$(shell uname -s)
SYSARCH:=	$(shell uname -m)

CFLAGS+=	-std=c99 -O2 -g -Wall -Wextra -Werror
CFLAGS+=	-D_POSIX_C_SOURCE=200809L
CFLAGS+=	-D_GNU_SOURCE -D_DEFAULT_SOURCE
CFLAGS+=	-I stand -D_NPF_STANDALONE -D__RCSID\(x\)=
CFLAGS+=	-Wno-unused-local-typedefs -Wno-unused-parameter

#
# Extended warning flags.
#
CFLAGS+=	-Wno-unknown-warning-option # gcc vs clang
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
CFLAGS+=	-Wmissing-declarations -Wredundant-decls -Wnested-externs
CFLAGS+=	-Wshadow -Wcast-qual -Wcast-align -Wwrite-strings
CFLAGS+=	-Wold-style-definition
CFLAGS+=	-Wsuggest-attribute=noreturn #-Wjump-misses-init
CFLAGS+=	-Wduplicated-cond -Wmisleading-indentation -Wnull-dereference
CFLAGS+=	-Wduplicated-branches -Wrestrict

#
# WARNING: All symbols must be hidden by default to not conflict with
# the libnpf(3) library.  The debug version would, however, conflict.
#
ifeq ($(DEBUG),1)
CFLAGS+=	-Og -DDEBUG -fno-omit-frame-pointer -D_NPF_TESTING
ifeq ($(SYSARCH),x86_64)
ifeq ($(CC),gcc) # XXX clang
CFLAGS+=	-fsanitize=address -fsanitize=undefined
LDFLAGS+=	-fsanitize=address -fsanitize=undefined
endif
endif
OBJDIR=		debug
else
CFLAGS+=	-fvisibility=hidden
CFLAGS+=	-DNDEBUG
OBJDIR=		build
endif

CFLAGS+=	-pthread
LDFLAGS+=	-lpthread -lnv -lqsbr -lthmap -llpm -lcdb -lbpfjit
ifeq ($(SYSNAME),Linux)
LDFLAGS+=	-ljemalloc
endif

#
# Objects to compile
#

OBJS=		$(shell awk '/^file/ { print $$2 }' files.npf | \
		    sed 's:\(.*\)net/npf/\(.*\).c:\2.o:g' | \
		    egrep -v '^(npf_os|npf_ext_log|npf_ifaddr|if_npflog|lpm)')

OBJS+=		stand/npfkern.o stand/bpf_filter.o
OBJS+=		stand/murmurhash.o stand/tls_pth.o
OBJS+=		stand/ebr_wrappers.o

#
# Flags for the library target
#

LIB=		libnpfkern
ifeq ($(DEBUG),1)
LIB=		libnpfkern-dbg
endif

INCS=		npf.h npfkern.h
MANS3=		npfkern.3
MANS7=		npf-params.7

$(LIB).la:	LDFLAGS+=	-rpath $(LIBDIR) -version-info 1:0:0
install/%.la:	ILIBDIR=	$(DESTDIR)/$(LIBDIR)
install:	IINCDIR=	$(DESTDIR)/$(INCDIR)
install:	IMAN3DIR=	$(DESTDIR)/$(MANDIR)/man3/
install:	IMAN7DIR=	$(DESTDIR)/$(MANDIR)/man7/

#
# Targets
#

lib: $(LIB).la

$(OBJDIR)/%.lo: %.c
	libtool --mode=compile --tag CC $(CC) $(CFLAGS) -c -o $@ $<

$(LIB).la: $(addprefix $(OBJDIR)/,$(shell echo $(OBJS) | sed 's/\.o/\.lo/g'))
	libtool --mode=link --tag CC $(CC) -o $@ $^ $(LDFLAGS)

install/%.la: %.la
	mkdir -p $(ILIBDIR)
	libtool --mode=install install -c $(notdir $@) $(ILIBDIR)/$(notdir $@)

install: $(addprefix install/,$(LIB).la)
	libtool --mode=finish $(LIBDIR)
	mkdir -p $(IINCDIR)/net && install -c $(INCS) $(IINCDIR)/net
	mkdir -p $(IMAN3DIR) && install -c $(MANS3) $(IMAN3DIR)
	mkdir -p $(IMAN7DIR) && install -c $(MANS7) $(IMAN7DIR)

clean:
	libtool --mode=clean rm
	rm -rf .libs $(OBJDIR) $(LIB).la

.PHONY: all lib install clean
