# Subversion to Git conversion scripts for the FreeBSD project

These are the scripts and data used for the svn to git conversion of the
FreeBSD repositories. Your help is requested in making sure the produced
result is an acceptable representation of the current SVN repository. Fixing
some few historical glitches are in scope if the effort is reasonable. Please
let us know what you find.

## Errata

1. An oversight in the vendor branches resulted in the history of the ipfilter
   kernel parts being chopped in two. There is `vendor/ipfilter-sys` for the
   newer commits and `vendor/ipfilter-sys-old` for the older commits. Both of
   those heads were merged into `main` with commit bfc88dcbf709 or SVN r255332.
   A `git replace` object was put into place to join these two histories, but
   you need to fetch `refs/replace/` to make that visible. A fixed ruleset is
   in the errata_1 branch of this repo, see also
   https://github.com/freebsd/git_conv/issues

1. Somewhere along the conversion, the `--identity-domain` flag was dropped,
   resulting in some author/committer emails ending in "@localhost".
   Furthermore, a missing author map entry was made non-fatal in the upstream
   code, and this was missed, resulting in a few commits not having a full name
   entry as well.
   This affects 1 commit in the src repo by "davidg" and a bunch of them in the doc repo:
   ```
   % git log --all | egrep "^Author: [a-z]" | sort | uniq -c
     1 Author: bean <bean@localhost>
     6 Author: davidg <davidg@localhost>
     3 Author: jmc <jmc@localhost>
   145 Author: nsj <nsj@localhost>
     1 Author: skynyrd <skynyrd@localhost>
     1 Author: svn2git <svn2git@FreeBSD.org>
     7 Author: viny <viny@localhost>
    28 Author: www <www@localhost>
   ```


## Gimme the repo!

```
git clone https://git.freebsd.org/src.git && cd src
git config --add remote.origin.fetch '+refs/notes/*:refs/notes/*'
git config --add remote.origin.fetch '+refs/replace/*:refs/replace/*'
git fetch
```

Same for the `doc` and `ports` repos. You can expect some things to be
different. Most importantly you should check whether branch and mergepoints
(especially for vendor branches, but also project branches) are there and make
sense.

Neither `user` or `projects` branches are "visible" by default.
`backups` refs are deleted branches and `cvs2svn` contains some of the detritus
left over from the CVS days and the cvs2svn conversion. The `internal`
namespace for now only has the `access` and `mentors` file, detailing when
people got their various commit bits. We will likely move these to a combined
repo for doc, src, and ports.

```
git config --add remote.origin.fetch '+refs/projects/*:refs/projects/*'
git config --add remote.origin.fetch '+refs/user/*:refs/user/*'
git config --add remote.origin.fetch '+refs/backups/*:refs/backups/*'
git config --add remote.origin.fetch '+refs/cvs2svn/*:refs/cvs2svn/*'
git config --add remote.origin.fetch '+refs/internal/*:refs/internal/*'
git fetch
```

Note that `internal`, `projects` and `user` branches also exist for the `doc`
repo and `ports` has `projects` and `releng` as well. cvs2svn and
backups are exclusive to the `src` repo though.

- `user/` branches are never merged back into `master`
- MFHs into `user/` or `projects/` branches are just cherry-picks to keep `git
  log --graph` somewhat readable and as merges wouldn't convey any useful
  information, really.
- `vendor` **tags** were never flattened post-creation, as that would advance
  them off of the mainline branch and make them invisible to a simple `git log`
- `release/1.0_cvs` et al. are snapshots of the checked out CVS source
  code, including expanded $Id$ tags.
- no other keywords are expanded
- various vendor-foo suffixes have been collapsed into 1 vendor namespace,
  except for a few vendors where merging the userland and kernel bits is not
  straightforward due to how they interleave with the merge and branch history.
  This is what also caused Errata 1.
- some branches have their history "extended", that is, commits under the
  `cvs2svn` area were properly attached.
