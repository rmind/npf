NJOBS:=	$(shell getconf _NPROCESSORS_ONLN)

all:
	make -j $(NJOBS) -C kern lib
	make -j $(NJOBS) -C libnpf lib

install:
	make -C kern install
	make -C libnpf install

clean:
	make -C kern clean
	make -C libnpf clean
	make -C npfctl clean

.PHONY: all install clean
