rpm:
	mkdir -p SOURCES && tar czpvf SOURCES/npf.tar.gz ../src
	rpmbuild -ba -v --define "_topdir ${PWD}" SPECS/npf.spec

clean:
	rm -rf BUILD BUILDROOT RPMS SOURCES SRPMS
