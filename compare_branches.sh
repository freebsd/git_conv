#!/usr/bin/env zsh
# vi:set sw=4 et:

# Checks out both the SVN tree and git tree of matching tags and branches and
# diff(1)s them. Errors out if a diff has been found. This can be used to check
# whether the latest-revision content of SVN vs git actually matches.
# So far, it has never found a discrepancy.

type=${1:-base}; shift

test -z "$type" && exit 1

SVN=file:///$PWD/$type
GIT=$PWD/freebsd-$type.git
export GIT_DIR=$GIT
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
    local from to r flags remove
    set -e
    while getopts "I:x:r:" OPT; do
        case "$OPT" in
            I) flags="$flags -I$OPTARG"
                ;;
            x) flags="$flags -x$OPTARG"
                ;;
            r) remove="$OPTARG"
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
    cd $S && rm -rf s g
    svn export --ignore-keywords -q $SVN/$from s &
    git archive --format=tar --prefix=g/ $to | tar xf -
    wait
    test -d s || exit 1
    test -d g || exit 1
    if [ -n "$remove" ]; then
        rm -rf `eval echo $remove`
    fi
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
                        if [ 1 -eq `ls -1 g/*/*/|wc -l` ] ; then
                            diff -ruN --strip-trailing-cr `echo $flags` s g/*/*/*/ >/dev/null || {
                                bail_out $from $to
                                return
                            }
                        else
                            bail_out $from $to
                            return
                        fi
                    }
                else
                    bail_out $from $to
                    return
                fi
            }
        else
            bail_out $from $to
            return
        fi
    }

    touch "$sentinel"
}

diff_em() {
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

    eval to=\$$#
    to=${to%%/}
    sentinel="$GIT/compared_to_`echo -n $to | tr / _`"
    if [ -r "$sentinel" ]; then
        return
    fi

    cd $S && rm -rf s g

    while [ $# -gt 1 ]; do
        case $1 in
            *:*)
                from=${1%:*}@${1#*:}
                r="-r ${1#*:}"
                ;;
            *)
                from=${1%%/}
                r=
                ;;
        esac
        svn export --force --ignore-keywords -q $SVN/$from s
        shift
    done

    git archive --format=tar --prefix=g/ $to | tar xf -
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
                        bail_out $from $to
                        return
                    }
                else
                    bail_out $from $to
                    return
                fi
            }
        else
            bail_out $from $to
            return
        fi
    }

    touch "$sentinel"
}

bail_out()
{
    echo "diffs found in SVN $1 vs git $2, bailing out" >&2
    if [ -z "$keep_going" ]; then
        exit 1
    fi
}

keep_going=
while getopts "k" OPT; do
    case "$OPT" in
        k) keep_going=1
            ;;
    esac
done
shift $(($OPTIND - 1))

