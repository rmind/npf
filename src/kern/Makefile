#
# Standalone NPF makefile: the kernel component as a library.
# This file is in the Public Domain.
#

CFLAGS+=	-std=c99 -O2 -g -Wall -Wextra -Werror
CFLAGS+=	-D_POSIX_C_SOURCE=200809L
CFLAGS+=	-D_GNU_SOURCE -D_DEFAULT_SOURCE
CFLAGS+=	-I stand -D_NPF_STANDALONE -D__RCSID\(x\)=
CFLAGS+=	-Wno-unused-local-typedefs -Wno-unused-parameter

SYSNAME:=	$(shell uname -s)

#
# Extended warning flags.
#
CFLAGS+=	-Wno-unknown-warning-option # gcc vs clang

CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
CFLAGS+=	-Wmissing-declarations -Wredundant-decls -Wnested-externs
CFLAGS+=	-Wshadow -Wcast-qual -Wcast-align -Wwrite-strings
CFLAGS+=	-Wold-style-definition
CFLAGS+=	-Wsuggest-attribute=noreturn #-Wjump-misses-init

# New GCC 6/7 flags:
#CFLAGS+=	-Wduplicated-cond -Wmisleading-indentation -Wnull-dereference
#CFLAGS+=	-Wduplicated-branches -Wrestrict

#
# WARNING: All symbols must be hidden by default to not conflict with
# the libnpf(3) library.  The debug version would, however, conflict.
#
ifeq ($(DEBUG),1)
CFLAGS+=	-Og -DDEBUG -fno-omit-frame-pointer
CFLAGS+=	-D_NPF_TESTING
OBJDIR=		debug
else
CFLAGS+=	-fvisibility=hidden
CFLAGS+=	-DNDEBUG
OBJDIR=		build
endif

LDFLAGS+=	-lpthread -lnv -lpcap -lqsbr -lthmap -llpm -lcdb -lbpfjit
ifeq ($(SYSNAME),Linux)
LDFLAGS+=	-ljemalloc
endif

#
# Objects to compile
#

OBJS=		$(shell grep ^file files.npf | cut -f2 | \
		    sed 's/\(.*\)net\/npf\/\(.*\).c/\2.o/g' | \
		    grep -v npf_os | grep -v npf_alg_ | \
		    grep -v npf_ext_ | grep -v npf_ifaddr | \
		    grep -v if_npflog | grep -v lpm)

OBJS+=		stand/bpf_filter.o
OBJS+=		stand/murmurhash.o stand/misc.o

#
# Flags for the library target
#

LIB=		libnpfkern
ifeq ($(DEBUG),1)
LIB=		libnpfkern-dbg
endif

INCS=		npf.h npfkern.h
MANS=		npfkern.3

$(LIB).la:	LDFLAGS+=	-rpath $(LIBDIR) -version-info 1:0:0
install/%.la:	ILIBDIR=	$(DESTDIR)/$(LIBDIR)
install:	IINCDIR=	$(DESTDIR)/$(INCDIR)
install:	IMANDIR=	$(DESTDIR)/$(MANDIR)/man3/

#
# Targets
#

lib: $(LIB).la

$(OBJDIR)/%.lo: %.c
	libtool --mode=compile --tag CC $(CC) $(CFLAGS) -c -o $@ $<

$(LIB).la: $(addprefix $(OBJDIR)/,$(shell echo $(OBJS) | sed 's/\.o/\.lo/g'))
	libtool --mode=link --tag CC $(CC) $(LDFLAGS) -o $@ $^

install/%.la: %.la
	mkdir -p $(ILIBDIR)
	libtool --mode=install install -c $(notdir $@) $(ILIBDIR)/$(notdir $@)

install: $(addprefix install/,$(LIB).la)
	libtool --mode=finish $(LIBDIR)
	mkdir -p $(IINCDIR)/net && install -c $(INCS) $(IINCDIR)/net
	mkdir -p $(IMANDIR) && install -c $(MANS) $(IMANDIR)

clean:
	libtool --mode=clean rm
	rm -rf .libs $(OBJDIR) $(LIB).la

.PHONY: all lib install clean
