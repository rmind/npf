all:
	make -C kern lib
	make -C libnpf lib

install:
	make -C kern install
	make -C libnpf install

clean:
	make -C kern clean
	make -C libnpf clean
	make -C npfctl clean

.PHONY: all install clean
