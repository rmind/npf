# Introduction

NPF is a layer 3 packet filter, supporting IPv4 and IPv6, as well as layer 4
protocols such as TCP, UDP and ICMP.  NPF offers a traditional set of features
provided by most packet filters.  This includes stateful packet filtering,
network address translation (NAT), tables (which provide different data
structures as a container), rule procedures for easy development of NPF
extensions, packet normalisation and logging, connection saving and restoring
and more.  NPF focuses on high performance design, ability to handle large
volume of clients and using the speed of multi-core systems.  It was written
from scratch in 2009 and is released under permissive 2-clause BSD license.

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
extensions.  Syntax of the configuration file supports arbitrary procedures
with their parameters, as supplied by the extensions.  An extensions consists
of two parts: a dynamic module (.so file) supplementing the
[npfctl(8)](http://man.netbsd.org/cgi-bin/man-cgi?npfctl+8+NetBSD-current)
utility and a kernel module (.kmod file).  Kernel interfaces are available
for use and avoid modifications to the NPF core code.

The internals of NPF are abstracted into well defined modules and follow
strict interfacing principles to ease the extensibility.  Communication
between userspace and the kernel is provided through the library -- libnpf,
described in the
[libnpf(3)](http://man.netbsd.org/cgi-bin/man-cgi?libnpf+3+NetBSD-current)
manual page.  It can be conveniently used by the developers who create their
own extensions or third party products based on NPF.  Application level
gateways (ALGs), such as support for
[traceroute(8)](http://man.netbsd.org/cgi-bin/man-cgi?traceroute+8+NetBSD-current),
are also abstracted in separate modules.

## Processing

NPF intercepts the packets at layer 3 on entry to the IP stack.  The packet
may be rejected before the NPF inspection if it is malformed and has invalid
IPv4 or IPv6 header or some fields.  Incoming IP packets are passed to NPF
before the IP reassembly.  Unless disabled, reassembly is performed by NPF.

Processing is performed on each interface a packet is traversing, either as
_incoming_ or _outgoing_.  Support for processing on _forwarding_ path and
fast-forward optimisations is planned for the future release.

Processing order within NPF is as follows:
```
    state inspection
        -> rule inspection (if no state)
            -> address/port translation
                -> rule procedure processing
```
