#!/bin/sh
# vi:set sw=4 et:

# Checks out both the SVN tree and git tree of matching tags and branches and
# diff(1)s them. Errors out if a diff has been found. This can be used to check
# whether the latest-revision content of SVN vs git actually matches.
# So far, it has never found a discrepancy.

type=$1; shift

test -z "$type" && exit 1

SVN=file:///$PWD/$type
GIT=$PWD/freebsd-$type.git
S=$PWD/scratch
mkdir -p scratch

# cleanup previous runs
oldmd0=`mount -ptufs | awk -vS=$S '$2 = S {print $1; exit}'`
case "$oldmd0" in
    /dev/md*)
        sudo umount $S
        sudo mdconfig -d -u $oldmd0
        ;;
esac

set -e
md0=`sudo mdconfig -a -t swap -s 4G -S 4096 -L svn_git_compare -o compress`
sudo newfs -U $md0

sudo mount -o async,noatime /dev/$md0 $S
sudo chown $USER $S

diff_it() {
    local from to
    from=${1%%/}; shift
    to=${1:-$from}
    to=${to%%/}

    sentinel="$GIT/compared_to_`echo -n $to | tr / _`"

    if [ -r "$sentinel" ]; then
        return
    fi
    set -e
    cd $S && rm -rf s g
    svn export --ignore-keywords -q $SVN/$from s &
    GIT_DIR=$GIT git archive --format=tar --prefix=g/ $to | tar xf -
    wait
    diff -ruN s g || { echo "diffs found in SVN $from vs git $to, bailing out" >&2; exit 1; }
    touch "$sentinel"
    cd ..
    set +e
}

diff_it head master

case "$type" in
    base)
        for t in release stable releng; do
            for b in `svn ls $SVN/$t | grep -v A | fgrep -v 2.1.6.1`; do
                diff_it $t/$b
            done
        done
        for t in vendor; do
            for b in `svn ls $SVN/$t | egrep "tcsh"`; do
                for s in `svn ls $SVN/$t/$b`; do
                    diff_it $t/$b$s
                done
            done
        done
        #for t in projects; do
        #    for b in `svn ls $SVN/$t`; do
        #        diff_it $t/$b
        #    done
        #done
        #for u in `svn ls $SVN/user`; do
        #    for b in `svn ls $SVN/user/$u`; do
        #        diff_it user/$u$b
        #    done
        #done
        ;;
    doc)
        for u in `svn ls $SVN/user`; do
            for b in `svn ls $SVN/user/$u`; do
                diff_it user/$u$b
            done
        done
        for t in projects release; do
            for b in `svn ls $SVN/$t`; do
                diff_it $t/$b
            done
        done
        for b in translations; do
            diff_it $b
        done
        ;;
    ports)
        # 2.x and 3.0.0 are broken in SVN, skip EOL tags as well.
        for t in `svn ls $SVN/tags | egrep '^RELEASE_([4-9]|1[0-9]|3_[1-9])_[0-9_]+/'`; do
            diff_it tags/$t `echo $t|sed 's,RELEASE_,release/,; s,_,.,g; s,/$,,'`
        done
        diff_it branches/RELEASE_8_4_0/ releng/8.4.0
        diff_it branches/RELENG_9_2_0 releng/9.2.0
        #for b in `svn ls $SVN/branches | grep Q`; do
        #    diff_it branches/$b $b
        #done
        #for b in translations; do
        #    diff_it $b
        #done
        ;;
esac
