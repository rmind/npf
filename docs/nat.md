# Network Address Translation

NPF supports various forms of network address translation (NAT).  The
translation may be dynamic (stateful) or static (stateless).  This includes
includes traditional NAT (known as NAPT or masquerading), bi-directional NAT
and port forwarding (redirection).  Static NAT currently supports simple 1:1
mapping of IPv4 addresses and IPv6-to-IPv6 network prefix translation (NPTv6).
NAT64 (the protocol translation) is planned for a future release of NPF.
It should be remembered that dynamic NAT, as a concept, relies on stateful
filtering, therefore it is performing it implicitly.

NAT rules are expressed in a form of segment mapping:
```
map	= "map" interface
	  ( "static" [ "algo" algorithm ] | "dynamic" )
	  net-seg ( "->" | "<-" | "<->" ) net-seg
	  [ "pass" [ proto ] filt‐opts ]
```

The following is an example configuration fragment of a traditional NAPT setup:
```
map $ext_if dynamic $localnet -> $ext_if

group "external" on $ext_if {
  pass stateful out final proto tcp from $localnet
}
```

The first line enables traditional NAPT (keyword `map`) on the interface
specified by `$ext_if` for all packets from the network defined in
`$localnet` to any other network (0.0.0.0/0), where the address to translate to
is the (only) one on the interface `$ext_if` (it may be specified directly
as well, and has to be specified directly if the interface has more than
one IPv4 address).

The arrow indicates the translation type, which can be one of the following:

* `->`&nbsp;&nbsp;&nbsp; for outbound NAT (also known as source NAT).
* `<-`&nbsp;&nbsp;&nbsp; for inbound NAT (destination NAT).
* `<->`&nbsp; for bi-directional NAT.

The rule `pass ...` permits all outgoing packets from the specified
network.  It additionally has stateful tracking enabled with the keyword
`stateful`.  Therefore, any incoming packets belonging to the connections
which were created by initial outgoing packets will be implicitly passed.

The following two lines are example fragments of bi-directional NAT and
port 8080 forwarding to a local IP address, port 80:
```
map $ext_alt_if dynamic $local_host_1 <-> $ext_alt_if
map $ext_if dynamic $local_host_2 port 80 <- $ext_if port 8080
```

In the examples above, NPF determines the filter criteria from the segments
on the left and right hand side implicitly.  Filter criteria can be specified
explicitly using an optional `pass ...` syntax in conjunction with `map`.
In such case the criteria has to be full, i.e. for both the source and the
destination.  For example:
```
map $ext_if dynamic 127.0.0.1 port 8080 <- 0.0.0.0 \
    pass from 10.0.0.1 to $rdr_ip port 80
```

This rule would redirect traffic only from 10.0.0.1 host with destination
port 80 and according destination address.  The left hand side (as it is
inbound NAT), according to the arrow, is used as a translation address.
It should be noted that the right hand side is ignored (and thus can be
0.0.0.0) as the filter criteria is specified explicitly.

The following lines illustrate a static NAT rule which performs IPv6 Network
Prefix Translation (NPTv6), as described in
[RFC 6296](https://tools.ietf.org/html/rfc6296):
```
$net6_inner = fd01:203:405::/48
$net6_outer = 2001:db8:1::/48

map $ext_if static algo npt66 $net6_inner <-> $net6_outer
```
