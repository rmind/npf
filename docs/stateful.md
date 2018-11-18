# Stateful filtering

TCP is a connection-oriented protocol, which means that network stacks
have a state structure for each connection.  The state is updated during
the connection.  A specific connection is determined by the source and
destination IP addresses, port numbers and the direction of the initial packet.
Additionally, TCP is responsible for reliable transmission, which is
achieved using TCP sequence and window numbers.  Validating the data of each
packet according to the data in the state structure, as well as updating
the state structure, is called TCP state tracking.  Since packet filters are
the middle points between the hosts (i.e. senders and receivers) they have
to perform their own TCP state tracking for each connection in order to
reliably distinguish different TCP connections and perform connection-based
filtering.

Heuristic algorithms are used to handle out-of-order packets, packet losses
and prevent connections from malicious packet injections.  Using the
conceptually same technique, limited tracking of message-based protocols,
mainly UDP and ICMP, can also be done.  Packet filters which have the
described functionality are called _stateful_ packet filters.  For a more
detailed description of the mechanism, one can refer to
[Rooij G., "Real stateful TCP packet filtering in IP Filter",
10th USENIX Security Symposium invited talk, Aug. 2001](
http://www.usenix.org/events/sec01/invitedtalks/rooij.pdf) paper.

NPF is a stateful packet filter capable of tracking TCP connections,
as well as performing limited UDP and ICMP tracking.  Stateful filtering is
enabled using the `stateful` or `stateful-ends` keywords.  The former creates
a state which is uniquely identified by a 5-tuple (source and destination IP
addresses, port numbers and an interface identifier).  The latter excludes
the interface identifier and must be used with precaution.  Once the state is
created, as described in the previous paragraph, all further packets of the
connection are tracked.  Packets in the backwards stream, after having been
confirmed to belong to the same connection, are passed without ruleset
inspection.  Example configuration fragment with stateful rules:

```
group "external" on $ext_if {
  block all
  pass stateful in final proto tcp flags S/SA to $ext_if port ssh
}
```

In this example, all incoming and outgoing traffic on the `$ext_if` interface
will be blocked, with the exception of incoming SSH traffic (with the
destination being an IP address of this interface) and the implicitly
passed backwards stream (outgoing reply packets) of these SSH connections.
Since initial TCP packets opening a connection are SYN packets, such rules
often have additional TCP filter criterion.  The expression `flags S/SA`
extracts SYN and ACK flags and checks that SYN is set and ACK is not.
If there are no `flags` specified, then stateful rules imply `flags S/SAFR`
for the TCP connections, i.e. the rules will pass and create the state only
on TCP connection request (SYN packets).  This is not the case if
[pcap-filter(7)](http://man.netbsd.org/cgi-bin/man-cgi?pcap-filter+7+NetBSD-current)
is used.

---
IMPORTANT: Stateful rules imply `flags S/SAFR` for TCP packets.

---

It is important to understand the implications of "stateful-ends".  Bypassing
the ruleset on other interfaces can have undesirable effects, e.g. a packet
with a spoofed IP address might bypass ingress filtering.  Associating a state
with two interfaces (forwarding case) may also cause problems if the routes
change.  On the other hand, picking up the state on any interface may lead
to higher performance in certain configurations and may also handle some
asymmetric routing cases.  The administrator is free to choose whether
"stateful" or "stateful-ends" is more suitable.

---
WARNING: The `stateful-ends` keyword must be used with precaution.

---

// Connection save/restore
