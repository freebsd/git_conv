#!/bin/sh

: ${BASE=${PWD}}
REPO=$BASE/freebsd-base.git

if [ "$#" -ne 1 ]; then
    #1.0-RELEASE     # nope
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
fi

for r; do
    rm -rf a.git b.git
    extra_flags=
    case $r in
        *.*.*-RELEASE) tag=${r%-RELEASE} ;;
        *.*-RELEASE) tag=${r%-RELEASE}.0 ;;
    esac
    case $tag in
        2.*) extra_flags='-x eBones -x secure' ;;
        3.*) extra_flags='-x secure -x crypto -x kerberosIV' ;;
        4.*) extra_flags='-x crypto -x kerberosIV -x kerberos5 -x secure' ;;
        5.*) extra_flags='-x crypto -x kerberos5 -x secure -x .cvsignore' ;;
    esac
    git clone -b release/$tag $REPO a.git
    git clone -b release/${tag}_shipped $REPO b.git
    diff -ruN -I'[$](Header|Id|FreeBSD).*[$]' -I',v [0-9].* Exp( .LBL.)?[->";)*/ ]*.?$' -x .git `echo $extra_flags` a.git b.git || exit 1
done
