#!/bin/sh

git=${1:-freebsd-base.git}

# Fix a whole bunch of svn vendor tags, that are created bogusly and result in
# an extra commit that hangs off the mainline branch. See
# https://github.com/freebsd/git_conv/issues/9 for what this looks like and why
# it happens.

rewrite_tag()
{
    local tag
    tag=$1
    export GIT_DIR=$git

    c_auth=`git cat-file tag $tag | sed -n '/^tagger/s/^tagger //; s/ [0-9 +]*$//p'`
    c_date=`git cat-file tag $tag | sed -n '/^tagger/s/^tagger //p' | egrep -o '[0-9]* [+0-9]*$'`
    c_msg=`git cat-file tag $tag | sed '1,/^$/d'`
    c_committer=${c_auth%<*}
    c_email=${c_auth#*<}

    old_commit=`git show -s --format=%h "$tag^{commit}"`

    # Move the tag up to the ancestor
    GIT_COMMITTER_DATE="$c_date" GIT_COMMITTER_NAME="$c_committer" GIT_COMMITTER_EMAIL="$c_email" git tag -a -f -m "$c_msg" ${tag} ${tag}~1
    # NOTE: grabs only the 2nd line (with the tag) and then re-edits the note
    # to drop the extra newline in the middle.
    git notes append -m "`git notes show $old_commit|tail -1`" "$tag^{commit}"
    EDITOR="sed -i'' -e '/^$/d'" git notes edit "$tag^{commit}"
}

git --git-dir=$git for-each-ref --format='%(refname:short)' refs/tags |
    egrep -v "^(backups|release)/" |
    while read rev; do
        tag_tree=`git --git-dir=$git show -s --format=%t "$rev^{}"`
        parent_tree=`git --git-dir=$git show -s --format=%t "$rev~1" 2>/dev/null`
        # parent might not exists, like for example for
        # backups/cvs2svn/which@13122 which is a single, lone commit.
        [ -z "$parent_tree" ] && continue
        if [ $tag_tree = $parent_tree ]; then
            rewrite_tag $rev
        fi
    done
