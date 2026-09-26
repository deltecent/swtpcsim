#!/usr/bin/env bash
#
# Did anything but documentation change between <base> and HEAD? Prints `true` (code changed:
# build all three platforms) or `false` (documentation only: the Linux leg is enough); the
# reason goes to stderr so it lands in the run log.
#
#   usage: bash tools/ci-changed-code.sh <base-commit>
#
# ci.yml's `scope` job decides which base(s) to ask about; this script only sorts paths. It is
# a file rather than inline YAML so the exact rule CI applies can be run against any commit
# locally.
#
# Documentation = anything under docs/, reference/ or .claude/, a top-level *.md, or LICENSE.
# Everything else is code -- src, tests, cmake, the data dirs the tests read (roms, tapes,
# machines, disks, cpm, examples), tools, and the workflows themselves. An unrecognized path is
# code, so a new directory is never silently skipped.
#
# .claude/ IS DOCUMENTATION even though it is prose in a nested directory. The skills there are
# instructions for an agent; nothing in them is compiled, and no test's result depends on the
# platform it is read on. Without this case a skill edit falls through to the */*.md rule below
# and builds three operating systems to prove a Markdown file did not change the binary.
#
# Needs the base present: check out with fetch-depth: 0.
set -euo pipefail

base="${1:?usage: ci-changed-code.sh <base-commit>}"

while IFS= read -r f; do
    [ -z "$f" ] && continue
    case "$f" in
        docs/*|reference/*|.claude/*|LICENSE) continue ;;  # documentation
        */*.md) ;;                        # a .md deeper in the tree (e.g. tests/) is code
        *.md) continue ;;                 # a top-level .md (README, DESIGN) is documentation
        *) ;;                             # anything else is code
    esac
    echo "ci-changed-code: $f changed since $(git rev-parse --short "$base") -- code" >&2
    echo true
    exit 0
done < <(git diff --name-only "$base" HEAD)

echo "ci-changed-code: documentation only since $(git rev-parse --short "$base")" >&2
echo false
