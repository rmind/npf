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
	make $(MKFLAGS) -C lib

install:
	make -C kern install
	make -C libnpf install
	make -C lib install

clean:
	make -C kern clean
	make -C libnpf clean
	make -C lib clean
	make -C npfctl clean

.PHONY: all install clean
