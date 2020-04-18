#!/bin/sh

set -eu

route del default
route add default gw 10.0.0.2
arping -f 10.0.0.2
ping -i 60 -W 30 1.1.1.1
