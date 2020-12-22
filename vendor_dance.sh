#!/bin/sh

# NOTE: this only works in the converter output. Due to the push refspec the
# repo target will have them in a different place and this requires an update
# to the push config as well.

git=${1:-freebsd-base.git}
cd $git

git pack-refs --all
rm -rf refs/*

# special cases first.
git show-ref | egrep ' refs/heads/vendor/(bind9|clang|compiler-rt|libc..|lld|lldb|llvm-libunwind|llvm-openmp|llvm|openssl)/dist-' |
    while read hash ref; do
	case $ref in
	    # refs/heads/vendor/bind9/dist-9.4
	    */vendor/bind9/dist*|*/vendor/openssl/dist*)
		newref=`echo $ref | sed -E 's,(bind9|openssl)/dist-([0-9.]*),\1-\2,'`;
		;;
	    # refs/heads/vendor/clang/dist-release_60
	    *)
		newref=`echo $ref | sed -e 's,/\([^/]*\)/dist-release[-_]\([0-9.]*\),/\1-\2,'`;
		;;
	esac
	echo "Moving $ref to $newref" >&2
	git update-ref -d $ref
	git update-ref $newref $hash
    done

git pack-refs --all
(cd refs && find . -depth -type d -empty -delete)

# snip off /dist where possible, but we have existing dirs in there we need to remove first.
git show-ref | grep ' refs/heads/vendor.*/dist$' |
    while read hash ref; do
	newref=`echo $ref | sed -e 's,/dist$,,'`;
	case $ref in
	    # Yours truly missed this in the converter rules :(
	    # TODO(uqs): add a replace/graft to splice them back together
	    # r255332 merged vendor-sys/ipfilter/dist into main (2x sys, 1x userland)
	    */vendor-sys/ipfilter/dist)
		newref=refs/heads/vendor/ipfilter-sys-old
		;;
	    */vendor/NetBSD/dist)
		newref=refs/heads/vendor/NetBSD/misc
		;;
	    */vendor/misc-GNU/dist)
		# yeah, sorry ...
		newref=refs/heads/vendor/misc-GNU/misc
		;;
	esac
	echo "Moving $ref to $newref" >&2
	git update-ref -d $ref
	git update-ref $newref $hash
    done
git pack-refs --all
