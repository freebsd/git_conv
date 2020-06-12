# Subversion to Git conversion scripts for the FreeBSD project

These are the scripts and data used for the svn to git conversion of the
FreeBSD repositories. Your help is requested in making sure the produced
result is an acceptable representation of the current SVN repository. Fixing
some few historical glitches are in scope if the effort is reasonable. Please
let us know what you find.

## Gimme the repo!

```
git clone https://cgit-beta.freebsd.org/src.git && cd src
git config --add remote.origin.fetch '+refs/notes/*:refs/notes/*' && git fetch
```

Same for the `doc` and `ports` repos. You can expect some things to be
different. Most importantly you should check whether branch and mergepoints
(especially for vendor branches, but also project branches) are there and make
sense.

- `user/` branches are never merged back into `master`
- vendor tags are sometimes branches, sometimes tags, this is WIP.
- `release/1.0_shipped` et al. are snapshots of the checked out CVS source
  code, including expanded $Id$ tags.
- no other tags are expanded
- various vendor-foo suffixes have been collapsed into 1 vendor namespace,
  except for a few vendors where merging the userland and kernel bits is not
  straightforward due to how they interleave with the merge and branch history.

If you would want to run the conversion yourself and play with the rules, read on.

## Required Software

`pkg install qt5-qmake qt5-core subversion git shlock`

(for Debian: `apt install qt5-qmake qtbase5-dev libapr1-dev libsvn-dev subversion git shlock`)

A patched copy of svn2git aka. svn-all-fast-export has been added to this repo using:
`git subtree add --squash --prefix svn2git https://github.com/freebsd/svn2git master`

```shell
cd svn2git && qmake && make && cd ..
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

On on moderately fast system with an SSD and/or enough RAM for the buffer cache,
this should take about 2h to finish for `src` and will produce about 10GiB of
intermediate data. The final `src` repo size should be around 1.7GiB.

## What you get

While the conversion will try to convert all branches (except for some truly
degenerate cases), we will only publish a small set of branches in the official
repo, the _other_ branches will be published to an _archive_ repo.

- src will have: master, stable/N, releng/N, release/N.M, vendor/\*
- doc will have: master, release/N.M
- ports will have: master, branches/YYYYQx

In the future, _project_ branches will be individual forks of the repos.

Further information and documentation can be found on the Wiki sites at
https://github.com/freebsd/git_conv/wiki

## License
The included svn2git is GPLv3; see svn2git/LICENSE. The scripts and configuration files are licensed under
[CC0](https://creativecommons.org/publicdomain/zero/1.0/legalcode).
