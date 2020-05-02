#!/bin/sh
# vi:set sw=4 et:

type=$1; shift

test -z "$type" && exit 1

SVN=file:///$PWD/$type
GIT=$PWD/freebsd-$type.git
S=$PWD/scratch
mkdir -p scratch

# cleanup previous runs
oldmd0=`mount -ptufs | awk -vS=$S '$2 = S {print $1}'`
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
    cd $S && rm -rf s g
    svn export --ignore-keywords -q $SVN/$from s &
    GIT_DIR=$GIT git archive --format=tar --prefix=g/ $to | tar xf -
    wait
    diff -ruN s g || { echo "diffs found in SVN $from vs git $to, bailing out" >&2; exit 1; }
    touch "$sentinel"
    cd ..
}

#diff_it head master

case "$type" in
    base)
        for u in `svn ls $SVN/user`; do
            for b in `svn ls $SVN/user/$u`; do
                diff_it user/$u$b
            done
        done
        for t in projects release stable releng; do
            for b in `svn ls $SVN/$t`; do
                diff_it $t/$b
            done
        done
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
        diff_it tags/RELEASE_4_1_0 release/4.1
        diff_it tags/RELEASE_4_2_0 release/4.2
        diff_it tags/RELEASE_5_0_0 release/5.0
        diff_it tags/RELEASE_6_0_0 release/6.0
        diff_it tags/RELEASE_7_0_0 release/7.0
        diff_it tags/RELEASE_8_4_0 release/8.4
        diff_it branches/RELEASE_8_4_0/ releng/8.4
        diff_it tags/RELEASE_9_0_0 release/9.0
        diff_it tags/RELEASE_10_0_0 release/10.0
        diff_it tags/RELEASE_11_0_0 release/11.0
        #for b in `svn ls $SVN/branches | grep Q`; do
        #    diff_it branches/$b $b
        #done
        #for b in translations; do
        #    diff_it $b
        #done
        ;;
esac
