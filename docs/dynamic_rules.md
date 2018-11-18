# Dynamic rules

NPF has support for dynamic rules which can be added or removed to a given
ruleset without reloading the entire configuration.  Consider the following
fragment:
```
group default {
  ruleset "test-set"
}
```

Dynamic rules can be managed using
[npfctl(8)](http://man.netbsd.org/cgi-bin/man-cgi?npfctl+8+NetBSD-current):
```
$ npfctl rule "test-set" add block proto icmp from 192.168.0.6
OK 1
$ npfctl rule "test-set" list
block proto icmp from 192.168.0.6
$ npfctl rule "test-set" add block from 192.168.0.7
OK 2
$ npfctl rule "test-set" list
block proto icmp from 192.168.0.6
block from 192.168.0.7 
$ npfctl rule "test-set" rem block from 192.168.0.7
$ npfctl rule "test-set" rem-id 1
$ npfctl rule "test-set" list
```

Each rule gets a unique identifier which is returned on addition.  The
identifier should be considered as alphanumeric string.  As shown in the
example, there are two methods to remove a rule:

* Using a unique identifier (`rem-id` command).
* Passing the exact rule and using a hash computed on a rule (`rem` command).

In the second case, SHA1 hash is computed on a rule to identify it.  Although
very unlikely, it is subject to hash collisions.  For a fully reliable and
more efficient way, it is recommended to use the first method.
