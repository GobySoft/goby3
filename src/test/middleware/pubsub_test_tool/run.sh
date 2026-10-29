#!/bin/bash
# Integration test for 'goby test': checks reports and exit codes against the files in this
# directory. udpm cases always run; zeromq and mixed cases need gobyd ("zeromq" argument), and
# the YAML case needs yaml-cpp support ("yaml" argument).
#
# Usage: run.sh <goby bin dir> <this directory> [zeromq] [yaml]
set -u
bin=$1
dir=$2
shift 2
export PATH=$bin:$PATH
zeromq=false; yaml=false
for arg in "$@"; do
    case $arg in
        zeromq) zeromq=true ;;
        yaml) yaml=true ;;
    esac
done

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

check 0 goby test "$dir/udpm_pass.pb.cfg"
check 1 goby test "$dir/udpm_fail.pb.cfg"
check 1 goby test "$dir/bad_field.pb.cfg"
check 2 goby test "$dir/udpm_missing_app.pb.cfg"

json=$(goby test "$dir/udpm_pass.pb.cfg" --output_format JSON)
echo "$json" | grep -q '"result": "PASS"' || { echo "FAILED: JSON report missing PASS"; fail=1; }

if $zeromq; then
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

    check 0 goby test "$dir/zeromq_pass.pb.cfg"
    check 0 goby test "$dir/mixed_pass.pb.cfg"
    check 2 goby test "$dir/zeromq_missing_app.pb.cfg"
    if $yaml; then
        check 0 goby test "$dir/zeromq_pass.yml"
    else
        # without yaml-cpp a YAML file is a configuration error
        check 1 goby test "$dir/zeromq_pass.yml"
    fi
else
    # a zeromq endpoint in a build without zeromq is a configuration error
    check 1 goby test "$dir/zeromq_pass.pb.cfg"
fi

exit $fail
