**INFO**: The standalone port of NPF is a work-in-progress -- 19 February 2015

# NPF -- NetBSD packet filter (standalone)

NPF is a layer 3 packet filter, supporting IPv4 and IPv6 as well as layer
4 protocols such as TCP, UDP, and ICMP.  It was designed with a focus on
high performance, scalability, and modularity.

NPF was written from scratch in 2009 and is distributed under the
2-clause BSD license.

This repository contains a **standalone** version of NPF.

## Features

NPF offers the traditional set of features provided by packet filters.
Some key features are:
- Stateful inspection (connection tracking).
- Network address translation (NAT).  This includes static (stateless) and
  dynamic (stateful) translation, port translation, bi-directional NAT, etc.
- IPv6-to-IPv6 network prefix translation (NPTv6).
- Tables for efficient IP sets.
- Application Level Gateways (e.g., to support traceroute).
- NPF uses BPF with just-in-time (JIT) compilation.
Rule procedures and a framework for NPF extensions.
- Traffic normalization (extension).
- Packet logging (extension).

For a full set features and their description, see the NPF documentation
and other manual pages.

## Documentation

http://www.netbsd.org/~rmind/npf/

## Source code structure

    src/                - root directory of the standalone NPF

        kern/           - the kernel component
                          http://nxr.netbsd.org/xref/src/sys/net/npf/

        libnpf/         - library to manage the kernel component
                          http://nxr.netbsd.org/xref/src/lib/libnpf/

        npfctl/         - command line user interface to control NPF
                          http://nxr.netbsd.org/xref/src/usr.sbin/npf/npfctl/
