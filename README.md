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

Neither `vendor`, `user` or `projects` branches are "visible" by default.
`backups` refs are deleted branches and `cvs2svn` contains some of the detritus
left over from the CVS days and the cvs2svn conversion.

```
git config --add remote.origin.fetch '+refs/vendor/*:refs/vendor/*'
git config --add remote.origin.fetch '+refs/projects/*:refs/projects/*'
git config --add remote.origin.fetch '+refs/user/*:refs/user/*'
git config --add remote.origin.fetch '+refs/backups/*:refs/backups/*'
git config --add remote.origin.fetch '+refs/cvs2svn/*:refs/cvs2svn/*'
git fetch
```

Note that `projects` and `user` branches also exist for the `doc` repo and
`ports` has `projects` and `releng` as well. vendor, cvs2svn and backups
are exclusive to the `src` repo though.

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
git show -p `git log --format=%h --notes --grep=revision=294706`
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

NOTE: This is not longer true, while it works fine for git version 2.24 and
2.25, at least version 2.27 will cause git-fast-import upon reading the marks
file to eat up all RAM and eventually crash with out of memory.

On on moderately fast system with an SSD and/or enough RAM for the buffer cache,
this should take about 2h to finish for `src` and will produce about 10GiB of
intermediate data. The final `src` repo size should be around 1.7GiB.

## What you get

- src will have: master, stable/N, releng/N, release/N.M
- doc will have: master, release/N.M
- ports will have: master, branches/YYYYQx

In the future, _project_ branches will be individual forks of the repos. The
`vendor` area is not visible by default, as it's only relevant for maintainers
of contrib software.

Further information and documentation can be found on the Wiki sites at
https://github.com/freebsd/git_conv/wiki

## For the curious

`blaming` the whole project takes about a day of wall clock time using this naive approach:
```
git ls-tree -r --name-only -z HEAD|xargs -0n1 git blame -e --line-porcelain | sed -n 's/^author-mail //p' | sort -f | uniq -ic | sort -nr
```
It only blames the current state of the repo, not all of history, mind you.
Sadly, it currently comes up with 6484 lines owned by "cvs2svn".

## License
The included svn2git is GPLv3; see svn2git/LICENSE. The scripts and configuration files are licensed under
[CC0](https://creativecommons.org/publicdomain/zero/1.0/legalcode).
