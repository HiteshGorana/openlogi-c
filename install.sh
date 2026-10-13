#!/bin/sh
# Install or update OpenLogi's prebuilt macOS tools. No compiler or sudo required.
set -eu

repository=https://github.com/HiteshGorana/openlogi-c
version=${OPENLOGI_VERSION:-latest}
prefix=${OPENLOGI_PREFIX:-${HOME:-}/.local}
tools='openlogid openlogictl openlogi_discover'
work_dir=
stage_dir=
changed_tools=
restart_mode=
restart_needed=0
completed=0

die() { printf 'OpenLogi: %s\n' "$*" >&2; exit 1; }
quote() { printf "'"; printf '%s' "$1" | sed "s/'/'\\\\''/g"; printf "'"; }

cleanup() {
    status=$?
    trap - 0 HUP INT TERM
    if [ "$completed" -eq 0 ] && [ -n "$stage_dir" ]; then
        for tool in $changed_tools; do
            if [ -f "$stage_dir/backup/$tool" ]; then
                mv -f "$stage_dir/backup/$tool" "$bin_dir/$tool" || :
            else
                rm -f "$bin_dir/$tool" || :
            fi
        done
        if [ "$restart_needed" -eq 1 ]; then
            if [ "$restart_mode" = service ]; then
                "$bin_dir/openlogictl" autostart enable >&2 || :
            else
                "$bin_dir/openlogid" --daemon >&2 || :
            fi
        fi
    fi
    [ -z "$stage_dir" ] || rm -rf "$stage_dir"
    [ -z "$work_dir" ] || rm -rf "$work_dir"
    exit "$status"
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' HUP TERM

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version|--prefix)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            case "$1" in --version) version=$2 ;; --prefix) prefix=$2 ;; esac
            shift 2 ;;
        -h|--help)
            printf 'Usage: sh install.sh [--version vX.Y.Z] [--prefix directory]\n'
            printf 'Defaults: latest release, $HOME/.local (or OPENLOGI_VERSION / OPENLOGI_PREFIX).\n'
            exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

[ "$(uname -s)" = Darwin ] || die 'Prebuilt releases support macOS only.'
[ -n "${HOME:-}" ] || die 'HOME is not set.'
[ "$(id -u)" -ne 0 ] || die 'Run the installer as your login user, without sudo.'
case "$prefix" in /*) ;; *) die 'The install prefix must be an absolute path.' ;; esac

os_version=$(sw_vers -productVersion)
os_major=${os_version%%.*}
case "$os_major" in ''|*[!0-9]*) die 'Could not determine the macOS version.' ;; esac
[ "$os_major" -ge 13 ] || die 'OpenLogi releases require macOS 13 or newer.'

architecture=$(uname -m)
if [ "$(sysctl -n hw.optional.arm64 2>/dev/null || :)" = 1 ]; then architecture=arm64; fi
case "$architecture" in
    arm64|aarch64) architecture=arm64 ;;
    x86_64) ;;
    *) die "Unsupported architecture: $architecture" ;;
esac
for dependency in curl tar shasum install mktemp launchctl pgrep ps; do
    command -v "$dependency" >/dev/null 2>&1 || die "Required command not found: $dependency"
done

fetch() {
    curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' \
        --connect-timeout 15 --max-time 120 --retry 3 "$@"
}
if [ "$version" = latest ]; then
    printf 'Finding the latest OpenLogi release...\n'
    release_url=$(fetch --output /dev/null --write-out '%{url_effective}' "$repository/releases/latest") ||
        die 'Could not find a published release. Try again after a release is available.'
    case "$release_url" in "$repository"/releases/tag/*) version=${release_url##*/} ;;
        *) die 'GitHub did not return a release tag.' ;; esac
fi
printf '%s\n' "$version" | LC_ALL=C grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$' ||
    die 'Expected a release version such as v0.1.0.'

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openlogi-download.XXXXXX")
archive="openlogi-macos-$architecture.tar.gz"
download_url="$repository/releases/download/$version"
printf 'Downloading OpenLogi %s for %s...\n' "$version" "$architecture"
fetch --output "$work_dir/SHA256SUMS" "$download_url/SHA256SUMS" || die 'Could not download checksums.'
fetch --output "$work_dir/$archive" "$download_url/$archive" || die 'Could not download the release archive.'
awk -v name="$archive" '
    $2 == name {
        if (length($1) != 64 || $1 ~ /[^0-9a-fA-F]/) exit 1
        print $1 "  " name
        count++
    }
    END { if (count != 1) exit 1 }
