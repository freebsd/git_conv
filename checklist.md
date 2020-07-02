# Day of Switchover Checklist

*  run svnmir.sh
*  update svn_log.txt output, gzip, commit to repo
*  run author.sh
*  convert with debug rules, 3x
*  run fix_tags.sh
*  run compare_branches.sh
*  run svneverever, capture output, commit to repo
*  gzip log-base log-freebsd-base.git gitlog-freebsd-base.git and commit to repo
*  ditto for doc and ports
*  git push
*  run gc --aggressive on the server repo