- ... and most of these commits have actually been inlined directly into the
  mainline tree to keep the history more "linear" and associate the commit with
  the original author and commit message.

## How to analyze the results

Here are some tips that were deemed useful in making sense of resulting
history. Please add a "graph notes log" alias to your `.gitconfig`:
```
[alias]
  gnlog = log --graph --pretty=format:'%Cred%h %C(green)%t %Creset %C(red)%ad %Creset-%C(yellow)%d%Creset %s %n      %N %-GG' --date=short
```

### Show the full tree of a vendor area

```
git gnlog `git show-ref|grep vendor/sendmail|cut -d" " -f1`
```

### Find a certain SVN revision on master

```
git show -p `git log --format=%h --notes --grep=revision=294706$`
```

### Show how/where/when a vendor branch was merged into master over time

```
git gnlog vendor/zstd/dist master
```
(but you'll need to search in the massive output for where the vendor branch is
being merged, if you know a better way to represent this, please let us know!)

### Look for commits with more than 5 parents and log them

```
git log --format='%H %P' --all|awk '{if (NF > 5) { print NF " " $0}}'|sort -rn|cut -d" " -f2|xargs -n1 -I% git snlog -n 1 %
```

If you want to run the conversion yourself and play with the rules, read on.

## Required Software

`pkg install qt5-qmake qt5-core subversion git shlock`

(for Debian: `apt install qt5-qmake qtbase5-dev libapr1-dev libsvn-dev subversion git shlock`)

A patched copy of svn2git aka. svn-all-fast-export has been added to this repo using:
`git subtree add --squash --prefix svn2git https://github.com/freebsd/svn2git master`

A copy of `parsecvs` has been added using:
`git subtree add --squash --prefix parsecvs https://github.com/BartMassey/parsecvs master`

```shell
cd svn2git && qmake && make && cd ..
cd parsecvs && make && cd ..
```

## Setup

You'll need to download a seed of the SVN repository dump (it takes weeks to bootstrap otherwise).
NOTE: Depending on the mirror nearest to you, the metadata of this mirrors is
actually off by several seconds for several of the commits and is also
sometimes missing the author. Furthermore, using the one-true SVN repo as the
source will drop all forced commits. Don't ask me why this happens.

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

NOTE: This is not longer true, while it works fine for git version 2.24 and
2.25, at least version 2.27 will cause git-fast-import upon reading the marks
file to eat up all RAM and eventually crash with out of memory.

On on moderately fast system with an SSD and/or enough RAM for the buffer cache,
this should take about 2h to finish for `src` and will produce about 10GiB of
intermediate data. The final `src` repo size should be around 1.7GiB.

Re-runs will re-use the previous packfiles and will likely be faster. Currently
`src` finishes in about 1 hour, `doc` in 2 minutes and `ports` in 45 minutes.

## What you get

- src will have: master, stable/N, releng/N, release/N.M, vendor/\*
- doc will have: master, release/N.M
- ports will have: master, branches/YYYYQx

In the future, _project_ branches will be individual forks of the repos. Hosted
either on GitHub, GitLab or our own infrastructure.

Further information and documentation can be found on the Wiki sites at
https://github.com/freebsd/git_conv/wiki

## For the curious

`blaming` the whole project takes about a day of wall clock time using this naive approach:
```
git ls-tree -r --name-only -z HEAD|xargs -0n1 git blame -e --line-porcelain | sed -n 's/^author-mail //p' | sort -f | uniq -ic | sort -nr
```
It only blames the current state of the repo, not all of history, mind you.
After several hairy fixes, this could be reduced from more than 6000 lines down
to just 182 lines that are now falsely attributed to author "cvs2svn". The
files are `gnu/usr.bin/grep/AUTHORS` and a few under `usr.sbin/ppp`.

## License
The included svn2git is GPLv3; see svn2git/LICENSE.
The included parsecvs is GPLv2; see parsecvs/COPYING.
The scripts and configuration files are licensed under
[CC0](https://creativecommons.org/publicdomain/zero/1.0/legalcode).
