all:
	make -C ext_log
	make -C ext_normalize
	make -C ext_rndblock
	make -C ext_ratelimit

install:
	make -C ext_log install
	make -C ext_normalize install
	make -C ext_rndblock install
	make -C ext_ratelimit install

clean:
	make -C ext_log clean
	make -C ext_normalize clean
	make -C ext_rndblock clean
	make -C ext_ratelimit clean

.PHONY: all install clean
