Name:		npf
Version:	1.0.0
Release:	1%{?dist}
Summary:	Standalone NPF package
License:	BSD
URL:		https://github.com/rmind/npf
Source0:	npf.tar.gz

BuildRequires:	make
BuildRequires:	libtool
#BuildRequires:	libnv-devel
Requires:	libbpfjit
BuildRequires:	libqsbr
BuildRequires:	liblpm >= 0.2.0
#BuildRequires:	libcdb-devel

Requires:	libnv
Requires:	libbpfjit
Requires:	libqsbr
Requires:	liblpm
Requires:	libcdb
Requires:	jemalloc

%description
NPF is a layer 3 packet filter, supporting IPv4 and IPv6 as well as layer
4 protocols such as TCP, UDP, and ICMP.  It was designed with a focus on
high performance, scalability and modularity.  NPF was written from
scratch in 2009.  It is written in C99 and distributed under the 2-clause
BSD license.

This RPM package is a standalone version of NPF.  It contains the
libnpfkern and libnpf libraries.


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

