#!/bin/sh

# Script to replace all release tags with checked out copies of the source
# code. This brings
# a) proper CVS IDs for people that care, but most importantly
# b) undoes the damage that repo-copies and moves did to the repo,
#    at least for those final releases.
#
# Same could be done for ports, but there is very little archaeological value
# in there, so it is left as an exercise for the reader.

: ${BASE=${PWD}}

SOURCE=ftp://ftp-archive.freebsd.org/pub/FreeBSD-Archive/old-releases/i386
TYPE=${1:-base}
REPO=${2:-$BASE/freebsd-base.git}

fetch_archive() {
    local r dest
    r=$1; shift
    dest=$1; shift

    set -e
    (
      cd $dest
      case "$r" in
	  9*|10*|11*|12*|13*)
	      fetch $SOURCE/$r/src.txz
	      ;;
          4.6-RELEASE)
              # slib is damaged under i386/
	      wget -nH -nd -r -N --progress=dot ${SOURCE%i386}alpha/$r/src/
	      ;;
	  *)
	      wget -nH -nd -r -N --progress=dot $SOURCE/$r/src/
	      ;;
      esac
    )
    set +e
}

extract() {
    local r dest
    r=$1; shift
    dest=$1; shift

    set -e
    (
      case "$r" in
	  9*|10*|11*|12*|13*)
	      tar xf src.txz -s',^usr/src/,,' -C wrk
	      ;;
	  *)
	      for f in *.aa; do
		  cat ${f%.aa}.?? | tar xf - -C wrk
	      done
	      ;;
      esac
      # clean up some leftover schmutz
      find wrk -name CVS -type d -exec rm -r {} +
    )
    set +e
}

checkout_and_tag() {
    local dest rel tag
    rel=$1; shift
    dest=$1; shift

    set -e

    case $rel in
        *.*.*-RELEASE) tag=${rel%-RELEASE} ;;
        *.*-RELEASE) tag=${rel%-RELEASE}.0 ;;
    esac

    cd $dest
    GIT_DIR=$REPO git worktree add --no-checkout wrk release/$tag
    extract $rel $dest
    cd wrk

    # Grab commit metadata from the original annotated tag
    #X#c_auth=`git show -s --format=medium release/$tag\^{tag} | sed -n '/^Tagger:/s/^Tagger: //p'`
    #X#c_committer=${c_auth%<*}
    #X#c_email=${c_auth#*<}
    #X## git show tag is stupid and wants to show me the commit data, not just the tag data, ugh.
    #X#c_date=`TZ=UTC svn log -l 1 --xml file:///$BASE/base/release/$tag | sed -n -e 's,</*date>,,gp'`
    #X#c_msg=`svn log -l 1 file:///$BASE/base/release/$tag | sed -e '1,3d; $d'`
    c_auth=`git cat-file tag release/$tag | sed -n '/^tagger/s/^tagger //; s/ [0-9 +]*$//p'`
    c_committer=${c_auth%<*}
    c_email=${c_auth#*<}
    c_date=`git cat-file tag release/$tag | sed -n '/^tagger/s/^tagger //p' | egrep -o '[0-9]* [+0-9]*$'`
    c_msg=`git cat-file tag release/$tag | sed '1,/^$/d'`

    msg="This commit was manufactured to restore the state of the $rel image.
Releases prior to 5.3-RELEASE are omitting the secure/ and crypto/ subdirs."
    #X## TODO: with the git worktree support, I can probably just use a regular
    #X## old git commit instead?
    #X#find . -type f -not -name .git | xargs git update-index --add
    #X#tree=`git write-tree`
    #X#parent=`git show -s --format=%h release/${tag}\^{commit}`
    #X## TODO: hoist tagger into author, fix up commit dates
    #X#commit=`git show -s --pretty=%B release/$tag | \
    #X#    git commit-tree -p $parent -F - -m "$msg" $tree`
    git add -N .
    GIT_COMMITTER_DATE="$c_date" GIT_COMMITTER_NAME="$c_committer" GIT_COMMITTER_EMAIL="$c_email" git commit -q -a --author="$c_auth" --date="$c_date" -m "$c_msg" -m "$msg"
    # TODO: what's in a name?
    GIT_COMMITTER_DATE="$c_date" GIT_COMMITTER_NAME="$c_committer" GIT_COMMITTER_EMAIL="$c_email" git tag -a -f -m "Tag $rel as shipped." release/${tag}_shipped $commit

    cd ../..
    ##GIT_DIR=$PWD/.git git worktree remove --force wrk
    #rm -rf wrk
    set +e
}

case "$TYPE" in
    base)
            #1.0.0-RELEASE   # nope
            #2.1.7.1-RELEASE # we don't have the CVS or SVN tag for it?
            #2.2.6-RELEASE   # has nothing under src/ on the archive server
            #2.2.9-RELEASE   # not in SVN
            #3.5.1-RELEASE   # not in SVN
            #12.1-RELEASE    # not on the mirror yet
        set -- \
            2.0.5-RELEASE \
            2.1.5-RELEASE \
            2.1.7-RELEASE \
            2.2.2-RELEASE \
            2.2.5-RELEASE \
            2.2.7-RELEASE \
            2.2.8-RELEASE \
            3.0-RELEASE \
            3.1-RELEASE \
            3.2-RELEASE \
            3.3-RELEASE \
            3.4-RELEASE \
            3.5-RELEASE \
            4.0-RELEASE \
            4.1-RELEASE \
            4.1.1-RELEASE \
            4.2-RELEASE \
            4.3-RELEASE \
            4.4-RELEASE \
            4.5-RELEASE \
            4.6-RELEASE \
            4.6.2-RELEASE \
            4.7-RELEASE \
            4.8-RELEASE \
            4.9-RELEASE \
            4.10-RELEASE \
            4.11-RELEASE \
            5.0-RELEASE \
            5.1-RELEASE \
            5.2-RELEASE \
            5.2.1-RELEASE \
            5.3-RELEASE \
            5.4-RELEASE \
            5.5-RELEASE \
            6.0-RELEASE \
            6.1-RELEASE \
            6.2-RELEASE \
            6.3-RELEASE \
            6.4-RELEASE \
            7.0-RELEASE \
            7.1-RELEASE \
            7.2-RELEASE \
            7.3-RELEASE \
            7.4-RELEASE \
            8.0-RELEASE \
            8.1-RELEASE \
            8.2-RELEASE \
            8.3-RELEASE \
            8.4-RELEASE \
            9.0-RELEASE \
            9.1-RELEASE \
            9.2-RELEASE \
            9.3-RELEASE \
            10.0-RELEASE \
            10.1-RELEASE \
            10.2-RELEASE \
            10.3-RELEASE \
            10.4-RELEASE \
            11.0-RELEASE \
            11.1-RELEASE \
            11.2-RELEASE \
            12.0-RELEASE
        for r; do
            dest=archive/${r%-RELEASE}
            test -d $dest && continue
            mkdir -p $dest && fetch_archive $r $dest
        done
        for r; do
            cd $BASE
            archive=archive/${r%-RELEASE}
            test -d $archive || continue
            test -f $archive/wrk/.git && continue
            checkout_and_tag $r $archive
        done
        exit 0
        ;;
    *)
        exit 1
        ;;
esac
