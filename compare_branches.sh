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
    local from to r flags
    set -e
    while getopts "I:x:" OPT; do
        case "$OPT" in
            I) flags="$flags -I$OPTARG"
                ;;
            x) flags="$flags -I$OPTARG"
                ;;
        esac
    done
    shift $(($OPTIND - 1))

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
    #flags='-I[$]FreeBSD.*[$]'
    # Some tag flattenings "lost" their original .cvsignore, ignore such diffs.
    flags="$flags -x.cvsignore"
    # llvm/llvm-release_60-r321788/ and co have a file called
    # preserve-comments-crlf.s where git preserves the CRLF line ending, but
    # the SVN export doesn't? The file in SVN has the svn:eol-style=native
    # property though ...
    diff -ruN --strip-trailing-cr `echo $flags` s g >/dev/null || {
        # we don't flatten tags, so try 1 or 2 levels deeper again.
        if [ 1 -eq `ls -1 g/|wc -l` ] ; then
            diff -ruN --strip-trailing-cr `echo $flags` s g/*/ >/dev/null || {
                if [ 1 -eq `ls -1 g/*/|wc -l` ] ; then
                    diff -ruN --strip-trailing-cr `echo $flags` s g/*/*/ >/dev/null || {
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
                case "$b" in
                    unknown/) diff_it $t/$b; continue ;;
                esac
                for s in `svn ls $SVN/$t/$b | grep '/$'`; do
                    case "$b$s" in
                        # stripping redundant tags/
                        SGI/tags/) diff_it $t/$b$s/v_2_17 $t/${b}v_2_17; continue ;;
                        # strip redundant prefix
                        byacc/byacc-20120115/) diff_it $t/$b$s $t/$b${s#byacc-}; continue ;;
                        # these got renamed, can't be bothered
                        *r375505/) continue ;;
                        # svn tags got fucked up by a second dist/ copy, there's also other schmutz
                        # FIXME: re-investigate
                        *-r291012/|*-r288513/) continue ;;
                        # ditto
                        device-tree/ianc-b78b6b80/) diff_it $t/$b$s/dist $t/$b$s; continue ;;
                        dtc/dtc-f807af19/) diff_it $t/$b$s/dist $t/$b$s; continue ;;
                        # lol, the flattening in r186675 left behind a contrib/file/.cvsignore, but that means my heuristic for finding flattened tags fails. Oh well ...
                        file/3.41/|file/4.10/|file/4.17a/|file/4.21/) continue ;;
                        # git tag dropped the redundant prefix
                        dialog/dialog-1.1*) diff_it $t/$b$s $t/$b${s#dialog-}; continue ;;
                        # TODO: these are missing the .gitignore, .gitattributes and .github/workflows/ci.yml in git, converter error?
                        libarchive/3.2.*/|libarchive/3.3.*/|libarchive/3.4.*/|libarchive/dist/) continue ;;
                        # These have an extra level of depth
                        google/*/|Juniper/libxo/|NetBSD/blacklist/|NetBSD/bmake/|NetBSD/libc-pwcache/|NetBSD/libc-vis/|NetBSD/libedit/|NetBSD/libexecinfo/|NetBSD/lukemftp/|NetBSD/lukemftpd/|NetBSD/mknod/|NetBSD/mtree/|NetBSD/softfloat/|NetBSD/sort/|NetBSD/tests/|NetBSD/unvis/|NetBSD/vis/|NetBSD/xlint/|misc-GNU/awk/|misc-GNU/bc/|misc-GNU/bison/|misc-GNU/cpio/|misc-GNU/cvs/|misc-GNU/tar/|misc-GNU/texinfo/)
                            for r in `svn ls $SVN/$t/$b$s | grep '/$'`; do
                                case "$r" in
                                    bmake-20121111/) diff_it $t/$b$s$r $t/$b$s${r#bmake-}; continue ;;
                                esac
                                diff_it $t/$b$s$r
                            done
                            continue
                            ;;
                        # 2 svn, 1 git
                        illumos*) continue ;;
                        # we dropped the extra gnu
                        misc-GNU/gnu-sort/)
                            for r in `svn ls $SVN/$t/$b$s | grep '/$'`; do
                                diff_it $t/$b$s$r $t/$b${s#gnu-}$r
                            done
                            continue
                        ;;
                        # TODO
                        ipfilter/*) continue ;;
                        ngatm/*) continue ;;
                        opensolaris/*) continue ;;
                        # FIXME! there's an extra file that must not be there!
                        opencsd/a1961c91b02a92f3c6ed8b145c636ac4c5565aca/|opencsd/dist/) continue ;;
                        # the tag flattening fucked up the svn keyword
                        # expansion, meaning that a hardcoded string was
                        # submitted with the old path and revision, you can see
                        # that here:
                        # https://svnweb.freebsd.org/base/vendor/openpam/CALIOPSIS/modules/pam_dummy/pam_dummy.c?revision=186063&view=markup
                        openpam/*) diff_it '-I[$](FreeBSD|Id).*[$]' $t/$b$s ;;
                        # FIXME FIXME FIXME, due to the merging of dist and
                        # dist-old, we have a bogus src/Makefiles subdir in
                        # here, we need to delete that somewhere along the
                        # conversion.
                        sendmail/*) continue ;;
                        # vendor and vendor-crypto were smushed together, not the SVN tag though.
                        telnet/95-10-23/) continue ;;
                        # this matches, but the heuristic looks in the wrong
                        # dir for the tag flattening
                        telnet/dist/) diff_it $t/$b$s/contrib/telnet/ $t/$b$s; continue ;;
                        # Ugh, these flattened away 2 dirs
                        tzcode/*) continue ;;
                        # has another dist/ subdir as the 2nd tagging was messed up
                        tzdata/tzdata2009i/) diff_it $t/$b$s/dist $t/$b$s; continue ;;
                        # bogus
                        zlib/test/) continue ;;
                        # FIXME has extra files inffas86.c and inffast.S ... how?
                        zlib/1.2.4/|zlib/1.2.8-full/) continue ;;
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
                        # tags were "cleaned up" in r182609, skip them
                        openssh/4.6p1|openssh/4.7p1|openssh/4.9p1|openssh/5.0p1|openssh/5.1p1) continue ;;
                        # some hardcoded $FreeBSD$ were committed :/
                        openssh/3.*|openssh/4.*) diff_it '-I[$]FreeBSD.*[$]' $t/$b$s vendor/$b$s; continue ;;
                        # bunch of extra files in here, as we didn't propagate the deletes, we could compare to an older rev though.
                        openssh/4.6p1|openssh/4.7p1|openssh/4.9p1|openssh/5.0p1|openssh/5.1p1|openssh/5.9p1) continue ;;
                        # prefixes were collapsed from 2 down to 1 in the conversion
                        telnet/95-10-23|telnet/dist) diff_it $t/$b$s/crypto/telnet vendor/$b$s; continue ;;
                        # unflattened tags, 2010 and following are identical again
                        acpica/200*) continue ;;
                        # compare against pre-flattening
                        ath/0.9.16*) diff_it $t/$b$s:182296 vendor/$b$s; continue ;;
                        # FIXME
                        ath/0*) continue ;;
                        # need to compare 2 branches, not implemented yet but was compared manually
                        illumos/*|ngatm/*|opensolaris/*) continue ;;
                        # TODO merge or compare against vendor-sys
                        ipfilter/*|pf/*) continue ;;
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
