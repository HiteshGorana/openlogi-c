#!/bin/sh
# Publish a complete draft; leave already-published versions unchanged on reruns.
set -eu

: "${GITHUB_REF_NAME:?A release tag is required}"
: "${GITHUB_REPOSITORY:?A repository is required}"
tag=$GITHUB_REF_NAME
printf '%s\n' "$tag" | LC_ALL=C grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$' || {
    printf 'Invalid release tag: %s\n' "$tag" >&2
    exit 1
}

# A missing release is a successful query returning null. Authentication and
# network failures must stop the job, rather than masquerading as "not found".
state=$(gh api graphql \
    -f owner="${GITHUB_REPOSITORY%/*}" -f name="${GITHUB_REPOSITORY#*/}" -f tag="$tag" \
    -f query='query($owner: String!, $name: String!, $tag: String!) {
        repository(owner: $owner, name: $name) {
            release(tagName: $tag) { isDraft }
        }
    }' \
    --jq 'if .data.repository == null then "unknown"
          elif .data.repository.release == null then "missing"
          elif .data.repository.release.isDraft then "draft"
          else "published" end')

case "$state" in
    published)
        message="Release $tag is already published; skipping publication. Use a new version tag for updated binaries."
        printf '%s\n' "$message"
        if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
            printf '%s\n' "$message" >> "$GITHUB_STEP_SUMMARY"
        fi
        exit 0 ;;
    missing|draft) ;;
    *) printf 'Unexpected release state: %s\n' "$state" >&2; exit 1 ;;
esac

for asset in dist/openlogi-macos-arm64.tar.gz dist/openlogi-macos-x86_64.tar.gz \
             install.sh packaging/macos/release-notes.md; do
    test -s "$asset" || { printf 'Missing or empty release file: %s\n' "$asset" >&2; exit 1; }
done
cat dist/openlogi-macos-arm64.tar.gz.sha256 \
    dist/openlogi-macos-x86_64.tar.gz.sha256 > dist/SHA256SUMS
(cd dist && shasum -a 256 --check SHA256SUMS)

if [ "$state" = missing ]; then
    gh release create "$tag" --repo "$GITHUB_REPOSITORY" --draft --verify-tag \
        --title "OpenLogi $tag" --notes-file packaging/macos/release-notes.md
fi

# Retrying an interrupted upload replaces assets only while the release is a draft.
dmgs=$(find dist -name "*.dmg" 2>/dev/null || true)
gh release upload "$tag" --repo "$GITHUB_REPOSITORY" --clobber \
    dist/openlogi-macos-arm64.tar.gz dist/openlogi-macos-x86_64.tar.gz \
    dist/SHA256SUMS install.sh $dmgs
gh release edit "$tag" --repo "$GITHUB_REPOSITORY" --draft=false
