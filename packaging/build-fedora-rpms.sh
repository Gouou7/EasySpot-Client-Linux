#!/usr/bin/env sh
set -eu

name="easyspot-linux-client"
version="$(cat VERSION)"
topdir="${RPMBUILD_TOPDIR:-$HOME/rpmbuild}"
artifacts_dir="${1:-packaging/artifacts}"

shift || true

if [ "$#" -eq 0 ]; then
  set -- fedora-40-x86_64 fedora-41-x86_64 fedora-42-x86_64 fedora-43-x86_64 fedora-44-x86_64
fi

command -v rpmbuild >/dev/null 2>&1 || {
  printf '%s\n' "rpmbuild is required. Install rpm-build first." >&2
  exit 1
}

command -v mock >/dev/null 2>&1 || {
  printf '%s\n' "mock is required. Install mock and add your user to the mock group first." >&2
  exit 1
}

mkdir -p "$topdir/SOURCES" "$topdir/SRPMS" "$artifacts_dir"

./packaging/make-source-tarball.sh "$topdir/SOURCES" >/dev/null
rpmbuild -bs --define "_topdir $topdir" packaging/"$name".spec

srpm="$topdir/SRPMS/$name-$version-1"*.src.rpm

for chroot in "$@"; do
  printf 'Building %s in %s\n' "$name" "$chroot"
  mock -r "$chroot" --resultdir "$artifacts_dir/$chroot" rebuild $srpm
done

printf 'RPM artifacts written under %s\n' "$artifacts_dir"
