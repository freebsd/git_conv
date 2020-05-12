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
        sudo umount -f $S
        sudo mdconfig -d -u $oldmd0
        ;;
esac

set -e
md0=`sudo mdconfig -a -t swap -s 4G -S 4096 -L svn_git_compare -o compress`
sudo newfs -U $md0

sudo mount -o async,noatime /dev/$md0 $S
sudo chown $USER $S

diff_it() {
    local from to r
    set -e
    case $1 in
        *:*)
            from=${1%:*}@${1#*:}
            r="-r ${1#*:}"
            to=$2
            ;;
        *)
            from=${1%%/}
            r=
            to=${2:-$from}
            to=${to%%/}
            ;;
    esac

    sentinel="$GIT/compared_to_`echo -n $to | tr / _`"

    if [ -r "$sentinel" ]; then
        return
    fi
    set -e
    cd $S && rm -rf s g
    svn export --ignore-keywords -q $SVN/$from s &
    GIT_DIR=$GIT git archive --format=tar --prefix=g/ $to | tar xf -
    wait
    test -d s || exit 1
    test -d g || exit 1
    diff -ruN -I'[$]FreeBSD.*[$]' s g >/dev/null || {
        # we don't flatten tags, so try 1 or 2 levels deeper again.
        if [ 1 -eq `ls -1 g/|wc -l` ] ; then
            diff -ruN -I'[$]FreeBSD.*[$]' s g/*/ >/dev/null || {
                if [ 1 -eq `ls -1 g/*/|wc -l` ] ; then
                    diff -ruN -I'[$]FreeBSD.*[$]' s g/*/*/ >/dev/null || {
                        echo "diffs found in SVN $from vs git $to, bailing out" >&2; exit 1;
                    }
                fi
            }
        else
            echo "diffs found in SVN $from vs git $to, bailing out" >&2; exit 1;
        fi
    }
    touch "$sentinel"
    cd ..
    set +e
}

if [ $# -ge 2 ] ; then
    while [ $# -ge 2 ] ; do
        from=$1; shift
        to=$1;shift
        diff_it $from $to
    done
else

#diff_it vendor-cddl/opensolaris/dist:178529 275928fc142
#diff_it vendor-cddl/opensolaris/20080410a:179530 275928fc142
#diff_it vendor-sys/opensolaris/dist:194446 vendor/opensolaris/dist
#diff_it vendor-sys/illumos/dist:238570 vendor/illumos/dist
#diff_it vendor-crypto/openssh/dist vendor/openssh/dist
#diff_it vendor-crypto/openssh/5.2p1 vendor/openssh/5.2p1

diff_it head master

case "$type" in
    base)
        diff_it releng/ALPHA_2_0 releng/2.0a
        diff_it releng/BETA_2_0 releng/2.0b
        for t in release stable releng; do
            for b in `svn ls $SVN/$t | grep -v A_2_0`; do
                diff_it $t/$b
            done
        done
        for t in vendor; do
            for b in `svn ls $SVN/$t`; do
                for s in `svn ls $SVN/$t/$b | grep '/$'`; do
                    case "$b$s" in
                        Juniper/libxo/|NetBSD/blacklist/|NetBSD/bmake/|NetBSD/libc-pwcache/|NetBSD/libc-vis/|NetBSD/libedit/|NetBSD/libexecinfo/|NetBSD/lukemftp/|NetBSD/lukemftpd/|NetBSD/mknod/|NetBSD/mtree/|NetBSD/softfloat/|NetBSD/sort/|NetBSD/tests/|NetBSD/unvis/|NetBSD/vis/|NetBSD/xlint/)
                            for r in `svn ls $SVN/$t/$b$s | grep '/$'`; do
                                case "$r" in
                                    bmake-20121111/) continue ;;
                                esac
                                diff_it $t/$b$s$r
                            done
                            continue
                            ;;
                    esac
                    s=${s%/}
                    case "$b$s" in
                        ipfilter/*) continue ;;
                    esac
                    diff_it $t/$b$s
                done
            done
        done
        for t in vendor-crypto vendor-sys; do
            for b in `svn ls $SVN/$t`; do
                for s in `svn ls $SVN/$t/$b`; do
                    s=${s%/}
                    case "$b$s" in
                        # bunch of extra files in here, as we didn't propagate the deletes, we could compare to an older rev though.
                        openssh/4.6p1|openssh/4.7p1|openssh/4.9p1|openssh/5.0p1|openssh/5.1p1|openssh/5.9p1) continue ;;
                        # I thought the prefixes were collapsed, TODO fix the rule stripping to fix this up
                        telnet/95-10-23|telnet/dist) continue ;;
                        # unflattened tags, 2010 and following are identical again
                        acpica/200*) continue ;;
                        # compare against pre-flattening
                        ath/0.9.16*) diff_it $t/$b$s:182296 vendor/$b$s; continue ;;
                        # FIXME
                        ath/0*) continue ;;
                        # need to compare 2 branches, not implemented yet but was compared manually
                        illumos/*|ngatm/*|opensolaris/*|pf/*) continue ;;
                        # TODO merge or compare against vendor-sys
                        ipfilter/*) continue ;;
                    esac
                    diff_it $t/$b$s vendor/$b$s
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
fi
