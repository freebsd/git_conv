#!/bin/sh

# Some commits message have broken versions of the commit message template left
# in them. Delete them.

# Examples in base
# r289711 (just has the last line)
# r290234 (76 columns)
# r315956 (and those below, will be ignored--)
# r267352 (oh boy ...)

sed \
-e '/those below, will be ignored--$/,$d' \
-e '/Description of fields to fill in above: *7. columns --.$/,$d' \
-e '/^> PR:            If a GNATS PR is affected by the change./,$d' \
-e '/^> Submitted by:  If someone else sent in the change./,$d' \
-e '/^_M   /,$d'

