#!/bin/bash
# Integration test for 'goby udpm test' (no daemon needed: the endpoint joins a multicast group).
#
# Usage: run.sh <goby bin dir> <this directory>
set -u
export PATH=$1:$PATH
dir=$2
fail=0
goby udpm test "$dir/pass.yml" || { echo "FAILED: pass.yml exited $?"; fail=1; }
goby udpm test "$dir/fail.yml"; [ $? -eq 1 ] || { echo "FAILED: fail.yml did not exit 1"; fail=1; }
exit $fail
