# SVN revisions of interest in the timeline

So here's what cvs2svn produced (not its fault, mind you).
r8869 | cvs2svn | 1995-05-30 07:50:54 +0200 (Tue, 30 May 1995) | 2 lines
   A /stable/2.0.5 (from /head:8852)
r8882 | cvs2svn | 1995-05-30 10:29:08 +0200 (Tue, 30 May 1995) | 1 line
   A /releng/2.0.5 (from /head:8881)
r9211 | cvs2svn | 1995-06-13 20:05:17 +0200 (Tue, 13 Jun 1995) | 1 line
   A /stable/2.1 (from /head:9202)
r19327 | cvs2svn | 1996-11-02 11:41:29 +0100 (Sat, 02 Nov 1996) | 1 line
   A /stable/2.2 (from /head:19320)
r42951 | cvs2svn | 1999-01-21 01:55:31 +0100 (Thu, 21 Jan 1999) | 1 line
   A /stable/3 (from /head:42948)
r57955 | cvs2svn | 2000-03-13 05:59:44 +0100 (Mon, 13 Mar 2000) | 1 line
   A /stable/4 (from /head:57954)
r57956 | cvs2svn | 2000-03-13 05:59:45 +0100 (Mon, 13 Mar 2000) | 1 line
   A /releng/4.4 (from /stable/4:57955)
r57960 | cvs2svn | 2000-03-13 05:59:49 +0100 (Mon, 13 Mar 2000) | 1 line
   A /releng/4.5 (from /stable/4:57955)
r63491 | cvs2svn | 2000-07-19 08:22:02 +0200 (Wed, 19 Jul 2000) | 1 line
   A /releng/4.6 (from /stable/4:63490)
r66473 | cvs2svn | 2000-09-30 04:49:38 +0200 (Sat, 30 Sep 2000) | 1 line
   A /releng/4.7 (from /stable/4:66472)
r75750 | cvs2svn | 2001-04-21 02:04:30 +0200 (Sat, 21 Apr 2001) | 1 line
   A /releng/4.3 (from /stable/4:75749)
r101416 | cvs2svn | 2002-08-06 10:24:47 +0200 (Tue, 06 Aug 2002) | 1 line
   A /releng/4.11 (from /stable/4:101415)
r101418 | cvs2svn | 2002-08-06 10:24:49 +0200 (Tue, 06 Aug 2002) | 1 line
   A /releng/4.9 (from /stable/4:101415)
r107811 | cvs2svn | 2002-12-13 07:54:34 +0100 (Fri, 13 Dec 2002) | 1 line
   A /releng/5.0 (from /head:107810)
r108855 | cvs2svn | 2003-01-07 05:28:56 +0100 (Tue, 07 Jan 2003) | 1 line
   A /releng/4.10 (from /stable/4:108854)
r111744 | cvs2svn | 2003-03-02 17:42:41 +0100 (Sun, 02 Mar 2003) | 1 line
   A /releng/4.8 (from /stable/4:111743)
r115436 | cvs2svn | 2003-05-31 13:28:29 +0200 (Sat, 31 May 2003) | 1 line
   A /releng/5.1 (from /head:115435)
r123193 | cvs2svn | 2003-12-07 04:02:28 +0100 (Sun, 07 Dec 2003) | 1 line
   A /releng/5.2 (from /head:123192)
r133968 | cvs2svn | 2004-08-18 18:37:05 +0200 (Wed, 18 Aug 2004) | 1 line
   A /stable/5 (from /head:133920)
r133969 | cvs2svn | 2004-08-18 18:37:06 +0200 (Wed, 18 Aug 2004) | 1 line
   A /releng/5.3 (from /stable/5:133968)
r133971 | cvs2svn | 2004-08-18 18:37:08 +0200 (Wed, 18 Aug 2004) | 1 line
   A /releng/5.4 (from /stable/5:133968)
r147906 | cvs2svn | 2005-07-11 06:14:43 +0200 (Mon, 11 Jul 2005) | 1 line
   A /stable/6 (from /head:147905)
r147907 | cvs2svn | 2005-07-11 06:14:44 +0200 (Mon, 11 Jul 2005) | 1 line
   A /releng/6.0 (from /stable/6:147906)
r147908 | cvs2svn | 2005-07-11 06:14:45 +0200 (Mon, 11 Jul 2005) | 1 line
   A /releng/6.1 (from /stable/6:147906)
r147921 | cvs2svn | 2005-07-11 06:14:58 +0200 (Mon, 11 Jul 2005) | 1 line
   A /releng/6.2 (from /stable/6:147906)
r147922 | cvs2svn | 2005-07-11 06:14:59 +0200 (Mon, 11 Jul 2005) | 1 line
   A /releng/6.3 (from /stable/6:147906)
r158462 | cvs2svn | 2006-05-12 03:09:20 +0200 (Fri, 12 May 2006) | 1 line
   A /releng/5.5 (from /stable/5:158461)
r172506 | cvs2svn | 2007-10-10 18:59:15 +0200 (Wed, 10 Oct 2007) | 1 line
   A /stable/7 (from /head:172505)
r172507 | cvs2svn | 2007-10-10 18:59:16 +0200 (Wed, 10 Oct 2007) | 1 line
   A /releng/7.0 (from /stable/7:172506)

