# Introduction

NPF is a layer 3 packet filter, supporting IPv4 and IPv6, as well as layer 4
protocols such as TCP, UDP and ICMP.  NPF offers a traditional set of features
provided by most packet filters.  This includes stateful packet filtering,
various forms of network address translation (NAT), IP sets (tables which
provide different data structures as a container), rule procedures for easy
development of NPF extensions, packet normalisation and logging, connection
saving and restoring and more.

It was designed with a focus on high performance, scalability, multi-threading
and modularity. NPF was written from scratch in 2009.  It is written in C99
and distributed under the 2-clause BSD license.

## Mode of operation

NPF was originally developed for the NetBSD operating system.  However, NPF
is also provided as a userspace library to be used in a bespoke application
to process packets.  Typically, in combination with such frameworks like
[Data Plane Development Kit (DPDK)](https://www.dpdk.org/) or
[netmap](https://www.freebsd.org/cgi/man.cgi?query=netmap&sektion=4).

Some aspects of this documentation, particularly concerning the configuration,
will be in the context of NetBSD (or other UNIX-like system).  However, the
general principles and concepts apply to the standalone NPF (as-a-library).

## Brief notes on design

NPF uses
[Berkeley Packet Filter (BPF) byte-code](http://man.netbsd.org/cgi-bin/man-cgi?bpf+4+NetBSD-current),
which is just-in-time (JIT) compiled into the machine code.  Each rule is
described by a sequence of low level operations to perform for a packet.
This design has the advantage of protocol independence, therefore support
for new protocols (for example, layer 7) or custom filtering patterns can
be easily added at userspace level without any modifications to the kernel
itself.

NPF provides rule procedures as the main interface to implement custom
extensions.  The configuration syntax file supports arbitrary procedures
with their parameters, as supplied by the extensions.  An extension consists
of two parts: a dynamic module (.so file) supplementing the
[npfctl(8)](http://man.netbsd.org/cgi-bin/man-cgi?npfctl+8+NetBSD-current)
utility and a kernel module (.kmod file).  Kernel interfaces are available
for use and avoid modifications to the NPF core code.

The internals of NPF are abstracted into well defined modules and follow
strict interfacing principles to ease the extensibility.  Communication
between userspace and the kernel is provided through the library -- **libnpf**,
described in the
[libnpf(3)](http://man.netbsd.org/cgi-bin/man-cgi?libnpf+3+NetBSD-current)
manual page.  It can be conveniently used by the developers who create their
own extensions or third party products based on NPF.  Application level
gateways (ALGs), such as support for
[traceroute(8)](http://man.netbsd.org/cgi-bin/man-cgi?traceroute+8+NetBSD-current),
are also abstracted in separate modules.

## Processing

NPF intercepts the packets at layer 3 of the TCP/IP stack.  The packet
may be rejected before the NPF inspection if it is malformed and has invalid
IPv4 or IPv6 header or some fields.  Incoming IP packets are passed to NPF
before the IP reassembly.  Unless disabled, reassembly is performed by NPF.

Processing is performed on each interface a packet is traversing, either as
_incoming_ or _outgoing_.  Support for processing on _forwarding_ path and
fast-forward optimisation is planned for the future release.

Packets can be _incoming_ or _outgoing_ with respect to an interface.
_Connection direction_ is identified by the direction of its first packet.
The meaning of incoming/outgoing packet in the context of connection direction
can be confusing.  Therefore, we will use the terms _forwards stream_ and
_backwards stream_, where packets in the forwards stream mean the packets
travelling in the direction as the connection direction.

Processing order within NPF is as follows:
```
state inspection
  -> rule inspection (if no state)
    -> address/port translation
      -> rule procedure processing
```
