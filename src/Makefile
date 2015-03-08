all:
	make -C kern lib
	make -C npftest

install:
	make -C kern install

clean:
	make -C kern clean
	make -C npfctl clean

.PHONY: all install clean
