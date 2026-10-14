#!/bin/sh
set -eu

architecture=$(uname -m)
case "$architecture" in arm64|x86_64) ;; *) printf 'Unsupported architecture: %s\n' "$architecture" >&2; exit 1 ;; esac
mkdir -p dist

# strip changes the Mach-O contents, so sign the final binary afterwards.
codesign --force --sign - --identifier "io.github.openlogi" "bin/openlogi"
codesign --verify --strict "bin/openlogi"
xcrun lipo "bin/openlogi" -verify_arch "$architecture"

bin/openlogi --help >/dev/null
bin/openlogid --help >/dev/null
bin/openlogictl --help >/dev/null
bin/openlogi_discover >/dev/null 2>&1 || true

archive="openlogi-macos-$architecture.tar.gz"
tar -czf "dist/$archive" -C bin openlogi
(cd dist && shasum -a 256 "$archive" > "$archive.sha256")
printf 'Packaged %s\n' "dist/$archive"
