#!/bin/sh

n=`sysctl -n hw.ncpu 2>/dev/null`
n=${n:-4}

trap 'rm -f "$TMPFILE"' EXIT
TMPFILE=$(mktemp -t `basename $0`) || exit 1

cat > $TMPFILE << EOS
#!/bin/sh
git ls-tree "\$@" | grep -Eq "	(head|dist)(/|$)" && {
    git ls-tree "\$@" | grep -Eq "	head(/|$)" && { echo -n "h "; git log -n1 --pretty=format:"%h %ad %N" "\$@"; };
    git ls-tree "\$@" | grep -Eq "	dist(/|$)" && { echo -n "d "; git log -n1 --pretty=format:"%h %ad %N" "\$@"; };
};
EOS

git rev-list --all --abbrev-commit | xargs -n1 -P$n  sh $TMPFILE | sed '/^$/d'
