SYSARCH:=	$(shell uname -m)

ifeq ($(SYSARCH),x86_64)
NJOBS:=		$(shell getconf _NPROCESSORS_ONLN)
MKFLAGS=	-j $(NJOBS)
else
MKFLAGS=
endif

all:
	make $(MKFLAGS) -C kern lib
	make $(MKFLAGS) -C libnpf lib

install:
	make -C kern install
	make -C libnpf install

clean:
	make -C kern clean
	make -C libnpf clean
	make -C npfctl clean

.PHONY: all install clean
