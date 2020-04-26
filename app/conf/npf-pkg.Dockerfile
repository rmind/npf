##########################################################################
# NPF builder -- contains all the dependencies
#
FROM npf-deps AS npf-pkg

#
# - Copy over the source code and build NPF.
# - Build the standalone NPF components.
# - Copy over the NPF packages.
#
COPY . /build/npf
RUN cd /build/npf/pkg && make rpm-libnpf && \
    rpm -ihv RPMS/*/*.rpm && make rpm-npfctl
RUN find /build/npf -name '*.rpm' -exec cp {} /pkg \;
