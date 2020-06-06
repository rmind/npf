#
# Generic Makefile for NPF extensions (npfctl part).
# This file is in the Public Domain.
#

CFLAGS+=	-std=c99 -O2 -g -Wall -Wextra -Werror
CFLAGS+=	-D_POSIX_C_SOURCE=200809L
CFLAGS+=	-D_GNU_SOURCE -D_DEFAULT_SOURCE
CFLAGS+=	-I. -D__RCSID\(x\)=

CFLAGS+=	-I . -I ../../kern/stand -D__KERNEL_RCSID\(x,y\)=
CFLAGS+=	-I ../../libnpf -D_NPF_STANDALONE

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
# Standard vs debug build flags.
#
ifeq ($(DEBUG),1)
CFLAGS+=	-Og -DDEBUG -fno-omit-frame-pointer
CFLAGS+=	-fsanitize=address -fsanitize=undefined
LDFLAGS+=	-fsanitize=address -fsanitize=undefined
else
CFLAGS+=	-DNDEBUG
endif

#
# Linker flags.
#

LDFLAGS+=	-module
LDFLAGS+=	-lnpf

#
# Flags for the library target
#

$(LIB).la:	LDFLAGS+=	-rpath $(LIBDIR) -version-info $(LIBVER)
install/%.la:	ILIBDIR=	$(DESTDIR)/$(LIBDIR)/npf

#
# Targets
#

obj: $(OBJS)

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

clean:
	libtool --mode=clean rm
	@ rm -rf .libs *.o *.lo *.la

.PHONY: all obj lib install clean
