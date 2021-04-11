##########################################################################
# NPF demo host -- CentOS 8.x image
#
FROM centos:centos8

RUN dnf update -y
RUN dnf install -y epel-release dnf-plugins-core
RUN dnf config-manager --set-enabled PowerTools || \
    yum config-manager --set-enabled powertools || true

RUN dnf install -y man-pages net-tools tcpdump traceroute mtr
RUN dnf install -y nmap-ncat socat nmap telnet curl bind-utils

WORKDIR /app
COPY ./app/run_host.sh /app/
