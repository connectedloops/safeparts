#!/usr/bin/env bash
# Install the reviewed actionlint release used by CI workflow policy jobs.
set -euo pipefail

version="1.7.12"
platform="linux_amd64"
archive="actionlint_${version}_${platform}.tar.gz"
url="https://github.com/rhysd/actionlint/releases/download/v${version}/${archive}"
checksum="8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8"

if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
  echo "install_actionlint.sh supports Linux x86_64 CI runners only" >&2
  exit 1
fi

install_dir="${1:-${ACTIONLINT_INSTALL_DIR:-$PWD/.actionlint-bin}}"
mkdir -p "$install_dir"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

curl --fail --silent --show-error --location --output "$work_dir/$archive" "$url"
printf '%s  %s\n' "$checksum" "$work_dir/$archive" | sha256sum --check --status
tar -xzf "$work_dir/$archive" -C "$work_dir" actionlint
install -m 0755 "$work_dir/actionlint" "$install_dir/actionlint"
"$install_dir/actionlint" -version
