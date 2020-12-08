#!/bin/sh
#
# The converter rewrites all the tags at the end (as they might have been
# advanced in SVN). We mostly stop that from happening via rules, but we still
# end up with refs/notes/commits that has:
# - commit 1 .. N
# - all the tags
# and running that incrementally, with N going to N+1 means all the later
# commits are different on that "branch" and they need a force push. So let's
# rewrite them chronologically. Their content is pretty simple, so there's a
# chance this will not result in merge conflicts.
#
# Hat tip to https://stackoverflow.com/questions/27245008/reorder-git-commit-history-by-date
#
# NOTE: this will do about 10 commits per second. You have been warned.

git=${1:-freebsd-base.git}
branch=${2:-refs/notes/commits}

echo "Sorting $branch commits started at" `date +"%F %T"`
cd $git
rm -rf notes-sort
git worktree prune
git worktree add notes-sort refs/notes/commits
git config --global log.date iso
# This list can be spot checked for duplicate timestamps, which would result in
# non-deterministic ordering of the picks later on.
# NOTE: non-numeric sort, so we can sort on the ISO timestamp + message, because we have dupes for that :/
git log --pretty='%H %at %ad %f %t' --reverse refs/notes/commits | sort -k3,6 -s > rebase_list_human
cat rebase_list_human | awk '{ print "pick "$1 }' > rebase_list

(
  cd notes-sort
  # sadly cannot use --committer-date-is-author-date :( and wouldn't be enough anyway
  EDITOR="sed -i.bak -n -e '1r ../rebase_list'" git rebase -i --root --strategy=recursive --strategy-option=ours
  if [ $? = 0 ]; then
      env FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch --env-filter 'export GIT_COMMITTER_DATE="$GIT_AUTHOR_DATE" GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME" GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"' HEAD
      git update-ref refs/notes/commits HEAD
      git update-ref -d refs/original/HEAD
  fi
)
git worktree remove notes-sort

echo "Sorting $branch commits ended at" `date +"%F %T"`
