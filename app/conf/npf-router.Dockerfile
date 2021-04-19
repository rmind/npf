##########################################################################
# NPF + DPDK builder
#
FROM npf-pkg AS npf-router-dev
WORKDIR /build
COPY . /build/npf

#
# Build the application.
#
RUN cd /build/npf/app/src && \
    make && mkdir -p /build/bin && \
    DESTDIR="/build/bin" BINDIR="" make install
RUN cp /build/npf/app/run.sh /build/bin/

##########################################################################
# Create a separate NPF-router image.
#

FROM centos:centos8 AS npf
RUN dnf install -y epel-release dnf-plugins-core
RUN dnf config-manager --set-enabled PowerTools || \
    yum config-manager --set-enabled powertools || true
RUN dnf install -y kernel-modules kernel-modules-extra dpdk libibverbs
RUN dnf install -y man man-pages net-tools traceroute vim

COPY --from=npf-router-dev /pkg/*.rpm /pkg/
RUN dnf install -y /pkg/*.x86_64.rpm

WORKDIR /app
COPY --from=npf-router-dev /build/bin/* /app/
