#!/bin/sh

# Script to replace all release tags with checked out copies of the source
# code. This brings
# a) proper CVS IDs for people that care, but most importantly
# b) undoes the damage that repo-copies and moves did to the repo,
#    at least for those final releases.
# c) they lack the eBones/, kerberosIV/, secure/ and crypto/ subdirs till 5.3-RELEASE
#
# Same could be done for ports, but there is very little archaeological value
# in there, so it is left as an exercise for the reader.

: ${BASE=${PWD}}

SOURCE=ftp://ftp-archive.freebsd.org/pub/FreeBSD-Archive/old-releases/i386
# more content can be had from the ISO images
# http://ftp-archive.freebsd.org/pub/FreeBSD-Archive/old-releases/i386/ISO-IMAGES/1.0/1.0-disc1.iso
# http://ftp-archive.freebsd.org/pub/FreeBSD-Archive/old-releases/i386/ISO-IMAGES/FreeBSD-1.1-RELEASE/cd1.iso
# http://ftp-archive.freebsd.org/pub/FreeBSD-Archive/old-releases/i386/ISO-IMAGES/FreeBSD-1.1.5.1/cd1.iso
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
          1.0-RELEASE) wget -nH -nd -r -N --progress=dot $SOURCE/$r/tarballs/srcdist/ ;;
          1.1-RELEASE) fetch -o- $SOURCE/ISO-IMAGES/FreeBSD-$r/cd1.iso | tar xf - -s',^tarballs/srcdist,,' tarballs/srcdist ;;
          1.1.5.1-RELEASE) fetch -o- $SOURCE/ISO-IMAGES/FreeBSD-${r%-RELEASE}/cd1.iso | tar xf - -s',^tarballs/srcdist,,' tarballs/srcdist ;;
          2.0-RELEASE) fetch -o- $SOURCE/ISO-IMAGES/FreeBSD-$r/cd1.iso | tar xf - -s',^srcdist,,' srcdist ;;
          # bsdtar doesn't want to read these ...
          2.1-RELEASE) fetch $SOURCE/ISO-IMAGES/FreeBSD-$r/cd1.iso && 7z e cd1.iso dists/src ;;
          2.1.6-RELEASE) fetch $SOURCE/ISO-IMAGES/FreeBSD-$r/cd1.iso && 7z e cd1.iso src;;
          2.2.1-RELEASE) fetch $SOURCE/ISO-IMAGES/FreeBSD-$r/cd1.iso && 7z e cd1.iso src ;;
          4.6-RELEASE) wget -nH -nd -r -N --progress=dot ${SOURCE%i386}alpha/$r/src/ ;;  # slib is damaged under i386/
          9*|10*|11*|12*|13*) fetch $SOURCE/$r/src.txz ;;
          *) wget -nH -nd -r -N --progress=dot $SOURCE/$r/src/ ;;
      esac
    )
    set +e
}

extract() {
    local r dest
    r=$1; shift
    dest=$1; shift

    set -e
    case "$r" in
        9*|10*|11*|12*|13*)
            tar xf src.txz -s',^usr/src/,,' -C wrk
            ;;
        1.*-RELEASE)
            for f in *.aa; do
                cat ${f%.aa}.?? | tar xf - -s',^usr/src/,,' -C wrk
            done
            ;;
        *)
            for f in *.aa; do
                cat ${f%.aa}.?? | tar xf - -C wrk
            done
            ;;
    esac
    # clean up some leftover schmutz
    find wrk -name CVS -type d -exec rm -r {} +
    find wrk -name .cvsignore -type f -exec rm -r {} +
    # yeah, so, well, some releases have a bunch of .depend and object files
    # and binaries left around still.
    (
        cd wrk
        for d in bin sbin usr.bin usr.sbin gnu/usr.bin release/sysinstall lib/libpam/modules/pam_krb5 lib/libpam/modules/pam_kerberosIV sys/i386/boot/biosboot sys/libkern; do
            test -d $d && make -C $d -k MACHINE_ARCH=i386 cleandir || true
        done
        make -C usr.bin/vi -k MACHINE_ARCH=i386 RELEASE_BUILD_FIXIT=1 cleandir || true
    )
    set +e
}

checkout_and_tag() {
    local dest rel tag
    rel=$1; shift
    dest=$1; shift

    set -e

    case $rel in
        2.0-RELEASE) tag=${rel%-RELEASE} ;;
        *.*.*-RELEASE) tag=${rel%-RELEASE} ;;
        *.*-RELEASE) tag=${rel%-RELEASE}.0 ;;
    esac

    c_auth=
    c_committer=
    c_email=
    c_date=
    c_msg=
    cd $dest
    case $rel in
        1.0-RELEASE)
            GIT_DIR=$REPO git worktree add --detach --no-checkout wrk
            ( cd wrk && git checkout --orphan stable/1 && git reset --hard )
            # FIXME
            c_auth="svn2git <svn2git@FreeBSD.org>"
            c_date="1993-11-01T00:00:00-0800"
            c_msg="Release FreeBSD 1.0"
            ;;
        1.1-RELEASE)
            GIT_DIR=$REPO git worktree add -f --no-checkout wrk stable/1
            # FIXME
            c_auth="svn2git <svn2git@FreeBSD.org>"
            c_date="1994-05-01T00:00:00-0800"
            c_msg="Release FreeBSD 1.1"
            ;;
        1.1.5.1-RELEASE)
            GIT_DIR=$REPO git worktree add -f --no-checkout wrk stable/1
            # FIXME
            c_auth="svn2git <svn2git@FreeBSD.org>"
            c_date="1994-07-01T00:00:00-0800"
            c_msg="Release FreeBSD 1.1.5.1"
            ;;
        *)
            GIT_DIR=$REPO git worktree add --no-checkout wrk release/$tag
            ;;
    esac
    extract $rel $dest
    cd wrk

    # Grab commit metadata from the original annotated tag
    if [ -z "$c_auth" ]; then
        c_auth=`git cat-file tag release/$tag | sed -n '/^tagger/s/^tagger //; s/ [0-9 +]*$//p'`
        c_date=`git cat-file tag release/$tag | sed -n '/^tagger/s/^tagger //p' | egrep -o '[0-9]* [+0-9]*$'`
        c_msg=`git cat-file tag release/$tag | sed '1,/^$/d'`
    fi
    c_committer=${c_auth%<*}
    c_email=${c_auth#*<}
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
    if [ -z "$c_date" -o -z "$c_committer" -o -z "$c_email" -o -z "$c_auth" -o -z "$c_msg" -o -z "$msg" ]; then
        echo "Don't know what to commit for rel $rel and tag $tag"
        exit 1
    fi
    git add -fN .
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
            #2.1.7.1-RELEASE # we don't have the CVS or SVN tag for it?
            #2.2.6-RELEASE   # has nothing under src/ on the archive server
            #2.2.9-RELEASE   # not in SVN
            #3.5.1-RELEASE   # not in SVN
            #8.4-RELEASE and ff # first releases off of SVN, no interesting IDs in there.
        set -- \
            1.0-RELEASE \
            1.1-RELEASE \
            1.1.5.1-RELEASE \
            2.0-RELEASE \
            2.0.5-RELEASE \
            2.1-RELEASE \
            2.1.5-RELEASE \
            2.1.6-RELEASE \
            2.1.7-RELEASE \
            2.2.1-RELEASE \
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
            8.3-RELEASE
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
        ;;
    *)
        exit 1
        ;;
esac
