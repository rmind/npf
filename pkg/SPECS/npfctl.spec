Name:		npfctl
Version:	0.1
Release:	1%{?dist}
Summary:	Standalone NPF package: npfctl utility
License:	BSD
URL:		http://www.netbsd.org/~rmind/npf/
Source0:	npf.tar.gz

BuildRequires:	make
BuildRequires:	libtool
BuildRequires:	openssl-devel
#BuildRequires:	libcdb-devel
#BuildRequires:	libprop-devel
BuildRequires:	flex
BuildRequires:	byacc

Requires:	libcdb
Requires:	libprop
Requires:	npf

%description

NPF is a layer 3 packet filter, supporting IPv4 and IPv6 as well as layer
4 protocols such as TCP, UDP, and ICMP.  It was designed with a focus on
high performance, scalability, and modularity.  NPF was written from scratch
in 2009 and is distributed under the 2-clause BSD license.

This RPM package contains npfctl(8) utility.


%prep
%setup -q -n src/npfctl


%build
make %{?_smp_mflags}


%install
make install DESTDIR=%{buildroot} BINDIR=%{_bindir} MANDIR=%{_mandir}


%files
%{_bindir}/*
%{_mandir}/*


%changelog

