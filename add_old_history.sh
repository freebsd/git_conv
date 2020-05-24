#!/bin/sh

# Fetch old (pre-FreeBSD) Unix history and link it to the FreeBSD
# commit graph.

set -e

# History repo
HISTORY=${1:-https://github.com/dspinellis/unix-history-repo}

# Verify that this is a suitable FreeBSD repository
if ! git log -n 1 origin/release/3.0.0 >/dev/null 2>&1 ; then
  echo "Unable to find origin/release/3.0.0 branch. Not a FreeBSD repo?" 1>&2
  exit 1
fi

# Add the history repo as a remote, if needed
if ! git remote get-url history >/dev/null 2>&1 ; then
  git remote add history "$HISTORY"
fi

# Fetch the history from the added remote repo, if needed
if ! git log -n 1 history/FreeBSD-release/3.0.0 >/dev/null 2>&1 ; then
  git fetch history
fi

# Replace a commit on the FreeBSD repo trunk where the 3.0 and 4.0 branches
# meet with the same commit from the history repo.
# This will make git log, blame, and friends to continue on the
# history repo from that point back.
# The replace is made at that point rather than earlier, because the
# history repo uses special techniques to handle the wholesale imports
# of snapshots, which were common before that time.
# In contrast, the FreeBSD repo doesn't (and shouldn't) use these
# techniques (a hidden shadow reference copy directory).
git replace -f $(git merge-base origin/release/3.0.0 origin/release/4.0.0) \
  $(git merge-base history/FreeBSD-release/3.0.0 history/FreeBSD-release/4.0.0)
