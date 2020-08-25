# Switchover Checklist

## One week before
*  email reminder
*  perform trial run of conversion process
*  maybe lock the vendor area

## One day before
*  email reminder

## Flag day
*  send email that migration process is starting
*  make SVN repo read-only
*  create 3 SVN repo dump tarballs

## Conversion process
*  [1h] copy and extra repo tarballs
*  [2h, parallel] update svn_log.txt output, gzip, commit to git_conv repo
*  [2h, parallel] run svneverever, capture output, commit to git_conv repo
*  [10m] run author.sh, commit to git_conv repo should there be changes
*  [6h, parallel] convert with debug rules, 3x
  * **CHECK FOR NEW UNHANDLED MERGEINFO**
*  [1h] run fix_bogus_tags.sh
*  [8h, but can start shortly after the conversion kicked off] run compare_branches.sh -k
  *  This requires sudo and likely doesn't work in the jail anyway, need to undo the mdconfig hack
*  [1h] git push
*  **WE ARE LIVE**
*  run gc --aggressive on the server repo
*  gzip log-base log-freebsd-base.git gitlog-freebsd-base.git and commit to git_conv repo
*  ditto for doc and ports
