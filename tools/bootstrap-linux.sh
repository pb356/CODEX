#!/usr/bin/env bash
# Rootless Debian 13 toolchain, verified by APT against Debian archive keys.
set -euo pipefail
base="${DESKTOPBOXES_TOOLCHAIN_BASE:-/workspace/.toolchain}"
mkdir -p "$base/apt/lists/partial" "$base/apt/archives/partial" "$base/apt/conf.d" "$base/apt/sources.d" "$base/root"
if [[ ! -f /usr/share/keyrings/debian-archive-keyring.gpg ]]; then
 echo 'Debian official archive keyring required; signature verification will not be disabled.' >&2
 exit 1
fi
# These are private APT paths; no system sources or installed packages are modified.
cat > "$base/apt/apt.conf" <<CONFIG
Dir::Etc::parts "$base/apt/conf.d";
Dir::Etc::main "$base/apt/no-config";
Dir::Etc::sourcelist "$base/apt/sources.list";
Dir::Etc::sourceparts "$base/apt/sources.d";
Dir::State "$base/apt";
Dir::State::status "$base/apt/status";
Dir::Cache "$base/apt";
APT::Sandbox::User "$(id -un)";
Debug::NoLocking "true";
CONFIG
cat > "$base/apt/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
SOURCES
cp /var/lib/dpkg/status "$base/apt/status"
export APT_CONFIG="$base/apt/apt.conf"
/usr/bin/apt-get update
/usr/bin/apt-get -y --download-only --no-install-recommends install \
 'g++-mingw-w64-x86-64-posix=14.2.0-19+27+b1' 'nsis=3.11-1'
# Extract only the toolchain packages, not optional test tools from a prior task.
for name in binutils-mingw-w64-x86-64 gcc-mingw-w64-base gcc-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix-runtime g++-mingw-w64-x86-64-posix mingw-w64-common mingw-w64-x86-64-dev nsis nsis-common; do
 for pkg in "$base/apt/archives/${name}_"*.deb; do
  [[ -f "$pkg" ]] || { echo "Missing verified package: $name" >&2; exit 1; }
  dpkg-deb -x "$pkg" "$base/root"
 done
done
"$base/root/usr/bin/x86_64-w64-mingw32-g++-posix" --version
NSISDIR="$base/root/usr/share/nsis" "$base/root/usr/bin/makensis" -VERSION
