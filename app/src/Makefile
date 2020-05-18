#
# NPF + DPDK demo application.
# This file is in the Public Domain.
#

BIN=		npf_router

SYSNAME:=	$(shell uname -s)

#
# Common C compiler flags.
#
CFLAGS+=	-std=gnu11 -O2 -g -Wall -Wextra -Werror

#
# Extended warning flags.
#
CFLAGS+=	-Wno-unknown-warning-option # gcc vs clang

CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
CFLAGS+=	-Wmissing-declarations -Wredundant-decls -Wnested-externs
CFLAGS+=	-Wshadow -Wcast-qual -Wcast-align -Wwrite-strings
CFLAGS+=	-Wold-style-definition
CFLAGS+=	-Wsuggest-attribute=noreturn -Wjump-misses-init
CFLAGS+=	-Wduplicated-cond -Wmisleading-indentation -Wnull-dereference
CFLAGS+=	-Wduplicated-branches -Wrestrict

#
# System-specific or compiler-specific flags.
#

ifeq ($(SYSNAME),Linux)
CFLAGS+=	-D_POSIX_C_SOURCE=200809L
CFLAGS+=	-D_GNU_SOURCE -D_DEFAULT_SOURCE
CFLAGS+=	-Dgetprogname\(\)=\"$(BIN)\"
endif

#
# Standard vs debug build flags.
#
ifeq ($(DEBUG),1)
CFLAGS+=	-O0 -DDEBUG -fno-omit-frame-pointer
CFLAGS+=	-fsanitize=address -fsanitize=undefined
LDFLAGS+=	-fsanitize=address -fsanitize=undefined
else
CFLAGS+=	-DNDEBUG
endif

#
# Dependencies: compiler flags and libraries to link.
#
CFLAGS+=	-march=native -I/usr/include/dpdk
LDFLAGS+=	-lpthread -ldpdk -lrte_mbuf -lrte_net -lrte_ethdev
LDFLAGS+=	-lrte_pmd_tap -lrte_pmd_virtio
LDFLAGS+=	-lthmap -llpm
ifeq ($(DEBUG),1)
LDFLAGS+=	-lnpfkern-dbg
else
LDFLAGS+=	-lnpfkern
endif
LDFLAGS+=	-lnpf # XXX

#
# Objects to compile and binary to produce.
#

OBJS=		npf_dpdk_ops.o if_dpdk.o
OBJS+=		npf_router.o worker.o arp.o route.o config.o

#
# Targets
#

install:	IBINDIR=	$(DESTDIR)/$(BINDIR)

all: $(OBJS)
	$(CC) $^ -o $(BIN) $(LDFLAGS)

install:
	mkdir -p $(IBINDIR) && install -c $(BIN) $(IBINDIR)

clean:
	rm -f $(BIN) *.o

.PHONY: all install clean
