##########################################################################
# NPF + DPDK builder
#
FROM npf AS npf-dpdk-builder
WORKDIR /build

#
# Install the build tools.
#
RUN dnf install -y epel-release kernel-modules kernel-modules-extra
RUN dnf install -y gcc make gdb libasan dpdk-devel libibverbs

#
# Build the application.
#
COPY . /build/npf
#RUN cd /build/npf/app/src && make

##########################################################################
# Create a separate NPF-router image.
#
FROM npf AS npf-router
RUN dnf install -y epel-release kernel-modules kernel-modules-extra
RUN dnf install -y net-tools traceroute dpdk libibverbs

WORKDIR /app
#COPY --from=npf-dpdk-builder /app/* /app/
