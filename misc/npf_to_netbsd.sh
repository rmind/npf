#!/bin/sh

base_netbsd="$1"
set -eu

if [ -z "$base_netbsd" ]; then
	echo "ERROR: ./npf_to_netbsd.sh <base-netbsd-src>" >&2
	exit 1
fi

base_npf="$(dirname $0)/../src"
cd "$base_npf"

# npfkern
scp ./kern/files.npf "$base_netbsd/sys/net/npf/"
scp ./kern/*npf*.{c,h} "$base_netbsd/sys/net/npf/"
scp ./kern/npf-params.7 "$base_netbsd/usr.sbin/npf/"

# npfctl
scp ./npfctl/npf*.{c,h,l,y,5,8} "$base_netbsd/usr.sbin/npf/npfctl/"

# libnpf
scp ./libnpf/libnpf.3 "$base_netbsd/lib/libnpf/"
scp ./libnpf/npf*.{c,h} "$base_netbsd/lib/libnpf/"

# npftest
scp ./npftest/npf*.{c,h,conf} "$base_netbsd/usr.sbin/npf/npftest/"
scp ./npftest/libnpftest/npf*.{c,h} "$base_netbsd/usr.sbin/npf/npftest/libnpftest/"
