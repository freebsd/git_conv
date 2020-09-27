#!/usr/bin/env zsh

# need a key for cgit and an admin key for repo
if ! ssh-add -l | egrep -q "freebsd" || ! ssh-add -l | egrep -q "FreeBSD.org/admin"; then
    echo "please load both freebsd ssh keys" >&2
    exit 1
fi

# pull repo.freebsd.org
rsync -va --del compost:/j/jails/repo/s/svn/ repo/

# update and pull our svnsync copy on cgit, housed at NYI and pulling itself from:
# % host svn.freebsd.org
# svn.freebsd.org is an alias for svnmir.geo.freebsd.org.
# svnmir.geo.freebsd.org has address 96.47.72.69
# % host 96.47.72.69
# 69.72.47.96.in-addr.arpa domain name pointer svnmir.nyi.freebsd.org.
ssh cgit "cd git_conv && ./svnmir.sh -1 -s doc\ base\ ports"
rsync -va --del cgit:/home/uqs/git_conv/doc . &
rsync -va --del cgit:/home/uqs/git_conv/base . &
rsync -va --del cgit:/home/uqs/git_conv/ports . &
wait

# dump their logs
svn log -vr 1:HEAD file:///$PWD/repo/doc | gzip > repo_doc.log.gz &
svn log -vr 1:HEAD file:///$PWD/repo/base | gzip > repo_base.log.gz &
svn log -vr 1:HEAD file:///$PWD/repo/ports | gzip > repo_ports.log.gz &

svn log -vr 1:HEAD file:///$PWD/doc | gzip > mirror_doc.log.gz &
svn log -vr 1:HEAD file:///$PWD/base | gzip > mirror_base.log.gz &
svn log -vr 1:HEAD file:///$PWD/ports | gzip > mirror_ports.log.gz &
wait

# grab revisions where the metadata differs, usually in the timestamp.
for t in doc base ports; do
    set -- `diff -e =(zgrep "^r[0-9]" mirror_$t.log.gz) =(zgrep "^r[0-9]" repo_$t.log.gz) | egrep -o '^r[0-9]* '`
    if [ $# -ge 1 ]; then
        echo "Found bad metadata in $t for revs $*" >&2
	# And patch them add the remote end. We then need to pull again, of course.
        ssh -A cgit "cd git_conv/$t && for r in $*; do svnsync copy-revprops -r \$r file:///\$PWD svn+ssh://repo.freebsd.org/$t; done"
    fi
done
