#!/usr/bin/env bash
# Bootstrap the documented Ubuntu toolchain from a WSL2 Linux filesystem.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REQUIRED_PACKAGES=(
  build-essential cmake pkg-config
  libgrpc++-dev libprotobuf-dev protobuf-compiler libjsoncpp-dev
  libhiredis-dev libcurl4-openssl-dev libssl-dev uuid-dev
  postgresql-client redis-server nodejs npm openssl
)

if [ "$(uname -s)" != "Linux" ] || ! grep -qiE '(microsoft|wsl)' /proc/sys/kernel/osrelease; then
  echo "bootstrap-wsl.sh must run inside WSL2 Ubuntu." >&2
  exit 1
fi

if ! grep -qi 'wsl2' /proc/sys/kernel/osrelease; then
  echo "WSL1 is unsupported; convert the distribution to WSL2 first." >&2
  exit 1
fi

case "$PROJECT_ROOT" in
  /mnt/*)
    echo "Move the repository to the WSL Linux filesystem (for example ~/src/nexusai) before bootstrapping." >&2
    exit 1
    ;;
esac

missing=()
for package_name in "${REQUIRED_PACKAGES[@]}"; do
  dpkg-query -W -f='${db:Status-Status}' "$package_name" 2>/dev/null | grep -qx installed || missing+=("$package_name")
done

if [ "${#missing[@]}" -gt 0 ]; then
  echo "Installing missing Ubuntu packages: ${missing[*]}"
  sudo apt-get update
  sudo apt-get install -y "${missing[@]}"
else
  echo "All documented Ubuntu packages are installed."
fi

CERT_DIR="$PROJECT_ROOT/certs"
if [ ! -f "$CERT_DIR/dev-cert.pem" ] || [ ! -f "$CERT_DIR/dev-key.pem" ]; then
  mkdir -p "$CERT_DIR"
  openssl req -x509 -newkey rsa:2048 -nodes -days 30 \
    -keyout "$CERT_DIR/dev-key.pem" -out "$CERT_DIR/dev-cert.pem" \
    -subj '/CN=localhost'
  chmod 600 "$CERT_DIR/dev-key.pem"
  echo "Generated development certificate in $CERT_DIR."
fi

echo "WSL2 bootstrap complete."