Yes, 4.3 gets created after 4.7 and 4.11 obviously before 4.10 and 4.8.
releng/5.1 and 5.2 off of head are ok, I think. stable/5 was created after
that.
This is likely due to CVS repo-copies and moves.

## Real branch points

Working backwards. RELENG\_8 was created in SVN land, so it is sane.

To quickly see the non-sense at the start of some branches, one can use e.g.
```
git log --compact-summary --reverse stable/7..releng/7.4
```
where it's obvious where the "big diff" landed, i.e. 99% of the files started
appearing on the tag (incl. COPYRIGHT and README for example). Everything
predating that is likely due to repo-copies and tag slips.

| Branch | cvs2svn | Date | CVS actual | Date |
|--------|---------|------|------------|------|
| releng/7.4    | r216643 | 2010-12-22 | n/a | n/a |
| releng/7.3    | r203738 | 2010-02-10 | n/a | n/a |
| releng/7.2    | r191094 | 2009-04-15 | n/a | n/a |
| releng/7.1    | r185312 | 2008-11-25 | n/a | n/a |
| releng/7.0    | r172507 | 2007-10-10 | r174854 | 2007-12-22 |
| stable/7      | r172506 | 2007-10-10 | r172506 | 2007-10-10 |
| releng/6.4    | r183538 | 2008-10-02 | n/a | n/a |
| releng/6.3    | r147922 | 2005-07-11 | r173886 | 2007-11-24 |
| releng/6.2    | r147921 | 2005-07-11 | r164286 | 2006-11-14 |
| releng/6.1    | r147908 | 2005-07-11 | r158179 | 2006-04-30 |
| releng/6.0    | r147907 | 2005-07-11 | r151174 | 2005-10-09 |
| stable/6      | r147906 | 2005-07-11 | " | " |
| releng/5.5    | r158462 | 2006-05-12 | " | " |
| releng/5.4    | r133971 | 2004-08-18 | r145335 | 2005-04-20 |
| releng/5.3    | r133969 | 2004-08-18 | r136588 | 2004-10-16 |
| stable/5      | r133968 | 2004-08-18 | " | " |
| releng/5.2    | r123193 | 2003-12-07 | " | " |
| releng/5.1    | r115436 | 2003-05-31 | " | " |
| releng/5.0    | r107811 | 2002-12-13 | " | " |
| releng/4.11   | r101416 | 2002-08-06 | r139026 | 2004-12-18 |
| releng/4.10   | r108855 | 2003-01-07 | r128520 | 2004-04-21 |
| releng/4.9    | r101418 | 2002-08-06 | r121369 | 2003-10-22 |
| releng/4.8    | r111744 | 2003-03-02 | r112503 | 2003-03-22 |
| releng/4.7    |  r66473 | 2000-09-30 | r104537 | 2002-10-05 |
| releng/4.6    |  r63491 | 2000-07-19 |  r97920 | 2002-06-06 |
| releng/4.5    |  r57960 | 2000-03-13 |  r89800 | 2002-01-25 |
| releng/4.4    |  r57956 | 2000-03-13 |  r83457 | 2001-09-11 |
| releng/4.3    |  r75750 | 2001-04-21 | " | " |
| release/4.2.0 |  r68931 | 2000-11-20 |  r68914 | 2000-11-19 |
| release/4.1.1 |  r66373 | 2000-09-25 |  r66334 | 2000-09-25 |
| release/4.1.0 |  r63895 | 2000-07-26 |  r63884 | 2000-07-26 |
| release/4.0.0 |  r58337 | 2000-03-20 |  r58046 | 2000-03-14 |
| stable/4      |  r57955 | 2000-03-13 | " | " |
| stable/3      |  r42951 | 1999-01-21 | " | " |
| stable/2.2    |  r19327 | 1996-11-02 | " | " |
| stable/2.1    |   r9211 | 1995-06-13 | " | " |
| stable/2.0.5  |   r8869 | 1995-05-30 | " | " |


## releng/4.6 to releng/4.4

This is broken in SVN. It seems that the tag was slipped on /crypto, as the
"big copy" in r99348 (which post-dates the actual release date, mind you)
copies all files from stable/4 from various points in time. The highest is
crypto from the immediate previous revision r99347. The release tag is
similarly messed up.

```
------------------------------------------------------------------------
r99348 | cvs2svn | 2002-07-03 15:01:42 +0200 (Wed, 03 Jul 2002) | 1 line
Changed paths:
   A /releng/4.6/COPYRIGHT (from /stable/4/COPYRIGHT:57955)
   A /releng/4.6/Makefile (from /stable/4/Makefile:96712)
   A /releng/4.6/Makefile.inc1 (from /stable/4/Makefile.inc1:95670)
   A /releng/4.6/Makefile.upgrade (from /stable/4/Makefile.upgrade:57955)
   A /releng/4.6/README (from /stable/4/README:95503)
...
   A /releng/4.6/crypto (from /stable/4/crypto:99347)
...
   A /releng/4.6/release/sysinstall/menus.c (from /stable/4/release/sysinstall/menus.c:97903)
...
   A /releng/4.6/sys/dev/fxp (from /stable/4/sys/dev/fxp:97917)
...

```

¯\\\_(ツ)\_/¯

## release/4.2.0 to release/4.0.0

These were done directly off of stable/4 but multiple files got their tag
slipped. Overall, it's a much cleaner representation of the history than the
later nonsense.
