# Switchover Checklist

*  make SVN repo read-only
*  wait for old-converter to settle
*  disable cronjobs for old-converter (2x! sic!)
*  pull an rsync copy of the whole thing from git-beta.freebsd.org
*  disable cronjob for new-converter, check it has updated fully
*  ./pull_and_patch_mirror.sh
*  [1h]
*  commit repo_doc.log.gz to git_conv repo
*  [10m] run author.sh, commit to git_conv repo should there be changes
*  convert with debug rules locally with a clean start!
  * **CHECK FOR NEW UNHANDLED MERGEINFO**
*  compare hashes
*  [2h--4h]
*  src only: run fix_bogus_tags.sh
*  [8h, but can start shortly after the conversion kicked off] run compare_branches.sh -k
  *  This requires sudo and likely doesn't work in the jail anyway, need to undo the mdconfig hack
  *  Or just run the copy @home and compare final hashes for a quorum vote.
*  **WE ARE LIVE**
*  git push delete all the non-master branches on Github
*  git push main to Github
*  configure Github's default branch to main (and don't forget to eventually GC master)
*  xz/gzip log-base log-freebsd-base.git gitlog-freebsd-base.git and commit to git_conv repo
*  ditto for doc and ports
