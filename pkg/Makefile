#
# Makefile to build RPM/DEB packages.
# This is primarily for the developer use.
#

PROJ?=	libnpf

all:
	@ echo "targets"
	@ echo "	make rpm"
	@ echo "	make deb"
rpm:
	mkdir -p SOURCES && tar czpvf SOURCES/$(PROJ).tar.gz ../src
	rpmbuild -ba -v --define "_topdir ${PWD}" SPECS/$(PROJ).spec
	@ echo && printf "\x1B[32mRPM packages:\033[0m\n" && ls -1 RPMS/*

deb:
	cp -R ../src ./SOURCES
	dpkg-buildpackage -rfakeroot -us -uc -b
	@ echo && printf "\x1B[32mDEB packages:\033[0m\n" && ls -1 ../*.deb

clean:
	rm -rf BUILD BUILDROOT RPMS SOURCES SRPMS

.PHONY: all rpm deb clean
