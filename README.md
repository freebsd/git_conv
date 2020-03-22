# Subversion to Git conversion scripts for the FreeBSD project

These are the scripts and data used for the svn to git conversion of the
FreeBSD repositories

## Required Software

`pkg install qmake qt5-core subversion git shlock`

A patched copy of svn2git aka. svn-all-fast-export has been added to this repo using:
`git subtree add --squash --prefix svn2git https://github.com/freebsd/svn2git master`

```shell
cd svn2git && qmake && make
```

## Setup

You'll need to download a seed of the SVN repository dump (it takes weeks to bootstrap otherwise).

```shell
fetch https://download.freebsd.org/ftp/development/subversion/svnmirror-base-r358354.tar.xz
fetch https://download.freebsd.org/ftp/development/subversion/svnmirror-ports-r527184.tar.xz
fetch https://download.freebsd.org/ftp/development/subversion/svnmirror-doc-r53937.tar.xz
tar xf svnmirror-base-r358354.tar.xz
tar xf svnmirror-doc-r53937.tar.xz
tar xf svnmirror-ports-r527184.tar.xz
```

## Conversion runs

- (optional:) fetch the current SVN state using `./svnmir.sh -1`
- convert either base, doc, or ports using `./git_conv { base | doc | ports }`

Additional runs of `svnmir.sh` or `git_conv` will run incrementally.

## License
The included svn2git is GPLv3; see svn2git/LICENSE. The scripts and configuration files are licensed under
[CC0](https://creativecommons.org/publicdomain/zero/1.0/legalcode).
