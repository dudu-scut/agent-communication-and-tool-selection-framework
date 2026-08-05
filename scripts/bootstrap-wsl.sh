#!/usr/bin/env bash
# Bootstrap the documented Ubuntu toolchain from a WSL2 Linux filesystem.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

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

for command_name in dpkg-query apt-get grep openssl; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "Missing prerequisite command: $command_name" >&2
    exit 1
  fi
done

REQUIRED_PACKAGES=(
  build-essential cmake pkg-config
  libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc libjsoncpp-dev
  libhiredis-dev libcurl4-openssl-dev libssl-dev uuid-dev
  libgtest-dev librapidcheck-dev
  postgresql-client redis-server nodejs npm openssl
)

missing=()
for package_name in "${REQUIRED_PACKAGES[@]}"; do
  dpkg-query -W -f='${db:Status-Status}' "$package_name" 2>/dev/null | grep -qx installed || missing+=("$package_name")
done

if [ "${#missing[@]}" -gt 0 ]; then
  if ! command -v sudo >/dev/null 2>&1; then
    echo "sudo is required to install missing Ubuntu packages: ${missing[*]}" >&2
    exit 1
  fi
  echo "Installing missing Ubuntu packages: ${missing[*]}"
  sudo apt-get update
  sudo apt-get install -y "${missing[@]}"
else
  echo "All documented Ubuntu packages are installed."
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker CLI is missing; enable Docker Desktop integration for this WSL2 distribution." >&2
  exit 1
fi
if ! docker info >/dev/null 2>&1; then
  echo "Docker Desktop is not reachable from WSL2; enable WSL integration and start Docker Desktop." >&2
  exit 1
fi
if ! docker compose version >/dev/null 2>&1; then
  echo "Docker Compose v2 is required and must be available through the Docker Desktop integration." >&2
  exit 1
fi

CERT_DIR="$PROJECT_ROOT/certs/dev"
CA_KEY="$CERT_DIR/dev-ca-key.pem"
CA_CERT="$CERT_DIR/dev-ca-cert.pem"
TLS_KEY="$CERT_DIR/frontend-key.pem"
TLS_CERT="$CERT_DIR/frontend-cert.pem"
TLS_CSR="$CERT_DIR/frontend.csr"
umask 077
mkdir -p "$CERT_DIR"

if [ ! -s "$CA_CERT" ] || [ ! -s "$CA_KEY" ] || ! openssl x509 -checkend 86400 -noout -in "$CA_CERT" >/dev/null 2>&1; then
  rm -f "$CA_CERT" "$CA_KEY"
  openssl req -x509 -newkey rsa:2048 -nodes -days 30 \
    -keyout "$CA_KEY" -out "$CA_CERT" \
    -subj '/CN=NexusAI Development CA' \
    -addext 'basicConstraints=critical,CA:TRUE,pathlen:0' \
    -addext 'keyUsage=critical,keyCertSign,cRLSign'
fi

certificate_is_valid() {
  local certificate="$1" private_key="$2" required_san="$3"
  [ -s "$certificate" ] && [ -s "$private_key" ] || return 1
  openssl x509 -checkend 86400 -noout -in "$certificate" >/dev/null 2>&1 || return 1
  openssl verify -CAfile "$CA_CERT" "$certificate" >/dev/null 2>&1 || return 1
  local certificate_modulus private_key_modulus
  certificate_modulus="$(openssl x509 -noout -modulus -in "$certificate" 2>/dev/null)" || return 1
  private_key_modulus="$(openssl rsa -noout -modulus -in "$private_key" 2>/dev/null)" || return 1
  [ "$certificate_modulus" = "$private_key_modulus" ] || return 1
  openssl x509 -in "$certificate" -noout -ext subjectAltName 2>/dev/null | grep -Fq "$required_san"
}

if ! certificate_is_valid "$TLS_CERT" "$TLS_KEY" "DNS:localhost"; then
  rm -f "$TLS_CERT" "$TLS_KEY" "$TLS_CSR" "$CA_CERT.srl"
  openssl req -newkey rsa:2048 -nodes -keyout "$TLS_KEY" -out "$TLS_CSR" -subj '/CN=localhost'
  openssl x509 -req -in "$TLS_CSR" -CA "$CA_CERT" -CAkey "$CA_KEY" -CAcreateserial -days 30 -out "$TLS_CERT" \
    -extfile <(printf '%s\n' 'basicConstraints=critical,CA:FALSE' 'keyUsage=critical,digitalSignature,keyEncipherment' 'extendedKeyUsage=serverAuth' 'subjectAltName=DNS:localhost,DNS:frontend,IP:127.0.0.1')
  rm -f "$TLS_CSR" "$CA_CERT.srl"
fi

if ! certificate_is_valid "$TLS_CERT" "$TLS_KEY" "DNS:localhost"; then
  echo "Development TLS certificate validation failed; remove certs/dev and retry." >&2
  exit 1
fi

chmod 600 "$CA_KEY" "$TLS_KEY"
chmod 644 "$CA_CERT" "$TLS_CERT"
echo "WSL2 bootstrap complete; Docker Desktop integration and development TLS are ready."
echo "Development CA: $CA_CERT"
echo "Frontend certificate: $TLS_CERT"
echo "Run: docker compose up --build"
