#!/usr/bin/env bash
set -euo pipefail
if [[ $EUID -ne 0 ]]; then echo "Run with sudo" >&2; exit 1; fi
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y python3-venv ffmpeg alsa-utils icecast2 avahi-daemon nginx openssl
id cdtosonos &>/dev/null || useradd --system --home /var/lib/cdtosonos --create-home --groups audio cdtosonos
install -d -o cdtosonos -g audio /opt/cdtosonos /var/lib/cdtosonos /etc/cdtosonos
python3 -m venv /opt/cdtosonos/venv
/opt/cdtosonos/venv/bin/pip install --upgrade pip
/opt/cdtosonos/venv/bin/pip install "$ROOT"
for name in source relay admin; do secret="$(openssl rand -hex 24)"; printf '%s' "$secret" > "/etc/cdtosonos/icecast-${name}-password"; chmod 640 "/etc/cdtosonos/icecast-${name}-password"; chown cdtosonos:audio "/etc/cdtosonos/icecast-${name}-password"; done
sed -e "s/__SOURCE_PASSWORD__/$(cat /etc/cdtosonos/icecast-source-password)/" -e "s/__RELAY_PASSWORD__/$(cat /etc/cdtosonos/icecast-relay-password)/" -e "s/__ADMIN_PASSWORD__/$(cat /etc/cdtosonos/icecast-admin-password)/" "$ROOT/deploy/icecast.xml.template" > /etc/icecast2/icecast.xml
install -m 0644 "$ROOT/deploy/cdtosonos.service" /etc/systemd/system/cdtosonos.service
install -m 0440 "$ROOT/deploy/cdtosonos-sudoers" /etc/sudoers.d/cdtosonos
install -m 0644 "$ROOT/deploy/nginx-cdtosonos.conf" /etc/nginx/sites-available/cdtosonos
ln -sf /etc/nginx/sites-available/cdtosonos /etc/nginx/sites-enabled/cdtosonos
rm -f /etc/nginx/sites-enabled/default
nginx -t
systemctl daemon-reload
systemctl enable --now avahi-daemon icecast2 cdtosonos nginx
systemctl restart icecast2 cdtosonos nginx
echo "Installed. Open http://cdtosonos.local and create the administrator account."
