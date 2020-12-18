#!/bin/sh
#
# We've rewritten some notes, deleted commits that had a note and we end up
# with 54k notes commits for doc, some 350k for src and 550k for ports. All we
# care about is the final tree of the notes though, so we squash that to cut
# the number of commits per repo in half.

git=${1:-freebsd-base.git}

cd $git

rm -rf notes
git worktree prune
git worktree add -f notes refs/notes/commits

(
  cd notes
  find . -type f -not -name .git | sort > ../notes_all
  case "$git" in
      *doc*)
          git rev-list --all | sed -e 's,^\(..\),./\1/,' | sort > ../revlist_all
          c_date="1607396423 +0000"  # date of the conversion
          ;;
      *base*)
          git rev-list --all | sed -e 's,^\(..\)\(..\),./\1/\2/,' | sort > ../revlist_all
          # for src, due to fix_bogus_tags, the tip of the notes isn't the oldest commit.
          c_date=`git cat-file commit refs/heads/master | sed -n '/^committer/s/^committer //p' | egrep -o '[0-9]* [+0-9]*$'`
          ;;
      *ports*)
          git rev-list --all | sed -e 's,^\(..\)\(..\),./\1/\2/,' | sort > ../revlist_all
          c_date=`git cat-file commit refs/notes/commits | sed -n '/^committer/s/^committer //p' | egrep -o '[0-9]* [+0-9]*$'`
          ;;
  esac
  comm -13 ../revlist_all ../notes_all | xargs rm
  git add -fN .
  tree=`git write-tree`
  commit=`GIT_AUTHOR_DATE="$c_date" GIT_AUTHOR_NAME="svn2git" GIT_AUTHOR_EMAIL="svn2git@FreeBSD.org" GIT_COMMITTER_DATE="$c_date" GIT_COMMITTER_NAME="svn2git" GIT_COMMITTER_EMAIL="svn2git@FreeBSD.org" git commit-tree -m "These are the git notes pointing to SVN revisions of the converted repo." $tree`
  git update-ref refs/notes/commits $commit
)

git worktree remove -f notes
