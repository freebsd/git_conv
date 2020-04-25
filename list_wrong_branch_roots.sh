#!/bin/sh

n=`sysctl -n hw.ncpu`
n=${n:-4}

# ACHTUNG: Do not make the string passed to the shell any longer, because that
# will stop it from working. Not sure why though, the "@" then no longer gets
# replaced.
git rev-list --all --abbrev-commit | xargs -n1 -P$n -I@ sh -c 'x=@;
git ls-tree -r $x|grep -q "	head/" && { echo -n "h "; git log -n1 --pretty=format:"%h %ad %N" $x; };
git ls-tree -r $x|grep -q "	dist/" && { echo -n "d "; git log -n1 --pretty=format:"%h %ad %N" $x; };
' | sed '/^$/d'
