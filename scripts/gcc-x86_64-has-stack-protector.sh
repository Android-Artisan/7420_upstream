#!/bin/sh

echo "int foo(void) { char X[200]; return 3; }" | $1 -S -xc -c -O0 -mcmodel=kernel -fstack-protector - -o - 2> /dev/null | grep -q "%gs"
if [ "$?" -eq "0" ] ; then
	echo $2
echo "int foo(void) { char X[200]; return 3; }" | $* -S -x c -c -O0 -mcmodel=kernel -fstack-protector - -o - 2> /dev/null | grep -q "%gs"
echo "int foo(void) { char X[200]; return 3; }" | $* -S -x c -c -O0 -mcmodel=kernel -fno-PIE -fstack-protector - -o - 2> /dev/null | grep -q "%gs"
if [ "$?" -eq "0" ] ; then
	echo y
else
	echo n
fi
