#!/bin/bash
# Healthy when something listens on the TCP port given as first argument. Reads /proc/net/tcp, so it does not connect
# and the server logs no session for it. The three servers share one network namespace; every port belongs to one server.
port=$(printf '%04X' "${1:?usage: healthcheck.sh PORT}")
grep -qiE ":${port} 00000000:0000 0A " /proc/net/tcp
