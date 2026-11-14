#!/usr/bin/env bash
# The build stamp's master rule, end to end (#508).
#
# A build shows the 2.<commit count> version only when its commit is on
# master's history (og_version_on_master in cmake/OpenGladVersion.cmake);
# every other build shows its hash alone. This drives the real stamp
# generator, `cmake -P cmake/GitHash.cmake`, against throwaway repositories
# and reads the header it writes, line by line:
#
#    1  HEAD is origin/master                   -> on master, "2.1"
#    2  a new commit on top, the SAME stamp     -> rewritten: off master, "2.2"
#    3  origin/master moves up to it            -> on master again
#    4  a branch commit off commit one          -> off master
#    5  OPENGLAD_ON_MASTER=ON on the branch     -> on; OFF on a master commit -> off
#    6  OPENGLAD_ON_MASTER=maybe                -> cmake refuses, naming it
#    7  no origin/master at all                 -> off master
#    8  a shallow clone                         -> off master, "2.0"
#    9  a plain directory, no git               -> off master, "nogit"
#   10  files added, no commit, count and hash passed (the shape the CI test
#       lanes build from)                        -> off master, "2.7", the hash
#   11  only src/core/version.cpp reads the bare number: no other caller of
#       og::version::string() or og::version::minor() under src/ or include/
#
# Usage: test_version_stamp.sh <cmake> <source-root>
# Bash 3.2 safe (the macOS lane): no mapfile, no associative arrays.
set -u

CMAKE="${1:?usage: test_version_stamp.sh <cmake> <source-root>}"
SOURCE_ROOT="${2:?usage: test_version_stamp.sh <cmake> <source-root>}"
GENERATOR="$SOURCE_ROOT/cmake/GitHash.cmake"

ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT
# Every git and cmake call below sees only the throwaway repositories: git
# may not walk up out of the temp root into a checkout that contains it, and
# no global or system configuration of the runner leaks in.
export GIT_CEILING_DIRECTORIES="$ROOT"
export GIT_CONFIG_GLOBAL=/dev/null
export GIT_CONFIG_SYSTEM=/dev/null

failures=0
fail() {
    echo "FAIL case $1: $2" >&2
    failures=$((failures + 1))
}

g() {
    git -c user.name=stamp -c user.email=stamp@example.invalid \
        -c commit.gpgsign=false "$@"
}

# stamp <repo> <out> [OPENGLAD_ON_MASTER [COMMIT_COUNT [GIT_HASH]]]: the same
# command line CMakeLists.txt's og_git_hash_gen runs, empty overrides included.
stamp() {
    "$CMAKE" -DSRC_ROOT="$1" -DOUT_FILE="$2" \
        -DOPENGLAD_ON_MASTER="${3:-}" \
        -DOPENGLAD_COMMIT_COUNT="${4:-}" \
        -DOPENGLAD_GIT_HASH="${5:-}" \
        -P "$GENERATOR"
}

# expect <case> <out> <on_master> <version> <hash>
expect() {
    if ! grep -qxF "#define OPENGLAD_VERSION_ON_MASTER $3" "$2"; then
        fail "$1" "expected OPENGLAD_VERSION_ON_MASTER $3, header was: $(tr '\n' '|' < "$2")"
        return
    fi
    if ! grep -qxF "#define OPENGLAD_VERSION_STRING \"$4\"" "$2"; then
        fail "$1" "expected OPENGLAD_VERSION_STRING \"$4\", header was: $(tr '\n' '|' < "$2")"
        return
    fi
    if ! grep -qxF "#define OPENGLAD_GIT_HASH \"$5\"" "$2"; then
        fail "$1" "expected OPENGLAD_GIT_HASH \"$5\", header was: $(tr '\n' '|' < "$2")"
    fi
}

short() {
    git -C "$1" rev-parse --short=8 HEAD
}

REPO="$ROOT/repo"
OUT="$ROOT/stamp.h"
mkdir -p "$REPO"
g -C "$REPO" -c init.defaultBranch=master init -q
g -C "$REPO" commit -q --allow-empty -m one
FIRST="$(git -C "$REPO" rev-parse HEAD)"
git -C "$REPO" update-ref refs/remotes/origin/master HEAD

# 1: HEAD is origin/master.
stamp "$REPO" "$OUT" >/dev/null || fail 1 "cmake failed"
expect 1 "$OUT" 1 "2.1" "$(short "$REPO")"

