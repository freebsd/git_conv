#!/bin/sh

git=${1:-freebsd-base.git}
export GIT_DIR=$git

# Show merge commits onto mainline, where one of the parents is a single lone
# commit. This is often caused by cvs2svn and we could inline these commits
# into the mainline, which usually brings a much better commit message and a
# more linear history.
#
# needs this alias in your .gitconfig
# children = "!sh -c 'c=${1:-HEAD}; set -- $(git rev-list --all --not \"$c\"^@ --children | grep $(git rev-parse \"$c\") ); shift; echo $*' -"

print_commit()
{
    local c p
    c=$1; shift
    p=$1; shift
    children=`git children $p | xargs -n1`

    # TODO: sometimes the parent might just have 1 other child, namely r13122 (but possibly others)
    echo $children | grep -q $c || { echo "child <-> parent doesn't match, scripting error?" >&2; exit 1; }
    if [ `echo $children | wc -l` = 1 ]; then
	echo -n "$c is merge commit with short parent: $p  notes: "
	echo `git log -n1 --format=%N $c` ← `git log -n1 --format=%N $p`
	# assuming that $c is on master
	svn_path=`git log --format=%N $p | egrep -o 'path=/[^;]*'`
	svn_path=${svn_path#path=}
	svn_rev=`git log --format=%N $p | egrep -o 'revision=[0-9]*'`
	svn_rev=${svn_rev#revision=}
cat << EOS
match $svn_path
  min revision $svn_rev
  max revision $svn_rev
  repository freebsd-base.git
  branch master
end match
EOS
    fi
}

git log --format='%H %P' --all --reverse | awk '{if (NF == 3) { print NF " " $0}}' |
    while read n c p1 p2; do
	# TODO skip commits on /projects et al
	git rev-parse -q --verify $p1\~1 > /dev/null || print_commit $c $p1
	git rev-parse -q --verify $p2\~1 > /dev/null || print_commit $c $p2
    done

