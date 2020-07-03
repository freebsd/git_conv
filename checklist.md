# Switchover Checklist

## One week before
*  email reminder
*  perform trial run of conversion process

## One day before
*  email reminder

## Flag day
*  send email that migration process is starting
*  make SVN repo read-only

## Conversion process
*  [1h] run svnmir.sh
*  [2h, parallel] update svn_log.txt output, gzip, commit to repo
*  [2h, parallel] run svneverever, capture output, commit to repo
*  [10m] run author.sh
*  [6h, parallel] convert with debug rules, 3x
*  [1h] run fix_bogus_tags.sh
*  [8h, but can start shortly after the conversion kicked off] run compare_branches.sh -k
  *  This requires sudo and likely doesn't work in the jail anyway, need to undo the mdconfig hack
*  [1h] git push
*  **WE ARE LIVE**
*  run gc --aggressive on the server repo
*  gzip log-base log-freebsd-base.git gitlog-freebsd-base.git and commit to repo
*  ditto for doc and ports
