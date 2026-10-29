#!/bin/bash
# Integration test for 'goby udpm test' (no daemon needed: the endpoint joins a multicast group).
#
# Usage: run.sh <goby bin dir> <this directory>
set -u
export PATH=$1:$PATH
dir=$2
fail=0
goby udpm test "$dir/pass.pb.cfg" || { echo "FAILED: pass.pb.cfg exited $?"; fail=1; }
goby udpm test "$dir/fail.pb.cfg"; [ $? -eq 1 ] || { echo "FAILED: fail.pb.cfg did not exit 1"; fail=1; }
exit $fail
