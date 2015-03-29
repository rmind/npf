#
# Makefile to build RPMs and install them.
# This is primarily for the developer use.
#

rpm: npf npfctl

npf: SOURCES
	rpmbuild -ba -v --define "_topdir ${PWD}" SPECS/npf.spec

npfctl: SOURCES
	rpmbuild -ba -v --define "_topdir ${PWD}" SPECS/npfctl.spec

SOURCES:
	mkdir -p SOURCES && tar czpvf SOURCES/npf.tar.gz ../src

devinstall:
	sudo rpm -ev npf npf-debuginfo || true
	sudo rpm -ev npfctl npfctl-debuginfo || true
	sudo rpm -Uhv RPMS/x86_64/npf*

clean:
	rm -rf BUILD BUILDROOT RPMS SOURCES SRPMS

.PHONY: rpm npf npfctl devinstall clean
