LIBS=		libnpf.la
INCS=		npf.h

CFLAGS=		-std=gnu99 -O2 -W -Wextra -Werror -I.
CFLAGS+=	-DNDEBUG -D__KERNEL_RCSID\(x,y\)=

LDFLAGS=        -rpath $(LIBDIR)

ILIBDIR=	$(DESTDIR)/$(LIBDIR)
IINCDIR=	$(DESTDIR)/$(INCDIR)

libcdb_OBJS=	npf.lo

all: libcdb.la

%.lo: %.c
	libtool --mode=compile --tag CC $(CC) $(CFLAGS) -c $<

libcdb.la: $(libcdb_OBJS)
	libtool --mode=link --tag CC $(CC) $(LDFLAGS) -o $@ $^

install/%.la: %.la
	mkdir -p $(ILIBDIR)
	libtool --mode=install install -c $(notdir $@) $(ILIBDIR)/$(notdir $@)

install: $(addprefix install/,$(LIBS))
	libtool --mode=finish $(LIBDIR)
	mkdir -p $(IINCDIR)
	install -c $(INCS) $(IINCDIR)

clean:
	libtool --mode=clean rm
	@ rm -rf .libs *.{l,}o *.la
