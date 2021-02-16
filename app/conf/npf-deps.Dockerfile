##########################################################################
# NPF and dependency builder -- CentOS 8.x image
#
FROM centos:centos8 AS npf-builder
WORKDIR /build

# Install/enable EPEL and Power Tools repositories.
RUN dnf install -y epel-release dnf-plugins-core
RUN dnf config-manager --set-enabled PowerTools || \
    yum config-manager --set-enabled powertools || true
RUN dnf install -y net-tools man-pages

#
# Install all the packages for building.
#
RUN dnf install -y gcc make gdb rpm-build libasan libubsan lua-devel
RUN dnf install -y libtool byacc flex jemalloc-devel
RUN dnf install -y libpcap libpcap-devel openssl-libs openssl-devel
RUN dnf install -y git subversion

# Install DPDK dependencies (to caching for the build).
RUN dnf install -y kernel-modules kernel-modules-extra dpdk-devel libibverbs

# Make it work with unprivileged container.
ENV LSAN_OPTIONS=detect_leaks=false

# nvlist
RUN git clone https://github.com/rmind/nvlist
RUN cd nvlist/pkg && make rpm && rpm -ihv RPMS/*/*.rpm
# libqsbr
RUN git clone https://github.com/rmind/libqsbr
RUN cd libqsbr/pkg && make rpm && rpm -ihv RPMS/*/*.rpm
# libthmap
RUN git clone https://github.com/rmind/thmap
RUN cd thmap/pkg && make rpm && rpm -ihv RPMS/*/*.rpm
# liblpm
RUN git clone https://github.com/rmind/liblpm
RUN cd liblpm/pkg && make rpm && rpm -ihv RPMS/*/*.rpm
# rhashmap
RUN git clone https://github.com/rmind/rhashmap
RUN cd rhashmap/pkg && make rpm && rpm -ihv RPMS/*/*.rpm
# bpfjit
RUN git clone https://github.com/rmind/bpfjit
RUN cd bpfjit && make rpm && rpm -ihv RPMS/*/*.rpm
# libcdb
RUN git clone https://github.com/rmind/libcdb
RUN cd libcdb && make rpm && rpm -ihv RPMS/*/*.rpm

# Copy all RPMs.
WORKDIR /pkg
RUN find /build -name '*.rpm' -exec cp {} /pkg \;
