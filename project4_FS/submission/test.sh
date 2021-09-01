#!/bin/bash

echo 'addronce'
./fcheck ../test_images/addronce
echo 'addronce2'
./fcheck ../test_images/addronce2
echo 'badaddr'
./fcheck ../test_images/badaddr
echo 'badfmt'
./fcheck ../test_images/badfmt
echo 'badindir1'
./fcheck ../test_images/badindir1
echo 'badindir2'
./fcheck ../test_images/badindir2
./fcheck ../test_images/badinode
echo 'badlarge'
./fcheck ../test_images/badlarge
echo 'badrefcnt'
./fcheck ../test_images/badrefcnt
echo 'badrefcn2'
./fcheck ../test_images/badrefcnt2
echo 'badroot'
./fcheck ../test_images/badroot
echo 'badroot2'
./fcheck ../test_images/badroot2
echo 'dironce'
./fcheck ../test_images/dironce
echo 'imrkfree'
./fcheck ../test_images/imrkfree
echo 'imrkused'
./fcheck ../test_images/imrkused
echo 'indirfree'
./fcheck ../test_images/indirfree
echo 'mrkfree'
./fcheck ../test_images/mrkfree
echo 'mrkused'
./fcheck ../test_images/mrkused
echo 'good'
./fcheck ../test_images/good
echo 'goodlarge'
./fcheck ../test_images/goodlarge
echo 'goodlink'
./fcheck ../test_images/goodlink
echo 'goodrefcnt'
./fcheck ../test_images/goodrefcnt
echo 'goodrm'
./fcheck ../test_images/goodrm