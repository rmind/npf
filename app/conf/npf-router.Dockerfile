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
RUN mkdir -p /build/bin
COPY . /build/npf

RUN cd /build/npf/app/src && make && \
    DESTDIR="/build/bin" BINDIR="" make install
RUN cp /build/npf/app/run.sh /build/bin/

##########################################################################
# Create a separate NPF-router image.
#
FROM npf AS npf-router
RUN dnf install -y epel-release kernel-modules kernel-modules-extra
RUN dnf install -y net-tools traceroute dpdk libibverbs

WORKDIR /app
COPY --from=npf-dpdk-builder /build/bin/* /app/
