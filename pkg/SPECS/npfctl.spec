%define version	%(cat %{_topdir}/version.txt)

Name:		npfctl
Version:	%{version}
Release:	1%{?dist}
Summary:	Standalone NPF package: npfctl utility
License:	BSD
URL:		https://github.com/rmind/npf
Source0:	npfctl.tar.gz

BuildRequires:	make
BuildRequires:	libtool
BuildRequires:	openssl-devel
BuildRequires:	flex
BuildRequires:	byacc
BuildRequires:	libnpf

Requires:	libnv
Requires:	libnpf

%description
NPF is a layer 3 packet filter, supporting IPv4 and IPv6 as well as layer
4 protocols such as TCP, UDP, and ICMP.  It was designed with a focus on
high performance, scalability and modularity.  NPF was written from
scratch in 2009.  It is written in C99 and distributed under the 2-clause
BSD license.

This RPM package contains npfctl(8) utility.


%prep
%setup -q -n src


%build
(cd npfctl && make %{?_smp_mflags})
(cd lib && make %{?_smp_mflags} LIBDIR=%{_libdir})


%install
(cd npfctl && make install \
    DESTDIR=%{buildroot} BINDIR=%{_bindir} MANDIR=%{_mandir})
(cd lib && make install \
    DESTDIR=%{buildroot} LIBDIR=%{_libdir})


%files
%{_libdir}/npf/*
%{_bindir}/*
%{_mandir}/*


%changelog

