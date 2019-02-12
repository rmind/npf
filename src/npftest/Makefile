#
# Standalone NPF makefile: npftest utility with the unit tests.
# This file is in the Public Domain.
#

BIN=		npftest

CFLAGS+=	-std=c99 -O2 -g -Wall -Wextra -Werror
CFLAGS+=	-D_POSIX_C_SOURCE=200809L
CFLAGS+=	-D_GNU_SOURCE -D_DEFAULT_SOURCE

CFLAGS+=	-I ../kern -I ../kern/stand -D__RCSID\(x\)=
CFLAGS+=	-D_NPF_STANDALONE

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
# Always debug mode, as this is the test utility.
#
CFLAGS+=	-Og -DDEBUG -fno-omit-frame-pointer
CFLAGS+=	-fsanitize=address
CFLAGS+=	-pthread

LDFLAGS+=	-fsanitize=address
LDFLAGS+=	-lpthread -lnv -lpcap -lcdb -lnpfkern-dbg

OBJS=		npftest.o npfstream.o

OBJS+=		libnpftest/npf_bpf_test.o
OBJS+=		libnpftest/npf_conn_test.o
OBJS+=		libnpftest/npf_nat_test.o
OBJS+=		libnpftest/npf_nbuf_test.o
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
