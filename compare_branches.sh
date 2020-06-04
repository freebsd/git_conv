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
sudo newfs -U $md0 >/dev/null

sudo mount -o async,noatime /dev/$md0 $S
sudo chown $USER $S

diff_it() {
    local from to r flags
    set -e
    while getopts "I:x:" OPT; do
        case "$OPT" in
            I) flags="$flags -I$OPTARG"
                ;;
            x) flags="$flags -x$OPTARG"
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

diff_em() {
    local from1 from2 to r flags
    set -e
    while getopts "I:x:" OPT; do
        case "$OPT" in
            I) flags="$flags -I$OPTARG"
                ;;
            x) flags="$flags -x$OPTARG"
                ;;
        esac
    done
    shift $(($OPTIND - 1))

    case $1 in
        *:*)
            from1=${1%:*}@${1#*:}
            r="-r ${1#*:}"
            ;;
        *)
            from1=${1%%/}
            r=
            ;;
    esac

    case $2 in
        *:*)
            from2=${2%:*}@${2#*:}
            r="-r ${2#*:}"
            ;;
        *)
            from2=${2%%/}
            r=
            ;;
    esac
    to=${3%%/}

    sentinel="$GIT/compared_to_`echo -n $to | tr / _`"

    if [ -r "$sentinel" ]; then
        return
    fi
    set -e
    cd $S && rm -rf s g
    # NOTE: vendor-sys/illumos/dist has a newer avl.c, as does git. If we
    # would export the SVN tags in different order, we'd get the older avl.c
    # and would end up with a diff.
    svn export --force --ignore-keywords -q $SVN/$from1 s
    svn export --force --ignore-keywords -q $SVN/$from2 s
    GIT_DIR=$GIT git archive --format=tar --prefix=g/ $to | tar xf -
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
                        echo "diffs found in SVN $from1 + $from2 vs git $to, bailing out" >&2; exit 1;
                    }
                fi
            }
        else
            echo "diffs found in SVN $from1 + $from2 vs git $to, bailing out" >&2; exit 1;
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
    exit 0
