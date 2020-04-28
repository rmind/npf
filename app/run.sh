#!/bin/sh

set -eu

get_eth_addr() { ifconfig $1 | awk '/inet / { print $2 }'; }
get_default_gw() { netstat -nr | grep 'UG[ \t]' | awk '{print $2}'; }

system_configure()
{
	local cfg="/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"
	local val=$(sysctl kernel.randomize_va_space | awk '{print $3}')

	[ "$(cat $cfg)" = "1024" ] || echo 1024 > "$cfg";
	[ "$val" = "0" ] || sysctl -w kernel.randomize_va_space=0;

	mount | grep -q ' /mnt/huge ' || {
		mkdir -p /mnt/huge && mount -t hugetlbfs nodev /mnt/huge;
	}

	modprobe sch_multiq
}

router_configure()
{
	[ ! -f /etc/npf-router.conf ] || return 0;

	echo "ifconfig dtap0 $(get_eth_addr eth0)" >> /etc/npf-router.conf
	echo "ifconfig dtap1 $(get_eth_addr eth1)" >> /etc/npf-router.conf
	echo "route 0.0.0.0/0 dtap0 $(get_default_gw)" >> /etc/npf-router.conf
	echo "route 10.0.0.0/24 dtap1" >> /etc/npf-router.conf

	cat > /etc/npf.conf <<-EOF
	\$ext_if = dtap0
	\$int_if = dtap1

	map \$ext_if dynamic 10.0.0.0/24 -> $(get_eth_addr eth0)

	group "external" on \$ext_if {
	  pass stateful out final all
	}

	group "internal" on \$int_if {
	  pass final all
	}

	group default {
	  block all
	}
	EOF
	npfctl debug /etc/npf.conf /tmp/npf.nvlist | tail -n1
}

run()
{
	/app/npf_router \
	    --vdev=net_tap0,remote=eth0 \
	    --vdev=net_tap1,remote=eth1
}

system_configure
router_configure
run
