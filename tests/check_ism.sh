#!/bin/sh

# --show-reachable=yes
exec="libtool --mode=execute valgrind --log-file=check-ism-valgrind.log --leak-check=full --error-exitcode=1"
$exec ./check-ism test.mp4 test.ismv
exit $?