else

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
                        # Massive typo, the real r291012 was tagged as such, then r291015 (sic!) was re-tagged into r291012, meaning there's a dist inside. We can only reasonably diff the inner one.
                        *-r291012/) diff_it $t/$b${s}dist $t/$b${s%012/}015; continue ;;
                        *-r288513/) diff_it $t/$b${s}dist $t/$b${s%513/}847; continue ;;
                        # these got renamed during conversion to match the previous naming scheme, can't be bothered
                        *r375505/) continue ;;
                        # svn tags got fucked up by a second dist/ copy
                        device-tree/ianc-b78b6b80/) diff_it $t/$b${s}dist $t/$b$s; continue ;;
                        dtc/dtc-f807af19/) diff_it $t/$b${s}dist $t/$b$s; continue ;;
                        # lol, the flattening in r186675 left behind a contrib/file/.cvsignore, but that means my heuristic for finding flattened tags fails. Oh well ...
                        file/3.41/|file/4.10/|file/4.21/) diff_it $t/$b$s@186674 $t/$b$s; continue ;;
                        # git tag dropped the redundant prefix
                        dialog/dialog-1.1*) diff_it $t/$b$s $t/$b${s#dialog-}; continue ;;
                        # NOTE: these are missing the .gitignore, .gitattributes et al, because `git archive` will honor the ignore attribute :/
                        libarchive/3.2.*/|libarchive/3.3.*/|libarchive/3.4.*/|libarchive/dist/) diff_it -x.github -x.gitignore -x.gitattributes $t/$b${s} $t/$b$s; continue ;;
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
                        # we dropped the extra gnu
                        misc-GNU/gnu-sort/)
                            for r in `svn ls $SVN/$t/$b$s | grep '/$'`; do
                                diff_it $t/$b$s$r $t/$b${s#gnu-}$r
                            done
                            continue
                        ;;
                        # the tag flattening fucked up the svn keyword
                        # expansion, meaning that a hardcoded string was
                        # submitted with the old path and revision, you can see
                        # that here:
                        # https://svnweb.freebsd.org/base/vendor/openpam/CALIOPSIS/modules/pam_dummy/pam_dummy.c?revision=186063&view=markup
                        openpam/*) diff_it '-I[$](FreeBSD|Id).*[$]' $t/$b$s; continue ;;
                        # We converted this into 1 dist instead.
                        sendmail/dist-old/) continue ;;
                        # vendor and vendor-crypto were smushed together, not the SVN tag though.
                        telnet/95-10-23/) continue ;;
                        # this matches, but the heuristic looks in the wrong
                        # dir for the tag flattening
                        telnet/dist/) diff_it $t/$b$s/contrib/telnet/ $t/$b$s; continue ;;
                        # Ugh, these flattened away 2 dirs, compare to earlier rev.
                        tzcode/tzcode9*|tzcode/tzcode1999*|tzcode/tzcode2004a/) diff_it $t/$b$s@183401 $t/$b$s; continue ;;
                        # has another dist/ subdir as the 2nd tagging was messed up
                        tzdata/tzdata2009i/) diff_it $t/$b$s/dist $t/$b$s; continue ;;
                        # bogus
                        zlib/test/) continue ;;
                        # r205483 removed 2 files from the tag. There was
                        # another tag just 8d later, so we're not going to fix
                        # this up perfectly.
                        zlib/1.2.4/) continue ;;
                        # skipping patched/flattened in the git conversion.
                        expat/2.0.1_1/) continue ;;
                        # NOTE: exists only in userland, need to skip kernel bits.
                        ngatm/1.1.1/) diff_it -xsys $t/$b$s ;;
                        # NOTE: exists only in userland, need to skip kernel bits.
                        opensolaris/20080410b/|opensolaris/20100802/) diff_it -xuts $t/$b$s ;;
                        # FIXME has a diff due to the merge, probably can't be helped.
                        illumos/20100818/) continue ;;
                        # FIXME needs investigation!
                        opensolaris/20080410/) continue ;;
                        # FIXME: git is missing a handful of files
                        opensolaris/20080410a/) continue ;;
                        # need to compare 2 branches
                        illumos/*|ngatm/*|opensolaris/*) diff_em $t/$b$s vendor-sys/$b$s $t/$b$s; continue ;;
                        # Curiously some SVN keyword expansion diff in that 1 tag only?!
                        # NOTE: Our branchpoint-bump-hack means 2 files are
                        # missing that were deleted anyway (but the CVS tag
                        # didn't get the memo). It's actually more correct that
                        # these are missing, imho.
                        ipfilter/4.1.8/) diff_it '-I[$]FreeBSD.*[$]' -xtodo -xtypescript $t/$b$s $t/$b$s; continue ;;
                        # ditto
                        bind9/9.4.2/) diff_it -xFREEBSD-Upgrade -xFREEBSD-Xlist $t/$b$s; continue ;;
                        # FIXME FIXME FIXME fallout from merging from the highest rev always to fix most tags (you win some, you lose some)
                        binutils/2.10.0/) continue ;;
                        file/4.17a/) continue ;; #diff_it $t/$b$s@186674 $t/$b$s; continue ;;
                        gcc/2.95.3-test1/) continue ;;
                        gcc/2.95.3-test3/) continue ;;
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
                        openssh/4.6p1|openssh/4.7p1|openssh/4.9p1|openssh/5.0p1|openssh/5.1p1) diff_it $t/$b$s@182608 vendor/$b$s; continue ;;
                        # some hardcoded $FreeBSD$ were committed :/
                        openssh/3.*|openssh/4.*) diff_it '-I[$]FreeBSD.*[$]' $t/$b$s vendor/$b$s; continue ;;
                        # tag was moved by deleting more files
                        openssh/5.9p1) diff_it $t/$b$s@225833 vendor/$b$s; continue ;;
                        # prefixes were collapsed from 2 down to 1 in the conversion
                        telnet/95-10-23|telnet/dist) diff_it $t/$b$s/crypto/telnet vendor/$b$s; continue ;;
                        # unflattened tags, 2007 and following are identical again
                        acpica/2000*|acpica/2001*|acpica/2002*|acpica/2003*|acpica/2004*|acpica/2005*|acpica/20070320) diff_it $t/$b$s@192383 vendor/$b$s; continue ;;
                        # compare against pre-flattening
                        ath/0.9.14*|ath/0.9.16*|ath/0.9.4*|ath/0.9.5*|ath/0.9.6*) diff_it $t/$b$s@182296 vendor/$b$s; continue ;;
                        # svn can't checkout the README at the pre-flattened revision :/ git has it, checked manually.
                        ath/0.9.17.2|ath/0.9.20.3) diff_it -xREADME $t/$b$s@182296 vendor/$b$s; continue ;;
                        # FIXME has a diff due to the merge, probably can't be helped.
                        illumos/20100818) continue ;;
                        # FIXME needs investigation!
                        opensolaris/20080410) continue ;;
                        # FIXME: git is missing a handful of files
                        opensolaris/20080410a) continue ;;
                        # need to compare 2 branches, basically already handled above anyway
                        illumos/*|ngatm/*|opensolaris/*) diff_em $t/$b$s vendor/$b$s vendor/$b$s; continue ;;
                        # this was merged into the proper dist branch
                        ipfilter/dist-old) continue ;;
                        # FIXME: these got the new layout compared to SVN
                        ipfilter/v3-4-16|ipfilter/v3-4-29) continue ;;
                        # compare against pre-flattening, due to splicing old/new dist, we end up with 2 extra files.
                        ipfilter/3*|ipfilter/v3*|ipfilter/V3*|ipfilter/4*) diff_it -xmlf_ipl.c -xmln_ipl.c $t/$b$s@253466 $t/$b$s; continue ;;
                        ipfilter/*) diff_it $t/$b$s $t/$b$s; continue ;;
                        # compare againts pre-flattening
                        pf/3.7.001|pf/4.1) diff_it $t/$b$s@181287 $t/$b$s; continue ;;
                        pf/*) diff_it $t/$b$s $t/$b$s; continue ;;
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