# 2: a commit on top of origin/master, stamped into the SAME file: the
# header is rewritten (HEAD moved), and the build is off master now.
g -C "$REPO" commit -q --allow-empty -m two
stamp "$REPO" "$OUT" >/dev/null || fail 2 "cmake failed"
expect 2 "$OUT" 0 "2.2" "$(short "$REPO")"

# 3: origin/master catches up with HEAD.
git -C "$REPO" update-ref refs/remotes/origin/master HEAD
stamp "$REPO" "$OUT" >/dev/null || fail 3 "cmake failed"
expect 3 "$OUT" 1 "2.2" "$(short "$REPO")"

# 4: a branch commit off commit one: its history is not master's.
git -C "$REPO" checkout -q -b topic "$FIRST"
g -C "$REPO" commit -q --allow-empty -m branch
stamp "$REPO" "$OUT" >/dev/null || fail 4 "cmake failed"
expect 4 "$OUT" 0 "2.2" "$(short "$REPO")"

# 5: the workflows' answer outranks git, both ways.
stamp "$REPO" "$OUT" ON >/dev/null || fail 5 "cmake failed (ON)"
expect 5 "$OUT" 1 "2.2" "$(short "$REPO")"
git -C "$REPO" checkout -q master
stamp "$REPO" "$OUT" OFF >/dev/null || fail 5 "cmake failed (OFF)"
expect 5 "$OUT" 0 "2.2" "$(short "$REPO")"

# 6: anything but ON/OFF (or empty) is refused by name.
if bad="$(stamp "$REPO" "$OUT" maybe 2>&1)"; then
    fail 6 "OPENGLAD_ON_MASTER=maybe was accepted"
elif ! printf '%s\n' "$bad" | grep -q "OPENGLAD_ON_MASTER"; then
    fail 6 "the refusal does not name OPENGLAD_ON_MASTER: $bad"
fi

# 7: no origin/master to compare with.
git -C "$REPO" update-ref -d refs/remotes/origin/master
stamp "$REPO" "$OUT" >/dev/null || fail 7 "cmake failed"
expect 7 "$OUT" 0 "2.2" "$(short "$REPO")"

# 8: a shallow clone cannot tell (and counts nothing).
git -C "$REPO" update-ref refs/remotes/origin/master HEAD
SHALLOW="$ROOT/shallow"
git clone -q --depth 1 "file://$REPO" "$SHALLOW"
stamp "$SHALLOW" "$OUT" >/dev/null || fail 8 "cmake failed"
expect 8 "$OUT" 0 "2.0" "$(short "$SHALLOW")"

# 9: no git at all.
PLAIN="$ROOT/plain"
mkdir -p "$PLAIN"
stamp "$PLAIN" "$OUT" >/dev/null || fail 9 "cmake failed"
expect 9 "$OUT" 0 "2.0" "nogit"

# 10: the CI test lanes' source copy (nix/native-ci.nix): a repository with
# files added and no commit, the count and hash passed by the builder.
NOCOMMIT="$ROOT/nocommit"
mkdir -p "$NOCOMMIT"
g -C "$NOCOMMIT" -c init.defaultBranch=master init -q
echo data > "$NOCOMMIT/file.txt"
git -C "$NOCOMMIT" add file.txt
stamp "$NOCOMMIT" "$OUT" "" 7 abcdef01 >/dev/null || fail 10 "cmake failed"
expect 10 "$OUT" 0 "2.7" "abcdef01"

# 11: every player-facing line is composed by og::version from on_master();
# a call site that reads the bare number would show it on any build.
# grep answers 1 for "scanned, no match"; 0 is a caller, and anything else
# (a missing root) means nothing was read, which must not pass as clean.
callers="$(cd "$SOURCE_ROOT" && grep -rnE 'og::version::(string|minor)\(' src include)"
scan_rc=$?
if [ "$scan_rc" -eq 0 ]; then
    fail 11 "the bare version number is read outside src/core/version.cpp: $callers"
elif [ "$scan_rc" -ne 1 ]; then
    fail 11 "could not scan src/ and include/ under $SOURCE_ROOT (grep rc $scan_rc)"
fi

if [ "$failures" -ne 0 ]; then
    echo "test_version_stamp: $failures failure(s)" >&2
    exit 1
fi
echo "PASS: the build stamp shows the version only on master's history"
