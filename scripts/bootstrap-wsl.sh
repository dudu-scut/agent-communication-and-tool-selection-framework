#!/usr/bin/env bash
# Bootstrap the documented Ubuntu toolchain from a WSL2 Linux filesystem.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Ubuntu 26.04 currently ships libpqxx 7.10, whose shared library has a
# process-exit double-free. Keep the workaround deterministic: only this
# exact upstream archive and digest may be installed, and only into the
# project-controlled user prefix below. The prefix is deliberately separate
# from /usr so apt-owned files are never replaced.
readonly NEXUSAI_LIBPQXX_VERSION="8.0.1"
readonly NEXUSAI_LIBPQXX_SOURCE_URL="https://github.com/jtv/libpqxx/archive/refs/tags/${NEXUSAI_LIBPQXX_VERSION}.tar.gz"
readonly NEXUSAI_LIBPQXX_SOURCE_SHA256="24f878a1b4249035e4b6c07d49351506bf99f88df584d36bf198d58ebf293823"
if [ -n "${XDG_DATA_HOME:-}" ]; then
  NEXUSAI_LIBPQXX_DEFAULT_PREFIX="$XDG_DATA_HOME/nexusai/libpqxx-${NEXUSAI_LIBPQXX_VERSION}"
else
  NEXUSAI_LIBPQXX_DEFAULT_PREFIX="${HOME:-$PROJECT_ROOT}/.local/share/nexusai/libpqxx-${NEXUSAI_LIBPQXX_VERSION}"
fi
NEXUSAI_LIBPQXX_PREFIX="${NEXUSAI_LIBPQXX_PREFIX:-$NEXUSAI_LIBPQXX_DEFAULT_PREFIX}"

declare -a NEXUSAI_BOOTSTRAP_TEMP_DIRS=()
cleanup_bootstrap_temp_dirs() {
  local temp_dir
  for temp_dir in "${NEXUSAI_BOOTSTRAP_TEMP_DIRS[@]}"; do
    [ -n "$temp_dir" ] && rm -rf -- "$temp_dir"
  done
}
trap cleanup_bootstrap_temp_dirs EXIT

libpqxx_is_ubuntu_2604() {
  local os_release_file="${NEXUSAI_OS_RELEASE_FILE:-/etc/os-release}"
  local os_id os_version_id
  [ -r "$os_release_file" ] || return 1
  os_id="$(sed -n 's/^ID=//p' "$os_release_file" | head -n 1 | tr -d '"')"
  os_version_id="$(sed -n 's/^VERSION_ID=//p' "$os_release_file" | head -n 1 | tr -d '"')"
  [ "$os_id" = "ubuntu" ] && [ "$os_version_id" = "26.04" ]
}

libpqxx_major_from_version() {
  local version="$1" major
  major="$(printf '%s\n' "$version" | sed -n 's/^\([0-9][0-9]*\)\(\..*\)\?$/\1/p')"
  [ -n "$major" ] || return 1
  printf '%s\n' "$major"
}

libpqxx_system_version() {
  local version="" package_name
  if command -v pkg-config >/dev/null 2>&1; then
    version="$(pkg-config --modversion libpqxx 2>/dev/null || true)"
  fi
  if [ -z "$version" ] && command -v dpkg-query >/dev/null 2>&1; then
    for package_name in libpqxx-dev libpqxx-8.0 libpqxx-7.10 libpqxx-7.8t64 libpqxx-6.4; do
      version="$(dpkg-query -W -f='${Version}' "$package_name" 2>/dev/null || true)"
      if [ -n "$version" ] && [ "$version" != "unknown" ]; then
        break
      fi
      version=""
    done
  fi
  printf '%s\n' "$version"
}

