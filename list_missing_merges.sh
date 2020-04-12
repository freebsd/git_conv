#!/bin/sh

svn=${1:-base}
git=${2:-freebsd-base.git}
branch=${3:-master}

# Print the potential MFV merge commits that have only a single parent (by
# checking whether the 2nd parent field looks more like a date.
git --git-dir=$git log --pretty=format:'%h %p %ad %N' --grep="^MF[VPp]" $branch | sed '/^$/d' |
    awk '$3 ~ /....-..-../ {print $0}'
