# NPF: stateful packet filter supporting NAT, IP sets, etc

[![Build Status](https://travis-ci.org/rmind/npf.svg?branch=master)](https://travis-ci.org/rmind/npf)

NPF is a layer 3 packet filter, supporting stateful packet inspection,
IPv6, NAT, IP sets, extensions and many more.  It was designed with a focus
on high performance, scalability, multi-threading and modularity.  NPF was
written from scratch in 2009.  It is written in C99 and distributed under
the 2-clause BSD license.

NPF is provided as a **userspace library** to be used in a bespoke application
to process packets. Typically, in combination with such frameworks like
[Data Plane Development Kit (DPDK)](https://www.dpdk.org/) or
[netmap](https://www.freebsd.org/cgi/man.cgi?query=netmap&sektion=4).

## Features

NPF offers the traditional set of features provided by packet filters.
Some key features are:
- Stateful inspection (connection tracking).
  - Including the [full TCP state tracking](https://www.usenix.org/events/sec01/invitedtalks/rooij.pdf).
- Network address translation (NAT):
  - Static (stateless) and dynamic (stateful) translation.
  - NAPT and other forms of port translation (e.g. port forwarding).
  - Inbound and outbound NAT as well as bi-directional NAT.
  - Network-to-network translation, including NETMAP and NPTv6.
- Tables for efficient IP sets, including the _longest prefix match_ support.
- Application Level Gateways (e.g. to support traceroute).
- NPF uses [BPF with just-in-time (JIT) compilation](https://github.com/rmind/bpfjit).
- Rule procedures and a framework for NPF extensions (plugins).
- Extensions include: traffic normalization and packet logging.
- [Data Plane Development Kit](https://dpdk.org/) integration.

For a full set of features and their description, see the NPF documentation
and other manual pages.

## Documentation

See on [Github Pages](http://rmind.github.io/npf).
Source in the [docs](docs) directory.

## Dependencies

- libnv: `git clone https://github.com/rmind/nvlist`
- thmap: `git clone https://github.com/rmind/thmap`
- libqsbr: `git clone https://github.com/rmind/libqsbr`
- liblpm: `git clone https://github.com/rmind/liblpm`
- bpfjit: `git clone https://github.com/rmind/bpfjit`
- libcdb: `git clone https://github.com/rmind/libcdb`

Each repository provides the build files for RPM (`cd pkg && make rpm`)
and DEB (`cd pkg && make deb`) packages.  You can also check the
[Travis](.travis.yml) file for an example of how to build everything.

## Source code structure

    docs/               - documentation
    src/                - root source code directory
        kern/           - the kernel component (npfkern library)
        libnpf/         - library to manage the NPF configuration
        npfctl/         - command line user interface to control NPF
        npftest/        - unit tests and a tool to debug NPF
    dpdk/               - DPDK integration code and a demo
    pkg/                - packaging files (RPM and DEB)

## Packages

To build the libnpf library (link using the `-lnpf` and `-lnpfkern`
flags) packages:
* RPM (tested on RHEL/CentOS 7): `cd pkg && make rpm`
* DEB (tested on Debian 9): `cd pkg && make deb`

## Who is using NPF?

<table>
  <tr height="150">
    <th width="150"><a href="https://en.outscale.com"><img src="https://fr.outscale.com/wp-content/uploads/2018/07/Logo_Outscale_Bleu_RGB.png" alt="Outscale" align="middle"></a></th>
    <th width="150"><a href="https://innofield.com"><img src="https://innofield.com/wp-content/uploads/2014/07/innofield_logo_sticky.gif" alt="innofield AG" align="middle" width="80%"></a></th>
    <th width="150"><a href="https://www.netbsd.org"><img src="https://www.netbsd.org/images/NetBSD.png" alt="NetBSD" align="middle" width="70%"></a></th>
    <th width="150"><a href="http://therouter.net"><img src="http://therouter.net/images/r.png" alt="TheRouter" align="middle"></a></th>
  </tr>
</table>
