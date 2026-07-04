#!/usr/bin/env sh
set -eu

name="easyspot-linux-client"
version="$(cat VERSION)"
output_dir="${1:-$HOME/rpmbuild/SOURCES}"
archive="${output_dir}/${name}-${version}.tar.gz"

mkdir -p "$output_dir"

tar \
  --exclude-vcs \
  --exclude='./.agents' \
  --exclude='./.codex' \
  --exclude='./build' \
  --exclude='./build-*' \
  --exclude='./build_*' \
  --exclude='./cmake-build-*' \
  --exclude='./rpmbuild' \
  --transform "s,^\\.,${name}-${version}," \
  -czf "$archive" .

printf '%s\n' "$archive"
