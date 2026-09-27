#!/usr/bin/env bash
#
# Did CI actually BUILD this commit? Prints `true` or `false` on stdout; the reason goes to
# stderr so it lands in the run log.
#
#   usage: bash tools/ci-sha-tested.sh <commit>
#
# `true` means a successful ci.yml run exists for exactly this SHA in which at least one build
# leg ran and passed. A green run is not enough on its own: a run whose `build` job was skipped
# is green too, and trusting it would pass the skip along to every merge built on top of it.
#
# THE SHORTCUTS IN ci.yml ASSUME A PARENT WAS TESTED; THIS IS WHERE THAT GETS CHECKED. A branch
# merged locally with --no-ff and pushed straight to master never had a PR run, so its head was
# never built -- and a merge whose tree equals that head would otherwise skip the matrix and go
# green having compiled nothing.
#
# Anything that cannot be confirmed answers `false`: no repository in the environment (a local
# run), no gh, an API error. Not knowing means building.
#
# Needs GH_TOKEN (the workflow's github.token) and GITHUB_REPOSITORY.
set -uo pipefail

ref="${1:?usage: ci-sha-tested.sh <commit>}"

no() { echo "ci-sha-tested: $* -- not trusted" >&2; echo false; exit 0; }

sha=$(git rev-parse --verify --quiet "$ref^{commit}") || no "$ref is not a commit here"
short=$(git rev-parse --short "$sha")
[ -n "${GITHUB_REPOSITORY:-}" ] || no "no GITHUB_REPOSITORY (not running in Actions)"
command -v gh >/dev/null || no "gh is not installed"

ids=$(gh api "repos/$GITHUB_REPOSITORY/actions/workflows/ci.yml/runs?head_sha=$sha&status=success" \
          --jq '.workflow_runs[].id' 2>/dev/null) || no "could not list the CI runs for $short"

for id in $ids; do
    built=$(gh api "repos/$GITHUB_REPOSITORY/actions/runs/$id/jobs" \
                --jq '[.jobs[] | select(.name != "Classify the change" and .conclusion == "success")] | length' \
                2>/dev/null) || continue
    if [ "${built:-0}" -gt 0 ]; then
        echo "ci-sha-tested: $short was built and passed in run $id" >&2
        echo true
        exit 0
    fi
done

no "$short has no successful CI run that built it"
