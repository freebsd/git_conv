#!/bin/sh

git=${1:-freebsd-base.git}
export GIT_DIR=$git

# Show vendor tags that have the same tree hash as other commits on the
# mainline branch, this indicates either emtpy commits, or cvs2svn fuck ups.

git for-each-ref --format='%(refname:short)' refs/tags |
    egrep "^vendor" |
    while read ref; do
        dist=${ref%/*}/dist
        # log instead of show here, as it transparently does the right thing
        # w/o need of ^{commit}
        commit=`git log -n1 -s --format=%H $ref`
        tag_tree=`git show -s --format=%T "$ref^{}"`
        # look for the same tree in the full vendor mainline history, sometimes
        # the branchpoint is off, and the same tree happens not in the parent,
        # but a child of the parent.
        other_commit=`git log --format="%H %T" $dist | awk -vt=$tag_tree '$2 == t {print $1}' | head -1`
        if [ -n "$other_commit" -a "$other_commit" != "$commit" ]; then
            echo
            echo "======================================================================="
            echo "$ref seems to share the same tree $tag_tree as $other_commit on $dist but it's not its parent"
            mb=`git merge-base $commit $other_commit`
            if git rev-parse -q --verify $mb~1 >/dev/null; then
                git gnlog ${mb}~1..$commit ${mb}~1..$other_commit
            else
                git gnlog $commit $other_commit
            fi
            svn_rev=`git show -s --format=%N $other_commit | egrep -o "revision=[0-9]*"`
            # This only works if other_commit is actually older, which isn't always the case.
            cat <<EOS
match /`echo $ref | sed 's,/,)/(,g; s/^/(/; s/$/)\//;'`
  repository freebsd-base.git
  branch refs/tags/\1/\2/\3
  branchpoint $dist@${svn_rev#revision=}
  annotated true
end match
EOS
        fi
    done
