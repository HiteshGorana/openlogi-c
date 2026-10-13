#!/bin/sh
# Exercise real archive verification and file replacement with offline release fixtures.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
fixture=$(mktemp -d "${TMPDIR:-/tmp}/openlogi-install-test.XXXXXX")
running_pid=
trap '[ -z "$running_pid" ] || kill "$running_pid" 2>/dev/null || :; rm -rf "$fixture"' 0
mkdir "$fixture/mock-bin" "$fixture/old" "$fixture/new"
export MOCK_ROOT="$fixture"
export MOCK_RELEASE_DIR="$fixture/new"
export MOCK_MACHINE=arm64 MOCK_NATIVE_ARM=1 MOCK_OS=Darwin MOCK_OS_VERSION=13.0
export PATH="$fixture/mock-bin:$PATH"

cat > "$fixture/mock-bin/uname" <<'SH'
#!/bin/sh
case "$1" in -s) printf '%s\n' "$MOCK_OS" ;; -m) printf '%s\n' "$MOCK_MACHINE" ;; *) exit 1 ;; esac
SH
cat > "$fixture/mock-bin/sw_vers" <<'SH'
#!/bin/sh
printf '%s\n' "$MOCK_OS_VERSION"
SH
cat > "$fixture/mock-bin/sysctl" <<'SH'
#!/bin/sh
printf '%s\n' "$MOCK_NATIVE_ARM"
SH
cat > "$fixture/mock-bin/pgrep" <<'SH'
#!/bin/sh
if [ -n "${MOCK_RUNNING_PID:-}" ]; then printf '%s\n' "$MOCK_RUNNING_PID"; else exit 1; fi
SH
cat > "$fixture/mock-bin/ps" <<'SH'
#!/bin/sh
printf '%s\n' "${MOCK_RUNNING_EXECUTABLE:-}"
SH
cat > "$fixture/mock-bin/launchctl" <<'SH'
#!/bin/sh
case "$1" in
    print) test -f "$MOCK_ROOT/active-service" ;;
    bootout) printf 'bootout\n' >> "$MOCK_ROOT/lifecycle"; rm -f "$MOCK_ROOT/active-service" ;;
    *) exit 1 ;;
esac
SH
cat > "$fixture/mock-bin/curl" <<'SH'
#!/bin/sh
output=/dev/null
while [ "$#" -gt 0 ]; do
    case "$1" in
        --output) output=$2; shift 2 ;;
        --write-out|--proto|--proto-redir|--connect-timeout|--max-time|--retry) shift 2 ;;
        --*) shift ;;
        *) url=$1; shift ;;
    esac
done
printf '%s\n' "$url" >> "$MOCK_ROOT/downloads"
case "$url" in
    */releases/latest) printf 'https://github.com/HiteshGorana/openlogi-c/releases/tag/v0.1.0' ;;
    */SHA256SUMS) cp "$MOCK_RELEASE_DIR/SHA256SUMS" "$output" ;;
    */openlogi-macos-*.tar.gz) cp "$MOCK_RELEASE_DIR/${url##*/}" "$output" ;;
    *) exit 22 ;;
esac
SH
chmod +x "$fixture/mock-bin/"*

