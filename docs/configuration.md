# Configuration

The first step is configuring general networking settings in the system,
for example assigning the addresses and bringing up the interfaces.  NetBSD
beginners can consult
[rc(8)](http://man.netbsd.org/cgi-bin/man-cgi?rc+8+NetBSD-current),
[rc.conf(5)](http://man.netbsd.org/cgi-bin/man-cgi?rc.conf+5+NetBSD-current),
[ifconfig.if(5)](http://man.netbsd.org/cgi-bin/man-cgi?ifconfig.if+5+NetBSD-current)
and other manual pages.
The second step is to create NPF's configuration file (by default,
/etc/npf.conf).  We will give an overview with some simple and practical
examples.  A detailed description of the syntax and options is provided in the
[npf.conf(5)](http://man.netbsd.org/cgi-bin/man-cgi?npf.conf+5+NetBSD-current)
manual page.  The following is an example configuration for small office/home
office (SOHO) network.  It contains two groups for two network interfaces and
a default group:

```
$ext_if = { inet4(wm0) }
$int_if = { inet4(wm1) }

table <blacklist> type hash file "/etc/npf_blacklist"
table <limited> type tree dynamic

$services_tcp = { http, https, smtp, domain, 6000, 9022 }
$services_udp = { domain, ntp, 6000 }
$localnet = { 10.1.1.0/24 }

alg "icmp"

# Note: if $ext_if has multiple IP address (e.g. IPv6 as well),
# then the translation address has to be specified explicitly.
map $ext_if dynamic 10.1.1.0/24 ‐> $ext_if
map $ext_if dynamic proto tcp 10.1.1.2 port 22 <‐ $ext_if port 9022

procedure "log" {
  # The logging facility can be used together with npfd(8).
  log: npflog0
}

group "external" on $ext_if {
  pass stateful out final all

  block in final from <blacklist>
  pass stateful in final family inet4 proto tcp to $ext_if port ssh apply "log"
  pass stateful in final proto tcp to $ext_if port $services_tcp
  pass stateful in final proto udp to $ext_if port $services_udp
  pass stateful in final proto tcp to $ext_if port 49151‐65535  # passive FTP
  pass stateful in final proto udp to $ext_if port 33434‐33600  # traceroute
}

group "internal" on $int_if {
  block in all
  block in final from <limited>

  # Ingress filtering as per BCP 38 / RFC 2827.
  pass in final from $localnet
  pass out final all
}

group default {
 pass final on lo0 all
 block all
}
```

It will be explained in detail below.

## Control

NPF can be controlled through the
[npfctl(8)](http://man.netbsd.org/cgi-bin/man-cgi?npfctl+8+NetBSD-current)
utility or NetBSD's rc.d system.
The latter is used during system startup, but essentially it is a convenience
wrapper.  The following is an example for starting NPF and loading the
configuration through the rc.d script:

```
srv# echo 'npf=YES' >> /etc/rc.conf
srv# /etc/rc.d/npf reload
Reloading NPF ruleset.
srv# /etc/rc.d/npf start
Enabling NPF.

srv# npfctl
Usage:  npfctl start | stop | flush | show | stats
        npfctl validate | reload [<rule-file>]
        npfctl rule "rule-name" { add | rem } <rule-syntax>
        npfctl rule "rule-name" rem-id <rule-id>
        npfctl rule "rule-name" { list | flush }
        npfctl table <tid> { add | rem | test } <address/mask>
        npfctl table <tid> { list | flush }
        npfctl save | load
        npfctl list [-46hNnw] [-i <ifname>]
```

Any modifications of npf.conf require reloading of the ruleset by performing
a 'reload' command in order to make the changes active.  One difference from
other packet filters is the behaviour of the 'start' and 'stop' commands.
These commands do not actually change (i.e. load or unload) the active
configuration.  Running 'start' will only enable the passing of packets
through NPF, while 'stop' will disable such passing.  Therefore,
configuration should first be activated using the 'reload' command and then
filtering enabled with 'start'.  Similarly, clearing of the active
configuration is done by performing the 'stop' and 'flush' commands.
Such behaviour allows users to efficiently disable and enable filtering
without actually changing the active configuration, as it may be unnecessary.

## Variables

Variables are general purpose keywords which can be used in the ruleset
to make it more flexible and easier to manage.  Most commonly, variables
are used to define one of the following: IP addresses, networks, ports or
interfaces.  A variable can contain multiple elements.

In the example above, network interfaces are defined using the `$ext_if` and
`$int_if` variables (note that the dollar sign ('$') indicates a variable),
which can be used further in the configuration file.

Certain functions can be applied to the interfaces: `inet4()` and `inet6()`.
The functions extract IPv4 or IPv6 addresses respectively.

## Groups

Having one huge ruleset for all interfaces or directions might be
inefficient;  therefore, NPF requires that all rules be defined within
groups.  Groups can be thought of as higher level rules which can contain
subrules.  The main properties of a group are its interface and traffic
direction.  Packets matching group criteria are passed to the ruleset of
that group.  If a packet does not match any group, it is passed to the
default group.  The default group must always be defined.

In the given example, packets passing the _wm0_ network interface will
first be inspected by the rules in the group named "external" and if none
matches, the default group will be inspected.  Accordingly, if the packet is
passing _wm1_, group "internal" will be inspected first, etc.  If the
packet is neither on _wm0_ nor _wm1_, then the default group would be
inspected first.

An important aspect to understand is that the groups (and the `on` keyword
for regular rules) take the symbolic representation of the interface.
They may reference interfaces which not exist at the time of configuration
load.  When the interface with a specified name is attached, the groups/rules
filtering on it will become activate; when the interface is detached, they
become inactive.  Therefore, NPF performs dynamic handling of the interface
arrivals and departures.

// Currently, grouping is done by the interfaces, but extensions are planned
// for abstracting different layers through the groups.

## Rules

Rules, which are the main part of the NPF configuration, describe the criteria
used to inspect and make decisions about packets.  Currently, NPF supports
filtering on the following criteria: interface, traffic direction, protocol,
IP address or network, TCP/UDP port or range, TCP flags and ICMP type/code.
Supported actions are blocking or passing the packet.

Each rule has a priority, which is set according to its order in the
ruleset.  Rules defined first are inspected first.  All rules in the group
are inspected sequentially and the last matching one dictates the action to
be taken.  Rules, however, may be explicitly marked as final.  In such cases,
processing stops after encountering the first matching rule marked as final.
If there is no matching rule in the custom group, then as described previously,
rules in the default group will be inspected.

The rules which block the packets may additionally have `return-rst`,
`return-icmp` or `return` option.  Such option indicates NPF to generate TCP
RST reply for TCP and/or ICMP destination unreachable (administratively
prohibited; type 3, code 13) reply for UDP packets.  These reply packets,
however, have to be passed in the ruleset as NPF will not pass them implicitly.
Such behaviour allows users to apply rule procedures for these reply packets.

Additionally, NPF supports
[pcap-filter(7)](http://man.netbsd.org/cgi-bin/man-cgi?pcap-filter+7+NetBSD-current)
syntax and capabilities, for example:
```
block out final pcap-filter "tcp and dst 10.1.1.252"
```

Virtually any filter pattern can be constructed using this mechanism.

## Tables

A common problem is the addition or removal of many IP addresses for a
particular rule or rules.  Reloading the entire configuration is a
relatively expensive operation and is not suitable for a stream of constant
changes.  It is also inefficient to have many different rules with the same
logic just for different IP addresses.  Therefore, NPF tables are provided
as a high performance container to solve this problem.

NPF tables are containers designed for large IP sets and frequent
updates without reloading the entire ruleset.  They are managed separately,
without reloading of the active configuration.  It can either be done
dynamically or table contents can be loaded from a separate file,
which is useful for large static tables.

The following table types are supported by NPF:

* hash -- provides amortised O(1) lookup time; a good option for sets which
do not change significantly.
* cdb -- constant database which guarantees O(1) lookup; ideal for sets which
change very rarely.
* tree -- provides O(k) lookup and prefix matching support (given a netmask);
a good option when the set changes often and requires prefix matching.

The following fragment is an example using two tables:

```
table <blacklist> type hash file "/etc/npf_blacklist"
table <permitted> type tree dynamic

group "external" on $ext_if {
  block in final from <blacklist>
  pass stateful out final from <permitted>
}
```

The static table identified as "blacklist" is loaded from a file (in this
case, located at `/etc/npf_blacklist`).  The dynamic table is initially empty
and has to be filled once the configuration is loaded.  Tables can be filled
and controlled using the
[npfctl(8)](http://man.netbsd.org/cgi-bin/man-cgi?npfctl+8+NetBSD-current)
utility.  Examples to flush a table, add
an entry and remove an entry from the table:

```
srv# npfctl table "blacklist" flush
srv# npfctl table "permitted" add 10.0.1.0/24
srv# npfctl table "permitted" rem 10.0.2.1
```

A public
[ioctl(2)](http://man.netbsd.org/cgi-bin/man-cgi?ioctl+2+NetBSD-current)
interface for applications to manage the NPF tables is
also provided.

## Rule procedures

Rule procedures are a key interface in NPF, which is designed to perform
custom actions on packets.  Users can implement their own specific
functionality as a kernel module extending NPF.  The NPF extensions will be
discussed thoroughly in the further chapter on
[extensions API](docs/extensions.md).

The configuration file is flexible to accept calls to such procedures
with variable arguments.  Apart from syntax validation, the
[npfctl(8)](http://man.netbsd.org/cgi-bin/man-cgi?npfctl+8+NetBSD-current)
utility has to perform extra checks
while loading the configuration.  It checks whether the custom procedure
is registered in the kernel and whether the arguments of the procedure are
valid (e.g. that the passed values are permitted).  There are built-in rule
procedures provided by NPF, e.g. packet logging and traffic normalisation.

The following is an example of two rule procedure definitions -- one for
logging and another one for normalisation:

```
procedure "log" {
  log: npflog0
}

procedure "norm" {
  normalize: "random-id", "min-ttl" 512, "max-mss" 1432
}
```

Traffic normalisation has a set of different mechanisms. In the example
above, the normalisation procedure has arguments which apply the following
mechanisms: IPv4 ID randomisation, Don't Fragment (DF) flag cleansing,
minimum TTL enforcement and TCP MSS "clamping".

To execute the procedure for a certain rule, use the `apply` keyword:

```
group "external" on $ext_if {
  block in final from <blacklist> apply "log"
}
```

In the case of stateful inspection (when a rule contains the `stateful`
keyword), the rule procedure will be associated with the state i.e.
the connection.
Therefore, a rule procedure would be applied not only for the first packets
which match the rule, but also for all subsequent packets belonging to
the connection. It should be noted that a rule procedure is associated
with the connections for their entire life cycle (until all associated
connections close) i.e. a rule procedure may stay active even if it was
removed from the configuration.

## Application Level Gateways

Certain application layer protocols are not compatible with NAT and require
translation outside layer 3 and 4.  Such translation is performed by the
packet filter extensions called application level gateways (ALGs).  Some
common cases are: traceroute and FTP applications.

Support for traceroute (both ICMP and UDP cases) is built-in, unless NPF
is used from kernel modules.  In that case, kernel module can be autoloaded
though the configuration, e.g. by adding the following line in npf.conf:
```
alg "icmp"
```

Alternatively, ALG kernel module can be loaded manually:
```
modload npf_alg_icmp
```
