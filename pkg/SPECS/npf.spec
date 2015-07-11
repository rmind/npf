Name:		npf
Version:	0.1
Release:	1%{?dist}
Summary:	Standalone NPF package
License:	BSD
URL:		http://www.netbsd.org/~rmind/npf/
Source0:	npf.tar.gz

BuildRequires:	make
BuildRequires:	libtool
Requires:	libbpfjit
#BuildRequires:	libcdb-devel
#BuildRequires:	libprop-devel

Requires:	libcdb
Requires:	libprop
Requires:	libbpfjit
Requires:	jemalloc

%description

NPF is a layer 3 packet filter, supporting IPv4 and IPv6 as well as layer
4 protocols such as TCP, UDP, and ICMP.  It was designed with a focus on
high performance, scalability, and modularity.  NPF was written from scratch
in 2009 and is distributed under the 2-clause BSD license.

This RPM package is a standalone version of NPF.  It contains libnpfkern
and libnpf libraries.


%prep
%setup -q -n src


%build
make %{?_smp_mflags} LIBDIR=%{_libdir}


%install
make install \
    DESTDIR=%{buildroot} \
    LIBDIR=%{_libdir} \
    INCDIR=%{_includedir} \
    MANDIR=%{_mandir}
cd kern && make clean && make install \
    DEBUG=1 \
    DESTDIR=%{buildroot} \
    LIBDIR=%{_libdir} \
    INCDIR=%{_includedir} \
    MANDIR=%{_mandir}


%files
%{_libdir}/*
%{_includedir}/*
%{_mandir}/*


%changelog

