#!/bin/sh

(
# Needed as some authors/logins are missing on freefall
# alm, asami, ats, conklin, cvs2svn, dick, dillon, dyson, erich,
# gclarkii, gehenna, jamil, jraynard, olah, pst, ugen, vkashyap, zarzycki
cat << EOS
alm = Andrew Moore <alm@FreeBSD.org>
asami = Satoshi Asami <asami@FreeBSD.org>
ats = Andreas Schulz <ats@FreeBSD.org>
conklin = J.T. Conklin <jtc@FreeBSD.org>
cvs2svn = cvs2svn <cvs2svn@FreeBSD.org>
dick = Richard Seaman Jr. <dick@FreeBSD.org>
dillon = Matthew Dillon <dillon@FreeBSD.org>
dyson = John Dyson <dyson@FreeBSD.org>
erich = Eric L. Hernes <erich@FreeBSD.org>
gclarkii = Gary Clark II <gclarkii@FreeBSD.org>
gehenna = Masahide MAEKAWA <gehenna@FreeBSD.org>
jamil = Jamil J. Weatherbee <jamil@FreeBSD.org>
jraynard = James Raynard <jraynard@FreeBSD.org>
olah = Andras Olah <olah@FreeBSD.org>
pst = Paul Traina <pst@FreeBSD.org>
ugen = Ugen J.S. Antsilevich <ugen@FreeBSD.org>
vkashyap = Vinod Kashyap <vkashyap@FreeBSD.org>
yar = Yar Tikhiy <yar@FreeBSD.org>
zarzycki = Dave Zarzycki <zarzycki@FreeBSD.org>
EOS

ssh freefall "getent passwd" | \
awk -F: '{sub(/,.*/, "", $5); sub(/;.*/, "", $5); printf "%s = %s <%s@FreeBSD.org>\n", $1, $5, $1}'
) | \
# squash duplicates
awk '{a[$1] = $0} END{for (v in a) {print a[v]}}' | sort > authors.txt
