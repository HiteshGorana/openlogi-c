#!/bin/sh
# Exercise release retries offline: no real GitHub requests or published assets.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
fixture=$(mktemp -d "${TMPDIR:-/tmp}/openlogi-release-test.XXXXXX")
trap 'rm -rf "$fixture"' 0
mkdir -p "$fixture/mock-bin" "$fixture/dist" "$fixture/packaging/macos"
export MOCK_RELEASE_ROOT="$fixture"
export GITHUB_REPOSITORY=example/openlogi-c GITHUB_REF_NAME=v0.1.3
export GITHUB_STEP_SUMMARY="$fixture/summary"
export PATH="$fixture/mock-bin:$PATH"

cat > "$fixture/mock-bin/gh" <<'SH'
#!/bin/sh
set -eu
printf '%s %s\n' "$1" "$2" >> "$MOCK_RELEASE_ROOT/calls"
case "$1 $2" in
    'api graphql')
        if [ "$MOCK_RELEASE_STATE" = error ]; then exit 1; fi
        printf '%s\n' "$MOCK_RELEASE_STATE" ;;
    'release create')
        for flag in --draft --verify-tag --notes-file; do
            case " $* " in *" $flag "*) ;; *) exit 2 ;; esac
        done
        test "${MOCK_RELEASE_FAIL:-}" != create ;;
    'release upload')
        case " $* " in *' --clobber '*) ;; *) exit 2 ;; esac
        for asset in dist/openlogi-macos-arm64.tar.gz dist/openlogi-macos-x86_64.tar.gz dist/SHA256SUMS install.sh; do
            case " $* " in *" $asset "*) ;; *) exit 2 ;; esac
            test -s "$asset"
        done
        test "${MOCK_RELEASE_FAIL:-}" != upload ;;
    'release edit')
        case " $* " in *' --draft=false '*) ;; *) exit 2 ;; esac ;;
    *) exit 2 ;;
esac
SH
chmod +x "$fixture/mock-bin/gh"
cd "$fixture"
printf 'installer\n' > install.sh
printf 'release notes\n' > packaging/macos/release-notes.md
for architecture in arm64 x86_64; do
    printf '%s archive\n' "$architecture" > "dist/openlogi-macos-$architecture.tar.gz"
    (cd dist && shasum -a 256 "openlogi-macos-$architecture.tar.gz" > "openlogi-macos-$architecture.tar.gz.sha256")
done

run_case() {
    export MOCK_RELEASE_STATE=$1 MOCK_RELEASE_FAIL=${2:-}
    : > calls
    : > summary
    sh "$root/scripts/publish_release.sh" > output.log 2>&1
}
assert_calls() {
    printf '%s\n' "$1" > expected
    cmp expected calls
}
expect_failure() {
    if run_case "$@"; then
        printf 'Expected publishing failure: %s\n' "$*" >&2
        exit 1
    fi
}

run_case missing
assert_calls 'api graphql
release create
release upload
release edit'
printf 'PASS: new release publishes only after asset upload\n'

run_case published
assert_calls 'api graphql'
grep -q 'already published' summary
printf 'PASS: published release is preserved on retry\n'

run_case draft
assert_calls 'api graphql
release upload
release edit'
printf 'PASS: interrupted draft can resume\n'

expect_failure error
assert_calls 'api graphql'
expect_failure unknown
assert_calls 'api graphql'
printf 'PASS: lookup failures cannot publish\n'

expect_failure missing create
assert_calls 'api graphql
release create'
expect_failure draft upload
assert_calls 'api graphql
release upload'
printf 'PASS: failed create or upload cannot publish\n'

printf 'corrupted\n' >> dist/openlogi-macos-arm64.tar.gz
expect_failure missing
assert_calls 'api graphql'
printf 'PASS: checksum failure prevents release mutation\n'

mv dist/openlogi-macos-x86_64.tar.gz dist/saved-archive
expect_failure draft
assert_calls 'api graphql'
printf 'PASS: missing architecture prevents release mutation\n'

export GITHUB_REF_NAME=main
expect_failure missing
test ! -s calls
printf 'PASS: invalid version tag is rejected\n'
