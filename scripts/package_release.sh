#!/bin/sh
set -eu

architecture=$(uname -m)
case "$architecture" in arm64|x86_64) ;; *) printf 'Unsupported architecture: %s\n' "$architecture" >&2; exit 1 ;; esac
mkdir -p dist

for tool in openlogid openlogictl openlogi_discover; do
    # strip changes the Mach-O contents, so sign the final binaries afterwards.
    codesign --force --sign - --identifier "io.github.openlogi.$tool" "bin/$tool"
    codesign --verify --strict "bin/$tool"
    lipo -verify_arch "$architecture" "bin/$tool"
done
bin/openlogid --help >/dev/null
bin/openlogictl --help >/dev/null

archive="openlogi-macos-$architecture.tar.gz"
tar -czf "dist/$archive" -C bin openlogid openlogictl openlogi_discover
(cd dist && shasum -a 256 "$archive" > "$archive.sha256")
printf 'Packaged %s\n' "dist/$archive"
