#!/bin/bash
set -euo pipefail
umask 077
cd /etc/athena-hodarium/tls
tailscale cert --min-validity=720h --cert-file next.crt --key-file next.key host.example-tailnet.ts.net
if ! cmp -s next.crt server.crt || ! cmp -s next.key server.key; then
  mv next.crt server.crt
  mv next.key server.key
  systemctl try-restart athena-hodarium-authority.service athena-hodarium-relay-public.service athena-hodarium-relay-tailnet.service
else
  rm next.crt next.key
fi
