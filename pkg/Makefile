#
# Makefile to build RPMs and install them.
# This is primarily for the developer use.
#

rpm:
	mkdir -p SOURCES && tar czpvf SOURCES/npf.tar.gz ../src
	rpmbuild -ba -v --define "_topdir ${PWD}" SPECS/npf.spec

devinstall:
	sudo rpm -ev npf npf-debuginfo && sudo rpm -Uhv RPMS/x86_64/npf-*

clean:
	rm -rf BUILD BUILDROOT RPMS SOURCES SRPMS
