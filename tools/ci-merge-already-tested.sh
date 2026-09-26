#!/usr/bin/env bash
#
# Has CI already tested exactly this tree? Prints `true` or `false` on stdout; the reason
# goes to stderr so it lands in the run log.
#
#   usage: bash tools/ci-merge-already-tested.sh <github.event_name>
#
# A MERGE WHOSE TREE EQUALS ITS SECOND PARENT CHANGED NO FILES. Merging an up-to-date PR
# branch into master produces a commit whose tree is byte-identical to the PR head that CI
# passed minutes earlier -- and a PR is only merged on green. Re-running the matrix on it
# compiles the same sources on three operating systems to reach the same answer; measured,
# that was about half of all CI runner-minutes (altairsim issue #543).
#
# This compares CONTENT, not paths. Nothing is judged safe to skip; the bytes are the same.
# Every other case builds:
#   - a pull_request run, or a manual workflow_dispatch -- somebody asked for an answer
#   - a push whose HEAD is not a merge (a squash, a direct commit, the PDF bot)
#   - a merge that DID change the tree -- one resolving conflicts, or one landing on a
#     master that moved underneath the branch. That is the case that needs a run.
#
# Needs the history present: check out with fetch-depth: 0 (both callers already do).
set -uo pipefail

event="${1:-}"

no() { echo "ci-merge-already-tested: $* -- building" >&2; echo false; exit 0; }

[ "$event" = "push" ] || no "event is '${event:-none}', not a push"
git rev-parse --verify --quiet 'HEAD^2' >/dev/null || no "HEAD is not a merge commit"
git diff --quiet 'HEAD^2' HEAD || no "the merge changed files relative to the PR head"

echo "ci-merge-already-tested: merge introduced no file changes -- the PR run already tested this tree ($(git rev-parse --short 'HEAD^2'))" >&2
echo true
