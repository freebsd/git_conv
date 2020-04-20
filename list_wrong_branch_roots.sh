#!/bin/sh

n=`sysctl -n hw.ncpu`
n=${n:-4}

git rev-list --all | xargs -n1 -P$n -I@ sh -c '
git ls-tree -r @ | grep -q "[[:space:]]head/" && { echo -n "head/ "; git log -n1 --pretty=format:"%h %ad %N" @; };
git ls-tree -r @ | grep -q "[[:space:]]dist/" && { echo -n "dist/ "; git log -n1 --pretty=format:"%h %ad %N" @; };
' | sed '/^$/d'