make_release() {
    release_dir=$1
    label=$2
    mkdir "$release_dir/bin"
    for tool in openlogid openlogictl openlogi_discover; do
        printf '#!/bin/sh\nrelease=%s\n' "$label" > "$release_dir/bin/$tool"
        cat >> "$release_dir/bin/$tool" <<'SH'
if [ "${1:-}" = autostart ] && [ "${2:-}" = enable ]; then
    printf 'enable-%s\n' "$release" >> "$MOCK_ROOT/lifecycle"
    if [ "$release" = new ] && [ "${MOCK_RESTART_FAIL:-0}" = 1 ]; then exit 1; fi
    touch "$MOCK_ROOT/active-service"
fi
printf '%s\n' "$release"
SH
        chmod +x "$release_dir/bin/$tool"
    done
    for architecture in arm64 x86_64; do
        tar -czf "$release_dir/openlogi-macos-$architecture.tar.gz" -C "$release_dir/bin" \
            openlogid openlogictl openlogi_discover
    done
    (cd "$release_dir" && shasum -a 256 ./*.tar.gz | sed 's|  ./|  |' > SHA256SUMS)
}
make_release "$fixture/old" old
make_release "$fixture/new" new

assert_tools() {
    for tool in openlogid openlogictl openlogi_discover; do
        test -x "$1/bin/$tool"
        test "$("$1/bin/$tool")" = "$2"
    done
}
expect_failure() {
    if "$@" > "$fixture/failure.log" 2>&1; then
        printf 'Expected installer failure: %s\n' "$*" >&2
        exit 1
    fi
}

# Default latest resolution, paths with spaces, and fresh installation without starting a daemon.
install_prefix="$fixture/install with spaces"
sh "$root/install.sh" --prefix "$install_prefix" > "$fixture/install.log"
assert_tools "$install_prefix" new
test ! -f "$fixture/active-service"
grep -q '/releases/download/v0.1.0/openlogi-macos-arm64.tar.gz' "$fixture/downloads"
printf 'PASS: latest Apple Silicon installation\n'

# Intel machines and explicit versions, including Rosetta selecting native ARM binaries.
MOCK_MACHINE=x86_64 MOCK_NATIVE_ARM=0 sh "$root/install.sh" --prefix "$fixture/intel" \
    --version v0.1.0 > "$fixture/intel.log"
assert_tools "$fixture/intel" new
grep -q 'openlogi-macos-x86_64.tar.gz' "$fixture/downloads"
MOCK_MACHINE=x86_64 MOCK_NATIVE_ARM=1 sh "$root/install.sh" --prefix "$fixture/rosetta" \
    --version v0.1.0 > "$fixture/rosetta.log"
assert_tools "$fixture/rosetta" new
grep -q 'for arm64' "$fixture/rosetta.log"
printf 'PASS: Intel and Rosetta architecture selection\n'

# An active installation is stopped, upgraded, and restarted; unrelated files are retained.
MOCK_RELEASE_DIR="$fixture/old" sh "$root/install.sh" --prefix "$fixture/update" \
    --version v0.1.0 > "$fixture/old.log"
printf 'user settings\n' > "$fixture/update/user-settings.toml"
touch "$fixture/active-service"
sh "$root/install.sh" --prefix "$fixture/update" --version v0.1.0 > "$fixture/update.log"
assert_tools "$fixture/update" new
test -f "$fixture/active-service"
test "$(cat "$fixture/update/user-settings.toml")" = 'user settings'
grep -q '^bootout$' "$fixture/lifecycle"
grep -q '^enable-new$' "$fixture/lifecycle"
printf 'PASS: update and service restart\n'

# A failed restart restores the previous executables and restarts the old installation.
MOCK_RELEASE_DIR="$fixture/old" sh "$root/install.sh" --prefix "$fixture/rollback" \
    --version v0.1.0 > "$fixture/rollback-old.log"
touch "$fixture/active-service"
if MOCK_RESTART_FAIL=1 sh "$root/install.sh" --prefix "$fixture/rollback" --version v0.1.0 \
    > "$fixture/rollback.log" 2>&1; then
    printf 'Expected restart failure\n' >&2; exit 1
fi
assert_tools "$fixture/rollback" old
test -f "$fixture/active-service"
grep -q '^enable-old$' "$fixture/lifecycle"
printf 'PASS: failed update rolls back\n'
rm -f "$fixture/active-service"

# A standalone daemon is stopped gracefully before replacement and restarted afterwards.
MOCK_RELEASE_DIR="$fixture/old" sh "$root/install.sh" --prefix "$fixture/standalone" \
    --version v0.1.0 > "$fixture/standalone-old.log"
sleep 60 &
running_pid=$!
MOCK_RUNNING_PID="$running_pid" MOCK_RUNNING_EXECUTABLE="$fixture/standalone/bin/openlogid" \
    sh "$root/install.sh" --prefix "$fixture/standalone" --version v0.1.0 > "$fixture/standalone.log"
if kill -0 "$running_pid" 2>/dev/null; then printf 'Previous daemon is still running\n' >&2; exit 1; fi
wait "$running_pid" 2>/dev/null || :
running_pid=
assert_tools "$fixture/standalone" new
printf 'PASS: standalone daemon update\n'

# Downloads failing checksum verification never replace an existing installation.
mkdir "$fixture/bad"
cp "$fixture/new/"*.tar.gz "$fixture/new/SHA256SUMS" "$fixture/bad/"
printf 'corrupted\n' >> "$fixture/bad/openlogi-macos-arm64.tar.gz"
MOCK_RELEASE_DIR="$fixture/bad" expect_failure sh "$root/install.sh" --prefix "$fixture/rollback" --version v0.1.0
assert_tools "$fixture/rollback" old
grep -q 'Checksum verification failed' "$fixture/failure.log"
printf 'PASS: corrupted archive rejected before installation\n'

# Archive manifests are checked even when the checksum matches.
mkdir "$fixture/unexpected"
printf 'extra\n' > "$fixture/new/bin/unexpected"
tar -czf "$fixture/unexpected/openlogi-macos-arm64.tar.gz" -C "$fixture/new/bin" \
    openlogid openlogictl openlogi_discover unexpected
(cd "$fixture/unexpected" && shasum -a 256 openlogi-macos-arm64.tar.gz > SHA256SUMS)
MOCK_RELEASE_DIR="$fixture/unexpected" expect_failure sh "$root/install.sh" --prefix "$fixture/rollback" --version v0.1.0
assert_tools "$fixture/rollback" old
grep -q 'Unexpected files' "$fixture/failure.log"
printf 'PASS: unexpected archive members rejected\n'

MOCK_OS=Linux expect_failure sh "$root/install.sh" --prefix "$fixture/unsupported"
MOCK_OS_VERSION=12.7 expect_failure sh "$root/install.sh" --prefix "$fixture/unsupported"
expect_failure sh "$root/install.sh" --prefix "$fixture/unsupported" --version 'v0.1.0/../../bad'
test ! -d "$fixture/unsupported"
printf 'PASS: unsupported platforms and invalid versions rejected\n'
printf 'All installer and updater checks passed.\n'