' "$work_dir/SHA256SUMS" > "$work_dir/checksum" || die 'Missing or invalid archive checksum.'
(cd "$work_dir" && shasum -a 256 -c checksum) || die 'Checksum verification failed; nothing was installed.'

# Release archives contain only these three regular files at their root.
tar -tzf "$work_dir/$archive" > "$work_dir/members" || die 'Invalid release archive.'
LC_ALL=C sort "$work_dir/members" > "$work_dir/actual"
printf '%s\n' $tools | LC_ALL=C sort > "$work_dir/expected"
cmp -s "$work_dir/actual" "$work_dir/expected" || die 'Unexpected files in release archive.'
tar -tvzf "$work_dir/$archive" > "$work_dir/details"
awk 'substr($0, 1, 1) != "-" { exit 1 }' "$work_dir/details" || die 'Archive contains non-regular files.'
mkdir "$work_dir/unpacked"
tar -xzf "$work_dir/$archive" -C "$work_dir/unpacked"

mkdir -p "$prefix/bin" || die "Could not create $prefix/bin. Choose a writable --prefix."
bin_dir=$(cd "$prefix/bin" && pwd -P)
[ -w "$bin_dir" ] || die "Install directory is not writable: $bin_dir"
stage_dir=$(mktemp -d "$bin_dir/.openlogi-install.XXXXXX")
mkdir "$stage_dir/backup"
for tool in $tools; do
    [ ! -d "$bin_dir/$tool" ] || die "A directory already exists at $bin_dir/$tool"
    install -m 755 "$work_dir/unpacked/$tool" "$stage_dir/$tool"
    if [ -e "$bin_dir/$tool" ]; then cp -p "$bin_dir/$tool" "$stage_dir/backup/$tool"; fi
done

# Stop only an existing installation before replacing executable files.
if [ -x "$bin_dir/openlogictl" ] && [ -x "$bin_dir/openlogid" ]; then
    domain="gui/$(id -u)"
    service="$domain/io.github.openlogi.agent"
    if launchctl print "$service" >/dev/null 2>&1; then
        restart_mode=service
        restart_needed=1
        printf 'Stopping the OpenLogi login service for the update...\n'
        launchctl bootout "$service" || die 'Could not stop the login service; update cancelled.'
    else
        # A foreground/background daemon started from this prefix also needs a restart.
        for pid in $(pgrep -u "$(id -u)" -x openlogid || :); do
            executable=$(ps -p "$pid" -o comm= || :)
            process_dir=
            case "$executable" in
                */openlogid) process_dir=$(CDPATH= cd -- "${executable%/*}" 2>/dev/null && pwd -P) || : ;;
            esac
            if [ "$process_dir" = "$bin_dir" ]; then
                restart_mode=daemon
                restart_needed=1
                kill -TERM "$pid" || die 'Could not stop the running daemon.'
                attempts=0
                while kill -0 "$pid" 2>/dev/null; do
                    attempts=$((attempts + 1))
                    [ "$attempts" -le 15 ] || die 'Daemon did not exit; update cancelled.'
                    sleep 1
                done
            fi
        done
    fi
fi

for tool in $tools; do
    changed_tools="$changed_tools $tool"
    mv -f "$stage_dir/$tool" "$bin_dir/$tool"
done
if [ "$restart_needed" -eq 1 ]; then
    if [ "$restart_mode" = service ]; then
        "$bin_dir/openlogictl" autostart enable || die 'Could not restart the service; restoring the previous tools.'
    else
        "$bin_dir/openlogid" --daemon || die 'Could not restart the daemon; restoring the previous tools.'
    fi
fi
completed=1

printf '\nOpenLogi %s installed in %s\n' "$version" "$bin_dir"
printf 'Run this installer again to update to the latest release.\n'
printf '\nGrant openlogid Accessibility and Input Monitoring access in System Settings > Privacy & Security.\n'
printf 'Then start the login service and configure your mouse:\n  '
quote "$bin_dir/openlogictl"; printf ' autostart enable\n  '
quote "$bin_dir/openlogictl"; printf ' setup\n  '
quote "$bin_dir/openlogictl"; printf ' status\n'
case ":${PATH:-}:" in
    *":$bin_dir:"*) ;;
    *) printf '\nAdd the tools to your PATH (and save this in your shell profile):\n  export PATH='
       quote "$bin_dir"; printf ':"$PATH"\n' ;;
esac
