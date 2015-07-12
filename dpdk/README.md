# NPF + DPDK

NPF is fully integrated with Intel DPDK framework.  The following steps
describe how to build and run a quick NPF + DPDK demo.

## Install dependencies

- Install proplib RPM package.

	TBD

- Install libcdb RPM package:

	git clone https://github.com/rmind/libcdb
	cd libcdb && make rpm && rpm -ihv RPMS/x86_64/libcdb-*

- Install and setup DPDK:

	See http://dpdk.org/doc/guides/linux_gsg/build_dpdk.html
	Ensure huge pages memory is reserved.

## Build and install NPF

	git clone https://github.com/rmind/npf
	cd npf/pkg && make rpm && rpm -ihv RPMS/x86_64/npf-*

## Build NPF+DPDK demo program:

	cd npf/dpdk
	export RTE_SDK=...
	make

## Run the program

	sudo ./build/npf_dpdk_demo -c1 -n1

	This test will generate and process ~1 million packets.
