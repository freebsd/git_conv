#!/bin/sh

# TODO: src, doc, ports, make 9m a variable, etc.

REPO=src
IDLE=18m

# TODO: need 1 clone per src/doc/ports
test -d reaper_repo || git clone --mirror https://git.freebsd.org/$REPO.git reaper_repo

if ! [ -d reaper_repo ]; then
    echo "cloning failed somehow" >&2
    exit 1
fi

cd reaper_repo
git fetch --prune

# Walk all refs, limit to commits done at most $IDLE months ago, print the
# committer-email and snip of the domain part. We're leaving the committer
# login case sensitive for now.
git rev-list --all --since=`date -v -$IDLE +%s` --format=%ce | sed -n '/@freebsd.org/Is/@.*//p' | sort -u > recent_committers

if ! [ -s recent_committers ]; then
    echo "getting recent committers failed" >&2
    exit 1
fi

# Get all current folks in `access`
# TODO: this will move to a separate, single repo for all of doc, ports, src.
git cat-file blob refs/internal/admin:access | awk '$1 !~ /^#/ { print $1 }' | sort -u > all_committers

if ! [ -s all_committers ]; then
    echo "getting all committers failed" >&2
    exit 1
fi

# Folks not in recent committers are:
comm -13 recent_committers all_committers | tee stale_committers

# maybe get the date of last commit for those folks? somehow git rev-list +
# --pretty will insist on spitting out two lines, the full commit hash, then
# custom format.
cat stale_committers | while read c; do
    git rev-list --all --committer=$c@ --date=short --pretty="tformat:%ce %cn %cd %h" -n1 | tail -1
done
