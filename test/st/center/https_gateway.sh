#!/usr/bin/env bash
# Test HTTPS entry for the existing I3 and I8 Linux lab services.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf '%s\n' 'Usage: bash https_gateway.sh CENTER_IPV4' >&2
    exit 2
fi
center_ip=$(python3 -c 'import ipaddress,sys; print(ipaddress.IPv4Address(sys.argv[1]))' "$1")
state="$HOME/.local/state/msmonitor-center-https-lab"
umask 077
mkdir -p -- "$state"
if [[ ! -s "$state/cert.pem" || ! -s "$state/key.pem" ]]; then
    openssl req -x509 -newkey rsa:2048 -nodes -days 7 -subj '/CN=msmonitor-center-lab' \
        -addext "subjectAltName=IP:$center_ip" -keyout "$state/key.pem" -out "$state/cert.pem"
fi
openssl x509 -in "$state/cert.pem" -noout -checkend 60
openssl verify -CAfile "$state/cert.pem" -verify_ip "$center_ip" "$state/cert.pem"
cat "$state/cert.pem" "$state/key.pem" > "$state/server.pem"
cat > "$state/haproxy.cfg" <<EOF
defaults
    mode http
    timeout connect 3s
    timeout client 30s
    timeout server 30s
    timeout http-request 5s

frontend https
    bind ${center_ip}:18469 ssl crt /lab/server.pem ssl-min-ver TLSv1.2
    acl i8_control path /api/v1/daemon/register /api/v1/daemon/heartbeat /api/v1/daemon/poll-commands /api/v1/daemon/ack /api/v1/daemon/submit-summary
    acl i3_write path_reg ^/insert/[0-9]+/prometheus/api/v1/write$
    acl i3_query path_reg ^/select/[0-9]+/prometheus/api/v1/(query|query_range|series|labels)$
    http-request deny deny_status 404 unless i8_control or i3_write or i3_query
    use_backend i8 if i8_control
    default_backend i3

backend i8
    balance roundrobin
    option httpchk
    http-check send meth GET uri /readyz
    http-check expect status 200
    server app1 127.0.0.1:18471 check
    server app2 127.0.0.1:18472 check

backend i3
    server vmauth 127.0.0.1:18467 check
EOF

image=$(docker inspect --format '{{.Image}}' req2-final-l4)
runtime=(--rm --network host --user "$(id -u):$(id -g)" --read-only
         --cap-drop ALL --security-opt no-new-privileges
         --mount "type=bind,source=$state,target=/lab,readonly")
docker run "${runtime[@]}" "$image" haproxy -c -f /lab/haproxy.cfg
docker run -d --name msmonitor-center-https-lab "${runtime[@]}" "$image" haproxy -f /lab/haproxy.cfg
printf 'HTTPS origin: https://%s:18469\nCA file: %s/cert.pem\n' "$center_ip" "$state"