if [ $# -ge 2 ] ; then
    while [ $# -ge 2 ] ; do
        from=$1; shift
        to=$1;shift
        diff_it $from $to
    done
    exit 0
else

case "$type" in
    base)
        git log --format="%h %N" --reverse --notes --grep="path=/head/" master | egrep '^[^s].*=/head/;' | sed -e 's/ .*=/ /' | awk 'NR % 10 == 0' | head -200 | \
            while read ref rev; do
                if [ $rev -lt 77859 ]; then
                    diff_it -r's/sys/contrib/ipfilter/netinet' head@$rev $ref
                else
                    diff_it head@$rev $ref
                fi
            done
        git log --format="%h %N" --reverse --notes --grep="path=/stable" stable/2.1 | egrep '^[^s].*=/stable' | sed -e 's/ .*=/ /' | awk 'NR % 10 == 0' | head -100 | \
            while read ref rev; do
                diff_it stable/2.1@$rev $ref
            done
        git log --format="%h %N" --reverse --notes --grep="path=/stable" stable/2.2 | egrep '^[^s].*=/stable' | sed -e 's/ .*=/ /' | awk 'NR % 10 == 0' | head -100 | \
            while read ref rev; do
                diff_it stable/2.2@$rev $ref
            done
        git log --format="%h %N" --reverse --notes --grep="path=/stable" stable/3 | egrep '^[^s].*=/stable' | sed -e 's/ .*=/ /' | awk 'NR % 20 == 0' | head -100 | \
            while read ref rev; do
                diff_it stable/3@$rev $ref
            done
        git log --format="%h %N" --reverse --notes --grep="path=/stable" stable/4 | egrep '^[^s].*=/stable' | sed -e 's/ .*=/ /' | awk 'NR % 30 == 0' | head -100 | \
            while read ref rev; do
                diff_it stable/4@$rev $ref
            done
        git log --format="%h %N" --reverse --notes --grep="path=/head/" master | egrep '^[^s].*=/head/;' | sed -e 's/ .*=/ /' | awk 'NR % 30 == 0' | head -300 | \
            while read ref rev; do
                diff_it -r's/sys/contrib/ipfilter/netinet' head@$rev $ref
            done
        git log --format="%h %N" --reverse --notes --grep="path=/head/" master | egrep '^[^s].*=/head/;' | sed -e 's/ .*=/ /' | awk 'NR % 1000 == 0' | head -100 | \
            while read ref rev; do
                if [ $rev -lt 77859 ]; then
                    diff_it -r's/sys/contrib/ipfilter/netinet' head@$rev $ref
                elif [ $rev -le 151841 ]; then
                    # ppp-user -> ppp repo-copy was corrected
                    diff_it -xppp head@$rev $ref
                else
                    diff_it head@$rev $ref
                fi
            done

        git log --format="%h %N" --reverse master|egrep '^[^s].*=/head/;' | sed -e 's/ .*=/ /' | awk 'NR % 1000 == 0' | \
            while read ref rev; do
                if [ $rev -eq 1638 ]; then
                    # in git this appears 1 rev earlier
                    diff_it -r's/sys/contrib/ipfilter/netinet' head@$((rev+1)) $ref
                elif [ $rev -lt 77859 ]; then
                    diff_it -r's/sys/contrib/ipfilter/netinet' head@$rev $ref
                elif [ $rev -le 151841 ]; then
                    # ppp-user -> ppp repo-copy was corrected
                    diff_it -xppp head@$rev $ref
                else
                    diff_it head@$rev $ref
                fi
            done

        diff_it head master
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
                    # We move some stuff from this directly into the mainline, so diffing no longer makes sense.
                    unknown/) continue ;;
                esac
                for s in `svn ls $SVN/$t/$b | grep '/$'`; do
                    # we skip generating these tags, they are of dubious quality anyway and looking up the history in the dist branch is easy enough.
                    if echo "$b$s" | egrep -q "misc-GNU/(gnu_tag|GZIP_1_1|TEXT_1_6|GREP_1_6|DIFF_2_3|DIFF3_2_3|V_GNU_0_2|ptx_0_3|readline_1_1|texinfo_2_0|libg\+\+_tag|V1_09|rcs_5_7|diff_2_7|gmp_1_3_2|textutils_1_14|gmp_2_0_2|v2_3|grep_2_[34][ad]?|grep_2_4_2|grep_2_5_1|v6_1_1)"; then
                        continue
                    fi
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
                        google/*/|Juniper/libxo/|NetBSD/blacklist/|NetBSD/bmake/|NetBSD/libc-pwcache/|NetBSD/libc-vis/|NetBSD/libedit/|NetBSD/libexecinfo/|NetBSD/lukemftp/|NetBSD/lukemftpd/|NetBSD/mknod/|NetBSD/mtree/|NetBSD/softfloat/|NetBSD/sort/|NetBSD/tests/|NetBSD/unvis/|NetBSD/vis/|NetBSD/xlint/|misc-GNU/awk/|misc-GNU/bc/|misc-GNU/bison/|misc-GNU/cpio/|misc-GNU/cvs/|misc-GNU/texinfo/)
                            for r in `svn ls $SVN/$t/$b$s | grep '/$'`; do
                                case "$r" in
                                    bmake-20121111/) diff_it $t/$b$s$r $t/$b$s${r#bmake-}; continue ;;
                                    1.6.3-END/) continue ;;
                                esac
                                case "$s$r" in
                                    # The "vendor" tag was resurrecting age old data and should likely never have happened
                                    xlint/dist/) diff_it cvs2svn/branches/JPO@260579 $t/$b$s$r; continue ;;
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
                        # These are handled in the vendor-sys loop below
                        illumos/20100818/) continue ;;
                        opensolaris/20080410/) continue ;;
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
                        # we spliced the 3 routed vendor branches into 1, just compare the latest SVN one
                        SGI/dist/|SGI/dist2/|SGI/sgi_routed/|SGI/v_2_22/) continue ;;
                        # the splicing means we actually have Makefiles!, sadly need to skip them all, also in the tags
                        SGI/dist_v_2_21/) diff_it -xMakefile $t/$b$s $t/${b}dist; continue ;;
                        # we resurrected a missing header here
                        SGI/vjs_960912/) diff_it -xrouted.h $t/$b$s; continue ;;
                        SGI/*/) diff_it -xMakefile $t/$b$s; continue ;;
                        # splicing MACKERAS and pppd leads to 2 extra files
                        pppd/2.1.2/) diff_it -xslcompress.c -xslcompress.h $t/$b$s; continue ;;
                        # have a bunch of extra files, we could diff_em together with the old cvs2svn branch @260580, though.
                        #pppd/2.2/|pppd/2.3.0/|pppd/2.3.1/|pppd/2.3.11/|pppd/2.3.3/|pppd/2.3.5/|pppd/dist/) continue ;;
                        # tags were advanced with a rename
                        top/3.4/|top/3.5beta12/) diff_it -xinstall -xinstall-sh $t/$b$s; continue ;;
                        ## lol, the tag flattening in r186675 actually fucked this up
                        file/4.17a/) diff_it $t/$b$s@186674 $t/$b$s; continue ;;
                        # we skipped the test1/test3 interim tags
                        gcc/2.95.3-test1/) continue ;;
                        gcc/2.95.3-test3/) continue ;;
                        # we skipped the flattening of tags, compare to earlier rev
                        gcc/4.2.0*|gcc/4.2.1*|gcc/egcs*) diff_it $t/$b$s@179467 $t/$b$s; continue ;;
                        # we moved some stuff from cvs2svn/branches/MACKERAS here
                        pppd/2.2/) diff_it -r'g/usr.sbin/pppd/{RELNOTES,args.h,callout.h,ppp.h}' $t/$b$s; continue ;;
                        pppd/2.3.0/) diff_it -r'g/{usr.sbin/pppd/{RELNOTES,args.h,callout.h,ppp.h},usr.bin/chat/{Example,Makefile,README,chat.?,connect-ppp,ppp-o*,unlock}}' $t/$b$s; continue ;;
                        pppd/2.3.1/|pppd/2.3.3/|pppd/2.3.5/) diff_it -r'g/usr.bin/chat/{Example,README,connect-ppp,ppp-o*,unlock}' $t/$b$s; continue ;;
                        pppd/2.3.3/) diff_it -r'g/usr.bin/chat/{Example,README}' $t/$b$s; continue ;;
                        # Everything is missing, what's the point
                        pppd/2.3.11/) continue ;;
                        # filesets diverged too much
                        pppd/dist/) continue ;;
                        # bogus tag
                        pppd/pppstats/) continue ;;
                        # we splice in cvs2svn/branches/UDEL into ntpd, so we have some extra files.
                        ntpd/dist/) diff_it -r'g/usr.sbin/xntpd/parse/util/Makefile' $t/$b$s; continue ;;
                        ntpd/udel_33Z/) diff_it -r'g/usr.sbin/xntpd/{parse/util/Makefile,Config,Config.sed}' $t/$b$s; continue ;;
                        ntpd/udel_3_3p/) diff_it -r'g/usr.sbin/xntpd/{Config,Config.sed,compilers/hpux10+.cc,machines/hpux10+,parse/util/Makefile}' $t/$b$s; continue ;;
                        ntpd/xntp*/) diff_it -r'g/usr.sbin/xntpd/{Config,Config.sed,compilers/hpux10+.cc,machines/hpux10+,parse/util/Makefile}' $t/$b$s; continue ;;
                        diff/*/) diff_it -xconfig.h $t/$b$s $t/misc-GNU/$b$s; continue ;;
                        # we stop a later import reverting things done earlier, libc has no business here anyway.
                        bind4/dist/) diff_it -r'{s,g}/lib/libc' $t/$b$s; continue ;;
                        # we undo some repo copy stuff
                        flex/2.4.7/) diff_it -r'{s/usr.bin/lex/flex.1,g/usr.bin/lex/lex.1}' $t/$b$s; continue ;;
                        blocklist/20160409/|blocklist/20170503/|blocklist/20191106/) continue ;;
                        #### inlined stuff below here ####
                        # These were all merged, but actually we inline some of them, so can't compare them anymore.
                        misc-GNU/dist*/)
                            #diff_em vendor/misc-GNU/dist vendor/misc-GNU/dist1 vendor/misc-GNU/dist3 vendor/misc-GNU/dist2 vendor/misc-GNU/dist
                            continue
                        ;;
                        # the 1 commit on telnet was inlined into main
                        telnet/*/) continue ;;
                        # we've inlined a bunch of files, cannot compare any longer
                        games/dist/) continue ;;
                        # inlined into mainline or we stole some select commits
                        # off of them. We could compare git vs SVN and ignore
                        # "new" files, but I have no desire to hack this in
                        # further.
                        pnpinfo/*/) continue ;;
                        jthorpe/dist/) continue ;;
                        misc-GNU/tar/) continue ;;
                        CSRG/*/) continue ;;
                        sendmail/8.6.10/|sendmail/8.7*/|sendmail/8.8*/) continue ;;
                        NetBSD/dist/) continue ;;
                        OpenBSD/*/) continue ;;
                    esac
                    diff_it $t/$b$s
                done
            done
        done
        for t in vendor-crypto vendor-sys; do
            for b in `svn ls $SVN/$t`; do
                for s in `svn ls $SVN/$t/$b`; do
                    s=${s%/}
                    b=${b%/}
                    case "$b/$s" in
                        # bunch of extra files in here, as we didn't propagate the deletes, we could compare to an older rev though.
                        openssh/4.6p1|openssh/4.7p1|openssh/4.9p1|openssh/5.0p1|openssh/5.1p1) diff_it $t/$b/$s@182608 vendor/$b/$s; continue ;;
                        # some hardcoded $FreeBSD$ were committed :/
                        openssh/3.*|openssh/4.*) diff_it '-I[$]FreeBSD.*[$]' $t/$b/$s vendor/$b/$s; continue ;;
                        # tag was moved by deleting more files
                        openssh/5.9p1) diff_it $t/$b/$s@225833 vendor/$b/$s; continue ;;
                        # unflattened tags, 2007 and following are identical again
                        acpica/2000*|acpica/2001*|acpica/2002*|acpica/2003*|acpica/2004*|acpica/2005*|acpica/20070320) diff_it $t/$b/$s@192383 vendor/$b/$s; continue ;;
                        # compare against pre-flattening
                        ath/0.9.14*|ath/0.9.16*|ath/0.9.4*|ath/0.9.5*|ath/0.9.6*) diff_it $t/$b/$s@182296 vendor/$b/$s; continue ;;
                        # svn can't checkout the README at the pre-flattened revision :/ git has it, checked manually.
                        ath/0.9.17.2|ath/0.9.20.3) diff_it -xREADME $t/$b/$s@182296 vendor/$b/$s; continue ;;
                        # git is different, because the tag includes r238575 from the sys branch (plus manpages)
                        # This is bogus anyway, as it has files with copyrights in 2011 and 2012, looks like the tag gone wrong, maybe 20120718 was intended?
                        illumos/20100818) diff_em -xman vendor/illumos/20100818 vendor-sys/illumos/dist@238575 vendor/$b/$s; continue ;;
                        # The original SVN tag of this was omitting r178528 from the userland bits and tagged r178525 instead.
                        opensolaris/20080410) diff_em vendor-cddl/opensolaris/dist/cddl/contrib/opensolaris@178528 $t/$b/$s vendor/$b/$s; continue ;;
                        # git is missing the uts/ tree, as we tagged from 178530 and not r194442 or r194452
                        opensolaris/20080410a) diff_it vendor/$b/$s vendor/$b/$s; continue ;;
                        # need to compare 2 branches, basically already handled above for vendor (not vendor-sys) anyway
                        illumos/*|ngatm/*|opensolaris/*) diff_em $t/$b/$s vendor/$b/$s vendor/$b/$s; continue ;;
                        # this was merged into the proper dist branch
                        ipfilter/dist-old) continue ;;
                        # these got the new layout compared to SVN
                        ipfilter/v3-4-16) diff_it -xmlf_ipl.c -xmln_ipl.c $t/$b/$s/sys vendor/$b-sys/$s; continue ;;
                        # duplicate tag, "3-4-29" is the real one.
                        ipfilter/v3-4-29) continue ;;
                        # compare against pre-flattening, due to splicing
                        # old/new dist, we end up with 2 extra files. I think
                        # this is more correct, it looks like the files have
                        # fallen off the CVS vendor branch. They are in dist,
                        # they should be in the tag.
                        ipfilter/3*|ipfilter/v3*|ipfilter/V3*|ipfilter/4*) diff_it -xmlf_ipl.c -xmln_ipl.c $t/$b/$s@253466 vendor/$b-sys/$s; continue ;;
                        ipfilter/*) diff_it $t/$b/$s vendor/$b-sys/$s; continue ;;
                        # compare against pre-flattening
                        pf/3.7.001|pf/4.1) diff_it $t/$b/$s@181287 vendor/$b-sys/$s; continue ;;
                        pf/*) diff_it $t/$b/$s vendor/$b-sys/$s; continue ;;
                        # we skip some binary backup files
                        heimdal/*) diff_it -x._ltoptions.m4 -x._ltsugar.m4 -x._lt\~obsolete.m4 $t/$b/$s vendor/$b/$s; continue ;;
                        # We put a newer import into dist, somehow this was missed in cvs2svn, compare to the old git hash
                        lomac/dist) diff_it $t/$b/$s vendor/$b/$s\~1; continue ;;
                        #### inlined stuff below here ####
                        # the 1 commit on telnet was inlined into main
                        telnet/*) continue ;;
                        # has just 1 file that was inlined
                        OpenSSH/*) continue ;;
                        # inlined
                        eBones/*) continue ;;
                    esac
                    diff_it $t/$b/$s vendor/$b/$s
                done
            done
        done
        # Things with newer stuff, or resurrected from cvs2svn/branches
        diff_it cvs2svn/branches/NAILabs@260579 vendor/lomac/dist
        diff_it cvs2svn/branches/XEROX@260579 vendor/mrouted/dist
        diff_it cvs2svn/branches/LBL@260579 vendor/rarpd/dist
        diff_it cvs2svn/branches/SUNRPC@260579 vendor/rpcgen/dist
        diff_it cvs2svn/branches/NETGRAPH@260579 projects/netgraph_ppp
        diff_it -r's/gnu/lib/{libg2c,libstdc++/doc/,libstdc++/_G_config.h}' cvs2svn/branches/WIP_GCC31@260579 cvs2svn/branches/WIP_GCC31
        diff_it cvs2svn/branches/MP@36312 cvs2svn/branches/MP

        for t in projects; do
            for b in `svn ls $SVN/$t | grep '/$'`; do
                case $b in
                    graid/|multi-fibv6/|ofed/|pf/|suj/|zfsd/)
                        for s in `svn ls $SVN/$t/$b | grep '/$'`; do
                            diff_it $t/$b$s
                        done
                        continue
                        ;;
                    # empty SVN dir
                    pkgtools/) continue ;;
                esac
                diff_it $t/$b
            done
        done
        for u in `svn ls $SVN/user`; do
            case "$u" in
                eri/) diff_it user/${u}pf45/head user/${u}pf45; continue ;;
                # not converted, nothing in there really
                gad/) continue ;;
            esac
            for b in `svn ls $SVN/user/$u | grep '/$'`; do
                case $b in
                    gssapi/|xenhvm/)
                        for s in `svn ls $SVN/user/$u$b | grep '/$'`; do
                            diff_it user/$u$b$s
                        done
                        continue
                        ;;
                    # missing the .git in git
                    git_conv/) diff_it -x.git -xconfig user/$u$b; continue ;;
                    # has the bogus /etc/rc.d/ppp repo-copied script
                    head_146698/) diff_it -xppp user/$u$b; continue ;;
                esac
                diff_it user/$u$b
            done
        done
        ;;
    doc)
        diff_it head master
        for u in `svn ls $SVN/user`; do
            for b in `svn ls $SVN/user/$u`; do
                diff_it user/$u$b
            done
        done
        for t in projects release; do
            for b in `svn ls $SVN/$t`; do
                case $b in
                    ISBN_1-57176-407-0/) diff_it $t/$b; continue ;;
                    D*|E*|H*|I*|L*|P*) diff_it $t/$b tags/$b; continue ;;
                esac
                diff_it $t/$b
            done
        done
        for b in translations; do
            diff_it $b
        done
        ;;
    ports)
        diff_it head master
        # 2.x and 3.0.0 are broken in SVN, skip EOL tags as well.
        for t in `svn ls $SVN/tags | egrep '^RELEASE_([4-9]|1[0-9]|3_[1-9])_[0-9_]+/'`; do
            diff_it tags/$t `echo $t|sed 's,RELEASE_,release/,; s,_,.,g; s,/$,,'`
        done
        diff_it branches/RELEASE_8_4_0/ releng/8.4.0
        diff_it branches/RELENG_9_1_0 releng/9.1.0
        diff_it branches/RELENG_9_2_0 releng/9.2.0
        for b in `svn ls $SVN/branches | grep Q`; do
            diff_it branches/$b $b
        done
        ;;
esac
fi
