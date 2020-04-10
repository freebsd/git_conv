#!/bin/sh

svn=${1:-base}
git=${2:-freebsd-base.git}
branch=${3:-master}

# Print empty git commits, where the tree object being pointed to is the same
# as the parent commit, so there is no diff introduced. Print the svn log and
# diff for these revisions too, sometimes they delete directories or touch up
# svn metadata like keywords, mimetypes or mergeinfo.
git --git-dir=$git log --pretty=format:'%t %ad %N' $branch | sed '/^$/d' |
    awk 'BEGIN{parent=0} { if ($1 == parent) { print "Empty commit found: " prev }; parent=$1; prev=$0}' |
    sed -e 's/.*=//' |
    while read rev; do
	svn log -vc$rev file:///$PWD/$svn;
	svn diff -c$rev file:///$PWD/$svn|head;
    done