libpqxx_pkg_config_version_for_prefix() {
  local prefix="$1" package_config package_config_dir
  for package_config in \
    "$prefix/lib/pkgconfig/libpqxx.pc" \
    "$prefix/lib64/pkgconfig/libpqxx.pc" \
    "$prefix"/lib/*/pkgconfig/libpqxx.pc; do
    if [ -f "$package_config" ]; then
      package_config_dir="$(dirname "$package_config")"
      PKG_CONFIG_PATH="$package_config_dir${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
        pkg-config --modversion libpqxx
      return
    fi
  done
  return 1
}

validate_libpqxx_prefix() {
  case "$NEXUSAI_LIBPQXX_PREFIX" in
    /*) ;;
    *)
      echo "NEXUSAI_LIBPQXX_PREFIX must be an absolute path: $NEXUSAI_LIBPQXX_PREFIX" >&2
      return 1
      ;;
  esac
  case "$NEXUSAI_LIBPQXX_PREFIX" in
    /usr|/usr/*|/lib|/lib/*|/lib64|/lib64/*|/bin|/bin/*|/sbin|/sbin/*|/etc|/etc/*)
      echo "Refusing to install libpqxx into a system prefix: $NEXUSAI_LIBPQXX_PREFIX" >&2
      return 1
      ;;
  esac
}

libpqxx_prefix_is_valid() {
  local marker="$NEXUSAI_LIBPQXX_PREFIX/.nexusai-libpqxx" version major
  [ -f "$marker" ] || return 1
  grep -Fqx "version=$NEXUSAI_LIBPQXX_VERSION" "$marker" || return 1
  grep -Fqx "source_url=$NEXUSAI_LIBPQXX_SOURCE_URL" "$marker" || return 1
  grep -Fqx "source_sha256=$NEXUSAI_LIBPQXX_SOURCE_SHA256" "$marker" || return 1
  version="$(libpqxx_pkg_config_version_for_prefix "$NEXUSAI_LIBPQXX_PREFIX" 2>/dev/null || true)"
  [ -n "$version" ] || return 1
  major="$(libpqxx_major_from_version "$version" 2>/dev/null || true)"
  [ -n "$major" ] && [ "$major" -ge 8 ]
}

install_pinned_libpqxx() {
  validate_libpqxx_prefix
  if libpqxx_prefix_is_valid; then
    echo "Controlled libpqxx ${NEXUSAI_LIBPQXX_VERSION} prefix is already ready: $NEXUSAI_LIBPQXX_PREFIX"
    return 0
  fi
  if [ -e "$NEXUSAI_LIBPQXX_PREFIX" ]; then
    echo "Refusing to overwrite an existing uncontrolled libpqxx prefix: $NEXUSAI_LIBPQXX_PREFIX" >&2
    echo "Remove it or set NEXUSAI_LIBPQXX_PREFIX to a new empty path, then retry." >&2
    return 1
  fi

  local work_dir archive source_dir build_dir staging_prefix source_archive version major
  work_dir="$(mktemp -d "${TMPDIR:-/tmp}/nexusai-libpqxx.XXXXXX")"
  NEXUSAI_BOOTSTRAP_TEMP_DIRS+=("$work_dir")
  archive="$work_dir/libpqxx-${NEXUSAI_LIBPQXX_VERSION}.tar.gz"

  if [ -n "${NEXUSAI_LIBPQXX_SOURCE_ARCHIVE:-}" ]; then
    source_archive="$NEXUSAI_LIBPQXX_SOURCE_ARCHIVE"
    [ -f "$source_archive" ] || {
      echo "NEXUSAI_LIBPQXX_SOURCE_ARCHIVE is not a file: $source_archive" >&2
      return 1
    }
    cp -- "$source_archive" "$archive"
  else
    command -v curl >/dev/null 2>&1 || {
      echo "curl is required to fetch the pinned libpqxx source archive." >&2
      return 1
    }
    echo "Fetching pinned libpqxx ${NEXUSAI_LIBPQXX_VERSION} source archive."
    curl --fail --location --proto '=https' --tlsv1.2 --retry 3 \
      --output "$archive" "$NEXUSAI_LIBPQXX_SOURCE_URL"
  fi

  printf '%s  %s\n' "$NEXUSAI_LIBPQXX_SOURCE_SHA256" "$archive" | sha256sum --check --strict -

  source_dir="$work_dir/libpqxx-${NEXUSAI_LIBPQXX_VERSION}"
  tar --extract --gzip --no-same-owner --file "$archive" --directory "$work_dir"
  [ -f "$source_dir/CMakeLists.txt" ] || {
    echo "Pinned libpqxx archive did not contain the expected source tree." >&2
    return 1
  }

  build_dir="$work_dir/build"
  staging_prefix="$work_dir/prefix"
  cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$staging_prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTING=OFF \
    -DBUILD_TEST=OFF \
    -DSKIP_BUILD_TEST=ON
  cmake --build "$build_dir" --parallel "${NEXUSAI_LIBPQXX_BUILD_JOBS:-$(nproc 2>/dev/null || printf '1')}"
  cmake --install "$build_dir"

  version="$(libpqxx_pkg_config_version_for_prefix "$staging_prefix" 2>/dev/null || true)"
  major="$(libpqxx_major_from_version "$version" 2>/dev/null || true)"
  if [ -z "$version" ] || [ -z "$major" ] || [ "$major" -lt 8 ]; then
    echo "Pinned libpqxx install did not provide a verifiable >=8 pkg-config version (found: ${version:-unknown})." >&2
    return 1
  fi

  {
    printf 'version=%s\n' "$NEXUSAI_LIBPQXX_VERSION"
    printf 'source_url=%s\n' "$NEXUSAI_LIBPQXX_SOURCE_URL"
    printf 'source_sha256=%s\n' "$NEXUSAI_LIBPQXX_SOURCE_SHA256"
  } > "$staging_prefix/.nexusai-libpqxx"
  mkdir -p "$(dirname "$NEXUSAI_LIBPQXX_PREFIX")"
  mv -- "$staging_prefix" "$NEXUSAI_LIBPQXX_PREFIX"
  libpqxx_prefix_is_valid || {
    echo "Controlled libpqxx prefix verification failed after installation." >&2
    return 1
  }
  echo "Installed and verified libpqxx ${NEXUSAI_LIBPQXX_VERSION} at $NEXUSAI_LIBPQXX_PREFIX"
}

ensure_libpqxx_for_ubuntu_2604() {
  libpqxx_is_ubuntu_2604 || return 0
  local system_version major
  system_version="$(libpqxx_system_version)"
  major="$(libpqxx_major_from_version "$system_version" 2>/dev/null || true)"
  if [ -z "$major" ]; then
    echo "Ubuntu 26.04 libpqxx version could not be determined; refusing to continue." >&2
    return 1
  fi
  if [ "$major" -ge 8 ]; then
    echo "Ubuntu 26.04 already provides libpqxx ${system_version}; no pinned install is needed."
    return 0
  fi
  echo "Ubuntu 26.04 detected libpqxx ${system_version}; installing the pinned >=8 workaround."
  install_pinned_libpqxx
}

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

for command_name in dpkg-query apt-get grep openssl sed head tr sha256sum tar cmake; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "Missing prerequisite command: $command_name" >&2
    exit 1
  fi
done

REQUIRED_PACKAGES=(
  build-essential cmake pkg-config
  libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc libjsoncpp-dev
  libhiredis-dev libcurl4-openssl-dev libssl-dev uuid-dev
  libgtest-dev librapidcheck-dev nlohmann-json3-dev
  libpq-dev golang-go libsodium-dev libpqxx-dev
  postgresql-client redis-server nodejs npm openssl curl
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

# On Ubuntu 26.04, replace only the unsupported stock major (<8) with the
# verified user-prefix build. Ubuntu 24.04 (including its 6.x package) takes
# the no-op path and continues using the system package.
ensure_libpqxx_for_ubuntu_2604

if ! command -v grpcurl >/dev/null 2>&1; then
  if ! command -v sudo >/dev/null 2>&1; then
    echo "sudo is required to install grpcurl via the Go SDK." >&2
    exit 1
  fi
  if ! command -v go >/dev/null 2>&1; then
    echo "Go SDK is missing after installing golang-go; cannot install grpcurl." >&2
    exit 1
  fi

  grpcurl_build_dir="$(mktemp -d)"
  NEXUSAI_BOOTSTRAP_TEMP_DIRS+=("$grpcurl_build_dir")
  if ! GOBIN="$grpcurl_build_dir" go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest; then
    echo "Failed to install grpcurl with the Go SDK." >&2
    exit 1
  fi
  if [ ! -x "$grpcurl_build_dir/grpcurl" ]; then
    echo "Go install did not produce the grpcurl executable." >&2
    exit 1
  fi
  if ! sudo install -m 0755 "$grpcurl_build_dir/grpcurl" /usr/local/bin/grpcurl; then
    echo "Failed to install grpcurl into /usr/local/bin." >&2
    exit 1
  fi
  rm -rf -- "$grpcurl_build_dir"
  if ! command -v grpcurl >/dev/null 2>&1; then
    echo "grpcurl was installed but is not available on PATH." >&2
    exit 1
  fi
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
