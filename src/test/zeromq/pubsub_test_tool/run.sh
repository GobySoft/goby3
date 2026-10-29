#!/bin/bash
# Integration test for 'goby zeromq test': launches gobyd instances and checks the tool's
# behavior and exit codes against the YAML files in this directory.
#
# Usage: run.sh <goby bin dir> <this directory>
set -u
bin=$1
dir=$2
export PATH=$bin:$PATH

pids=()
for platform in pstest_a pstest_b pstest_mod; do
    gobyd --interprocess "platform: \"$platform\"" > /dev/null 2>&1 &
    pids+=($!)
done
shutdown() {
    kill -INT "${pids[@]}" 2>/dev/null
    for _ in 1 2 3 4 5; do
        kill -0 "${pids[@]}" 2>/dev/null || break
        sleep 0.5
    done
    kill -TERM "${pids[@]}" 2>/dev/null
    wait
}
trap shutdown EXIT
sleep 1

fail=0
check() {
    local expected=$1; shift
    "$@"
    local rc=$?
    if [ $rc -ne $expected ]; then
        echo "FAILED: '$*' exited $rc, expected $expected"
        fail=1
    fi
}

check 0 goby zeromq test "$dir/pass.yml"
check 1 goby zeromq test "$dir/fail.yml"
check 2 goby zeromq test "$dir/missing_app.yml"
check 1 goby zeromq test "$dir/bad_field.yml"

json=$(goby zeromq test "$dir/pass.yml" --output_format JSON)
echo "$json" | grep -q '"result": "PASS"' || { echo "FAILED: JSON report missing PASS"; fail=1; }

exit $fail
