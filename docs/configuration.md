# Configuration

This chapter will describe NPF configuration in the context of NetBSD
or a similar UNIX-like system environment.

The standalone NPF configuration would be constructed using the `libnpf`
library and submitted to the kernel-side component, using the `npfkern`
library.  See the libnpf(3) and npfkern(3) manual pages for the API.

---
NetBSD beginners can consult
[rc(8)](http://man.netbsd.org/cgi-bin/man-cgi?rc+8+NetBSD-current),
[rc.conf(5)](http://man.netbsd.org/cgi-bin/man-cgi?rc.conf+5+NetBSD-current),
[ifconfig.if(5)](http://man.netbsd.org/cgi-bin/man-cgi?ifconfig.if+5+NetBSD-current),
[route(8)](http://man.netbsd.org/cgi-bin/man-cgi?route+8+NetBSD-current)
and other manual pages for the general networking configuration in the system.

---

## Structure and syntax

The NPF configuration is represented by a file, called `npf.conf` (the
default location is `/etc/npf.conf`).  It is loaded and NPF is generally
operated via the command line utility called `npfctl`.  For a reference,
use
[npf.conf(5)](http://man.netbsd.org/cgi-bin/man-cgi?npf.conf+5+NetBSD-current)
and
[npfctl(8)](http://man.netbsd.org/cgi-bin/man-cgi?npfctl+8+NetBSD-current)
manual pages.

There are multiple structural elements that `npf.conf` may contain, such as:
- variables
- table definitions (with or without content)
- abstraction groups
- packet filtering rules
- map rules for address translation
- application level gateways
- procedure definitions to call on filtered packets.

### Variables

Variables are specified using the dollar (`$`) sign, which is used for both
definition and referencing of a variable.  Variables are defined by
assigning a value to them as follows:
```
$var1 = 10.0.0.1
```
A variable may also be defined as a set:
```
$var2 = { 10.0.0.1, 10.0.0.2 }
```

Common variable definitions are for IP addresses, networks, ports, and
interfaces.

### Tables

Tables are specified using a name between angle brackets `<` and `>`.
The following is an example of table definition:
```
table <blocklist> type ipset
```

Currently, tables support three data storage types: `ipset`, `lpm`, or `const`.
The contents of the table may be pre-loaded from the specified file.
The `const` tables are immutable (no insertions or deletions after loading)
and therefore must always be loaded from a file.

The specified file should contain a list of IP addresses and/or networks
in the form of 10.1.1.1 or 10.0.0.0/24

Tables of type `ipset` and `const` can only contain IP addresses.  The `lpm`
tables can contain networks and they will perform the longest prefix match
on lookup.

### Interfaces

In NPF, an interface can be referenced directly by using its name, or can
be passed to an extraction function which will return a list of IP
addresses configured on the actual associated interface.

It is legal to pass an extracted list from an interface in keywords where
NPF would expect instead a direct reference to said interface.  In this
case, NPF infers a direct reference to the interface, and does not con‐
sider the list.

There are two types of IP address lists.  With a static list, NPF will
capture the interface addresses on configuration load, whereas with a
dynamic list NPF will capture the runtime list of addresses, reflecting
any changes to the interface, including the attach and detach.  Note that
with a dynamic list, bringing the interface down has no effect, all
addresses will remain present.

The following functions exist, to extract addresses from an interface with
a chosen list type and IP address type:

| Function | Type | Description |
| -------------------- | ------------ | -------------- |
| `inet4(interface)`   | static list  | IPv4 addresses |
| `inet6(interface)`   | static list  | IPv6 addresses |
| `ifaddrs(interface)` | dynamic list | Both IPv4 and IPv6.  The family keyword of a filtering rule can be used in combination to explicitly select an IP address type. |

Example of configuration:
```
$var1 = inet4(wm0)
$var2 = ifaddrs(wm0)

group default {
  block in on wm0 all               # rule 1
  block in on $var1 all             # rule 2
  block in on inet4(wm0) all        # rule 3
  pass in on inet6(wm0) from $var2  # rule 4
  pass in on wm0 from ifaddrs(wm0)  # rule 5
}
```

In the above example, $var1 is the static list of IPv4 addresses configured
on wm0, and $var2 is the dynamic list of all the IPv4 and IPv6
addresses configured on wm0.  The first three rules are equivalent,
because with the `block ... on <interface>` syntax, NPF expects a direct
reference to an interface, and therefore does not consider the extraction
functions.  The fourth and fifth rules are equivalent, for the same reason.

### Groups

NPF requires that all rules be defined within groups.  Groups can be
thought of as higher level rules which can contain subrules.  Groups may
have the following options: name, interface, and direction.  Packets
matching group criteria are passed to the ruleset of that group.  If a
packet does not match any group, it is passed to the default group.  The
default group must always be defined.

Example of configuration:
```
group "my‐name" in on wm0 {
  # List of rules, for packets received on wm0
}

group default {
  # List of rules, for the other packets
}
```

### Rules

With a rule statement NPF is instructed to `pass` or `block` a packet
depending on packet header information, transit direction and the interface it
arrived on, either immediately upon match or using the last match.

If a packet matches a rule which has the `final` option set, this rule is
considered the last matching rule, and evaluation of subsequent rules is
skipped.  Otherwise, the last matching rule is used.

The `proto` keyword can be used to filter packets by layer 4 protocol (TCP,
UDP, ICMP or other).  Its parameter should be a protocol number or its
symbolic name, as specified in the `/etc/protocols` file.  This keyword can
additionally have protocol‐specific options, such as `flags`.

The `flags` keyword can be used to match the packets against specific TCP
flags, according to the following syntax:
```
proto tcp flags match[/mask]
```

Where match is the set of TCP flags to be matched, out of the mask set,
both sets being represented as a string combination of: `S` (SYN), `A`
(ACK), `F` (FIN), and `R` (RST).  The flags that are not present in _mask_
are ignored.

To notify the sender of a blocking decision, three return options can be
used in conjunction with a block rule:

| Keyword | Description |
| --- | --- |
| `return` | Behaves as return‐rst or return‐icmp, depending on whether the packet being blocked is TCP or UDP. |
| `return‐rst` | Return a TCP RST message, when the packet being blocked is a TCP packet.  Applies to IPv4 and IPv6. |
| `return‐icmp` | Return an ICMP UNREACHABLE message, when the packet being blocked is a UDP packet.  Applies to IPv4 and IPv6. |

Further packet specification at present is limited to TCP and UDP under‐
standing source and destination ports, and ICMP and IPv6‐ICMP understand‐
ing icmp‐type.

A rule can also instruct NPF to create an entry in the state table when
passing the packet or to apply a procedure to the packet (e.g. "log").

A "fully‐featured" rule would for example be:
```
pass stateful in final family inet4 proto tcp flags S/SA \
  from $source port $sport to $dest port $dport apply "someproc"
```

Alternatively, NPF supports
[pcap-filter(7)](http://man.netbsd.org/cgi-bin/man-cgi?pcap-filter+7+NetBSD-current)
syntax, for example:
```
block out final pcap‐filter "tcp and dst 10.1.1.252"
```

Fragments are not selectable since NPF always reassembles packets before
further processing.

### Stateful

Stateful packet inspection is enabled using the `stateful` or `stateful‐ends`
keywords.  The former creates a state which is uniquely identified by a
5‐tuple (source and destination IP addresses, port numbers and an inter‐
face identifier).  The latter excludes the interface identifier and must
be used with precaution.  In both cases, a full TCP state tracking is
performed for TCP connections and a limited tracking for message‐based
protocols (UDP and ICMP).

By default, a stateful rule implies SYN‐only flag check ("flags S/SAFR")
for the TCP packets.  It is not advisable to change this behavior; how‐
ever, it can be overridden with the aforementioned `flags` keyword.

### Map

Network Address Translation (NAT) is expressed in a form of segment map‐
ping.  The translation may be dynamic (stateful) or static (stateless).
The following mapping types are available:

| Syntax | Description |
|:------:| --- |
| `‐>`   | outbound NAT (translation of the source) |
| `<‐`   | inbound NAT (translation of the destination) |
| `<‐>`  | bi‐directional NAT (combination of inbound and outbound NAT) |

The following would translate the source (10.1.1.0/24) to the IP address
specified by `$pub_ip` for the packets on the interface `$ext_if`.
```
map $ext_if dynamic 10.1.1.0/24 ‐> $pub_ip
```

Translations are implicitly filtered by limiting the operation to the
network segments specified, that is, translation would be performed only
on packets originating from the 10.1.1.0/24 network.  Explicit filter
criteria can be specified using `pass criteria ...` as an additional option
of the mapping.

The dynamic NAT implies network address and port translation (NAPT).  The
port translation can be controlled explicitly.  For example, the following
provides "port forwarding", redirecting the public port 9022 to the
port 22 of an internal host:
```
map $ext_if dynamic proto tcp 10.1.1.2 port 22 <‐ $ext_if port 9022
```

If the dynamic NAT is configured with multiple translation addresses,
then a custom selection algorithm can be chosen using the `algo` keyword.
The currently available algorithms are:

| Algorithm | Description |
|:---------:| --- |
| `ip-hash` | The translation address for a new connection is selected based on a hash of the original source and destination addresses. This algorithms attempts to keep all connections of particular client associated with the same translation address. This is the default algorithm. |
| `round-robin` | The translation address for each new connection is selected on a round-robin basis. |

The static NAT can also have different address translation algorithms,
chosen using the `algo` keyword.  The currently available algorithms are:

| Algorithm | Description |
|:---------:| --- |
| `netmap`  | Network address mapping from one segment to another, leaving the host part as-is. The new address is computed as following: `addr = net-addr | (orig-addr & ~mask)` |
| `npt66`   | IPv6‐to‐IPv6 network prefix translation (NPTv6) |

If no algorithm is specified, then 1:1 address mapping is assumed.
Currently, the static NAT algorithms do not perform port translation.

### Application Level Gateways

Certain application layer protocols are not compatible with NAT and
require translation outside layers 3 and 4.  Such translation is per‐
formed by packet filter extensions called Application Level Gateways
(ALGs).

NPF supports the following ALGs:
- ICMP ALG (keyword `icmp`): Applies to IPv4 and IPv6.  Allows to find an
  active connection by looking at the ICMP payload, and to perform NAT
  translation of the ICMP payload.  Generally, this ALG is necessary to
  support `traceroute(8)` behind the NAT, when using the UDP or TCP probes.

The ALGs are built‐in.  If NPF is used as kernel module, then they come
as kernel modules too.  In such case, the ALG kernel modules can be
autoloaded through the configuration, using the `alg` keyword.

For example:
```
alg "icmp"
```

Alternatively, the ALG kernel modules can be loaded manually, using
`modload(8)`.

### Procedures

A rule procedure is defined as a collection of extension calls (it may
have none).  Every extension call has a name and a list of options in the
form of key‐value pairs.  Depending on the call, the key might represent
the argument and the value might be optional.  Available options:
- `log: interface`: Log events. This requires the `npf_ext_log`
  kernel module, which would normally get auto loaded by NPF.  The specified
  npflog interface would also be auto‐created once the configuration is loaded.
  The log packets can be written to a file using the `npfd(8)` daemon.
- `normalize: option1[, option2 ...]`: Modify packets according to the
  specified normalization options.  This requires the `npf_ext_normalize`
  kernel module, which would normally get auto‐loaded by NPF.

The available normalization options are:

| Parameter | Description |
| --- | --- |
| `max‐mss <value>` | Enforce a maximum value for the Maximum Segment Size (MSS) TCP option.  Typically, for "MSS clamping".
| `min‐ttl <value>` | Enforce a minimum value for the IPv4 Time To Live (TTL) parameter.
| `no‐df` | Remove the Don't Fragment (DF) flag from IPv4 packets.
| `random‐id` | Randomize the IPv4 ID parameter. |

For example:
```
procedure "someproc" {
  log: npflog0
  normalize: "random‐id", "min‐ttl" 64, "max‐mss" 1432
}
```

In this case, the procedure calls the logging and normalization modules.

### Misc

Text after a hash (`#`) character is considered a comment.  The backslash
(`\`) character at the end of a line marks a continuation line, i.e., the
next line is considered an extension of the present line.

## Control and operation

NPF is controlled using the
[npfctl(8)](http://man.netbsd.org/cgi-bin/man-cgi?npfctl+8+NetBSD-current)
utility.
```
$ npfctl
Usage:	npfctl start | stop | flush | show | stats
	npfctl validate | reload [<rule-file>]
	npfctl rule "rule-name" { add | rem } <rule-syntax>
	npfctl rule "rule-name" rem-id <rule-id>
	npfctl rule "rule-name" { list | flush }
	npfctl table "table-name" { add | rem | test } <address/mask>
	npfctl table "table-name" { list | flush }
	npfctl table "table-name" replace [-n "name"] [-t <type>] <table-file>
	npfctl save | load
	npfctl list [-46hNnw] [-i <ifname>]
	npfctl debug [<rule-file>] [<raw-output>]
```

Once the NPF configuration file has been written, use `npfctl` to load it
and then start the packet handling:
```
$ npfctl load
$ npfctl start
```

Any modifications of npf.conf require reloading of the ruleset by performing
a `reload` command in order to make the changes active.  One difference from
other packet filters is the behaviour of the `start` and `stop` commands.
These commands do not actually change (i.e. load or unload) the active
configuration.  Running `start` will only enable the passing of packets
through NPF, while `stop` will disable such passing.  Therefore,
configuration should first be activated using the `reload` command and then
filtering enabled with `start`.  Similarly, clearing of the active
configuration is done by performing the `stop` and `flush` commands.
Such behaviour allows users to efficiently disable and enable filtering
without actually changing the active configuration, as it may be unnecessary.

### Autostart on boot

In NetBSD, the rc.d system can be used to start NPF on boot.  The following
is an example for starting NPF and loading the configuration through the rc.d
script:
```
$ echo 'npf=YES' >> /etc/rc.conf
$ /etc/rc.d/npf reload
Reloading NPF ruleset.
$ /etc/rc.d/npf start
Enabling NPF.
```
