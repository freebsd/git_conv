#!/bin/sh

svn=${1:-base}
git=${2:-freebsd-base.git}
branch=${3:-master}
export GIT_DIR=$git

# Print the potential MFV merge commits that have only a single parent
git log --pretty=format:'%h %p %ad %N' --grep="^MF[VPp]" --max-parents=1 master $branch | sed '/^$/d'

# Alternative approach, list all vendor/foo/dist refs that are *not* reachable
# from master. These are changed vendor imports that a) haven't been recorded
# as merges or b) were never merged. If we also do it for all tags we get c)
# tags that have been advanced after the merge, usually because they were
# "flattened". We want to avoid that, so that the refs stick to the actual
# vendor branch.
# That is, this is useless:
#  % git glog vendor/misc-GNU/patch/dist vendor/misc-GNU/patch/2.4 vendor/misc-GNU/patch/2.5
#  * fb6b9b91d11d - (vendor/misc-GNU/patch/dist, vendor/misc-GNU/patch/2.5.9) Virgin import of patch-2.5.9 ...
#  * fea6cc68e360 - (vendor/misc-GNU/patch/2.5) Flatten gnu-patch vendor tree.
#  * 98c7cc311fbd - (refs/backups/r253716/heads/vendor/misc-GNU/patch/dist) Raw import of patch 2.5
#  | * d57baaf7039a - (vendor/misc-GNU/patch/2.4) Flatten gnu-patch vendor tree.
#  |/
#  * 511633a2fe6a - Import of GNU patch version 2.4.
#
# We don't want the 2.4 tag to stick out, it wouldn't show up when just logging /dist.

# TODO: is this maybe just `git tag --no-merged`?
git show-ref dist | sed 's,.* refs/heads/,,; s,/dist,,' | while read v; do
  git show-ref --heads --tags | fgrep $v | while read sha ref; do
      if ! git --git-dir=$git merge-base --is-ancestor $sha $branch; then
	  echo "WARNING: $ref isn't reachable from $branch"
      fi
  done
done


# Show un-merged vendor tags, these need manual inspection for why they were
# not properly recorded, often this is because the tag was cvs2svn manufactured
# :/
git tag --sort=-taggerdate --no-merged master vendor/\*
