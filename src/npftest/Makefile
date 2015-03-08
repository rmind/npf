#
# Standalone NPF makefile: npftest utility with the unit tests.
# This file is in the Public Domain.
#

BIN=		npftest

CFLAGS=		-std=gnu99 -O2 -Wall -Wextra -Werror
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
CFLAGS+=	-I ../kern -I ../kern/stand -D_NPF_STANDALONE
CFLAGS+=	-D__RCSID\(x\)=

CFLAGS+=	-Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
CFLAGS+=	-Wno-unused

LDFLAGS=	-lpthread -lpcap -lcdb -lprop -lnpfkern

SYSNAME:=	$(shell uname -s)

ifeq ($(SYSNAME),Linux)
CFLAGS+=	-D_POSIX_C_SOURCE=200809L
CFLAGS+=	-D_BSD_SOURCE -D__FAVOR_BSD=1
endif

OBJS=		npftest.o npfstream.o

OBJS+=		libnpftest/npf_bpf_test.o
OBJS+=		libnpftest/npf_nat_test.o
#OBJS+=		libnpftest/npf_nbuf_test.o
OBJS+=		libnpftest/npf_perf_test.o
OBJS+=		libnpftest/npf_rule_test.o
OBJS+=		libnpftest/npf_state_test.o
OBJS+=		libnpftest/npf_table_test.o

OBJS+=		libnpftest/npf_mbuf_subr.o
OBJS+=		libnpftest/npf_test_subr.o

all: $(OBJS)
	$(CC) $^ -o $(BIN) $(LDFLAGS)

clean:
	rm -rf $(BIN) *.o libnpftest/*.o

.PHONY: all clean
