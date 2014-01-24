#!/bin/sh

# Configures rump kernel networking using rumprun.
# There are two interfaces attached to a single networking
# stack, and routing is done between the two interfaces.

# $RUMP_SERVER is assumed to be an absolute path
export RUMP_SERVER=unix://$(pwd)/$1

cd ../deps/rumprun || { echo failed to go to ../deps/rumprun ; exit 1; }
export LD_LIBRARY_PATH=.:rumpdyn/lib

# first interface
./rumpremote ifconfig snb0 create
./rumpremote ifconfig snb0 inet6 2002::2

# second interface
./rumpremote ifconfig snb1 create
# set MAC address so that it matches selftest.output.expect
./rumpremote ifconfig snb1 link b2:a0:83:85:0f:0f active
./rumpremote ifconfig snb1 inet6 2001::2

#
# global parameters

./rumpremote sysctl -w net.inet6.ip6.forwarding=1

# make sure the router can transmit to the test destination without
# the destination having to answer ndp solicitation requests
# (a non-existing destination usually doesn't do that ;)
./rumpremote ndp -s 2001::1 b2:11:22:33:44:55

#
# display configuration

./rumpremote ifconfig -a
./rumpremote route show

echo '>> rump kernel configured'
