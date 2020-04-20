#!/bin/sh

# svnsync is non-atomic.  Every commit is done in two distinct steps.
# 1: the commit is replayed as a transaction.
# 2: after the commit, the metadata is copied as a *separate* transaction.
# This makes svnsync -> svnsync chains annoying as there is a brief window
# between #1 and #2 where the repo is unlocked and bogus metadata is
# visible. (!!!)
# As a workaround, recheck the svn:author property of the last few
# revisions and re-copy as needed.  This reduces the window to a few
# seconds.  Still not good but better than no recovery at all.

usage() {
  echo "Usage: $0 [-1] [-l logdir ] [-r repodirs] [-s setlist]" 1>&2
  exit 1
}

umask 002
me=$(id -un)

once=false
logdir=$PWD
repodirs=$PWD
setlist=""

while getopts "1l:r:s:" _opt; do
  case "$_opt" in
  1) once=true ;;
  l) logdir="$OPTARG" ;;
  r) repodirs="$OPTARG" ;;
  s) setlist="$OPTARG" ;;
  *) usage ;;
esac
done
shift $(($OPTIND - 1))
if [ $# -ne 0 ]; then
  usage
fi

if [ -z "${setlist}" ]; then
  for r in base doc ports; do
    if [ -d ${repodirs}/$r ]; then
      setlist="${setlist} $r"
    fi
  done
fi

# Only ever run this under the dosync.lock
for r in ${setlist}; do
  echo "Startup: $(date)" >> ${logdir}/svnsync-$r.log
done

t0=$(date +%s)

# Pack repo once at startup
for r in ${setlist}; do
  svnadmin pack ${repodirs}/$r >> ${logdir}/svnsync-$r.log 2>&1
done

while :; do
  for r in ${setlist}; do
    locked=$(svn propget --revprop -r 0 svn:sync-lock file://${repodirs}/$r 2>/dev/null)
    if [ -n "$locked" ]; then
      svn propdel --revprop -r 0 svn:sync-lock file://${repodirs}/$r >>${logdir}/svnsync-$r.log 2>&1
    fi
    oldrev=$(svn propget --revprop -r 0 svn:sync-last-merged-rev file://${repodirs}/$r 2>>${logdir}/svnsync-$r.log)
    svnsync --non-interactive sync file://${repodirs}/$r >> ${logdir}/svnsync-$r.log 2>&1
    newrev=$(svn propget --revprop -r 0 svn:sync-last-merged-rev file://${repodirs}/$r 2>>${logdir}/svnsync-$r.log)
    oldrev=$(($oldrev - 10))
    for rev in $(seq $oldrev $newrev); do
      a=$(svn propget --revprop -r $rev svn:author file://${repodirs}/$r 2>>${logdir}/svnsync-$r.log)
      if [ "x$a" == "x$me" ]; then
	# Got garbage metadata, copy revprops again. This only checks for
	# obvious snafus about the commit author, but not other problems like
	# different timestamps in the metadata :/
        svnsync copy-revprops -r $rev file://${repodirs}/$r >> ${logdir}/svnsync-$r.log 2>&1
      fi
    done
  done

  if $once; then
    exit 0
  fi
  # exit and restart once an hour in case of a script update
  now=$(date +%s)
  elapsed=$(( $now - $t0 ))
  # slightly less than 1 hour so we catch the cron restart asap
  if [ ${elapsed} -gt 3590 ]; then
    exit 0
  fi
  sleep 300
done
