# NPF -- packet filter with stateful inspection, NAT, IP sets, etc

[![Build Status](https://travis-ci.org/rmind/npf.svg?branch=master)](https://travis-ci.org/rmind/npf)

NPF is a layer 3 packet filter, supporting IPv4 and IPv6 as well as layer
4 protocols such as TCP, UDP, and ICMP.  It was designed with a focus on
high performance, scalability and modularity.  NPF was written from
scratch in 2009.  It is written in C99 and distributed under the 2-clause
BSD license.

## Features

NPF offers the traditional set of features provided by packet filters.
Some key features are:
- Stateful inspection (connection tracking).
- Network address translation (NAT):
  - Static (stateless) and dynamic (stateful) translation.
  - NAPT and other forms of port translation (e.g. port forwarding).
  - Inbound and outbound NAT as well as bi-directional NAT.
  - IPv6-to-IPv6 network prefix translation (NPTv6).
- Tables for efficient IP sets.
- Application Level Gateways (e.g., to support traceroute).
- NPF uses BPF with just-in-time (JIT) compilation.
- Rule procedures and a framework for NPF extensions.
- Traffic normalization (extension).
- Packet logging (extension).

For a full set of features and their description, see the NPF documentation
and other manual pages.

## Documentation

http://www.netbsd.org/~rmind/npf/

## Source code structure

    src/                - root directory of the standalone NPF
        kern/           - the kernel component (as a library)
        libnpf/         - library to manage the kernel component
        npfctl/         - command line user interface to control NPF
        npftest/        - unit tests and utility to debug NPF
    pkg/                - packaging files (e.g. RPM specs)

## Packages

To build the libnpf library (link using the `-lnpf` and `-lnpfkern`
flags) packages:
* RPM (tested on RHEL/CentOS 7): `cd pkg && make rpm`
* DEB (tested on Debian 9): `cd pkg && make deb`
