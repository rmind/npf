---
name: Bug report
about: Create a report to help us improve
title: ''
labels: bug
assignees: ''

---

### Description

- Please provide a clear and concise problem description here.
- Make sure you explain how to reproduce the problem.
- If applicable, describe the expected behaviour.

### Environment and configuration

Environment:
- NPF environment: [ NPF-Router | bespoke Linux/DPDK application | NetBSD | other ]
- Operating system version: [ distribution/release and output of `uname -a` ]
- NPF version: [ GIT commit, package or release version ]

Configuration:
- If applicable, provide your `npf.conf` configuration.
- If applicable, provide the output of: `npfctl debug -c npf.conf -o npf.nvlist`
- If applicable, describe your network setup and/or logical topology.

### Any additional information

- You can describe a proposed fix, patch or suggestion, if you have one.
- If reporting a crash, try to obtain the stack trace from GDB or other debugger.
- If applicable, attach the relevant tcpdump output or _pcap_ files.
