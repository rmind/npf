#
# Standalone NPF makefile
# This file is in the Public Domain.
#

CFLAGS=		-std=gnu99 -O2 -flto -Wall -Wextra #-Werror
CFLAGS+=        -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
CFLAGS+=	-fvisibility=hidden -I stand -D_NPF_STANDALONE
CFLAGS+=	-D__KERNEL_RCSID\(x,y\)=

CFLAGS+=	-Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
CFLAGS+=	-Wno-unused

ifeq ($(DEBUG),1)
CFLAGS+=	-g -O1 -DDEBUG -fno-omit-frame-pointer
else
CFLAGS+=	-DNDEBUG
endif

LDFLAGS=	-flto -lpthread -lpcap -lprop

SYSNAME:=	$(shell uname -s)

ifeq ($(SYSNAME),Linux)
CFLAGS+=	-D_POSIX_C_SOURCE=200809L -D_BSD_SOURCE
endif

OBJS=		$(shell grep ^file files.npf | cut -f2 | \
		    sed 's/\(.*\)net\/npf\/\(.*\).c/\2.o/g' | \
		    grep -v npf_mbuf | grep -v npf_ext_ | grep -v if_npflog)

OBJS+=		stand/bpf.o
OBJS+=		stand/rb.o stand/ptree.o
OBJS+=		stand/murmurhash.o stand/subr_hash.o

all: $(OBJS)

test: $(OBJS) main.o
	# $(CC) $^ -o npf $(LDFLAGS)

clean:
	rm -f *.o stand/*.o
