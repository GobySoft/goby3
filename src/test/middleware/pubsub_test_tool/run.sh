#!/bin/bash
# Integration test for 'goby test': checks reports and exit codes for run, validate and visualize
# against the files in this directory. udpm cases always run; zeromq and mixed cases need gobyd
# ("zeromq" argument, the daemons are fixtures), and the YAML case needs yaml-cpp ("yaml").
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

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

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
expect_output() {
    local pattern=$1 file=$2
    grep -q -- "$pattern" "$file" || { echo "FAILED: output lacks '$pattern'"; cat "$file"; fail=1; }
}

# unknown actions fail rather than printing help and succeeding
check 1 goby test no_such_action
check 1 goby no_such_action

# run: pass, fail, setup errors, variables
check 0 goby test run "$dir/udpm_pass.pb.cfg"
check 0 goby test run "$dir/udpm_pass.pb.cfg" -D PSTEST_UDPM_PORT=11196
check 1 goby test run "$dir/udpm_fail.pb.cfg"
check 2 goby test run "$dir/udpm_missing_app.pb.cfg"
check 2 goby test run "$dir/bad_field.pb.cfg"
check 2 goby test run "$dir/invalid.pb.cfg"
goby test run "$dir/udpm_pass.pb.cfg" --output_format JSON > "$work/report.json"
expect_output '"result": "PASS"' "$work/report.json"

# validate: every problem with its location, nothing run
check 0 goby test validate "$dir/udpm_pass.pb.cfg"
goby test validate "$dir/invalid.pb.cfg" > "$work/invalid.txt"
check 1 goby test validate "$dir/invalid.pb.cfg"
for loc in "endpoint\[1\].name" "fixture\[0\].command" "test.run\[0\].publish\[0\].endpoint" \
           "test.run\[0\].publish\[0\].value" "test.run\[0\].expect\[0\].condition\[0\].field"; do
    expect_output "$loc" "$work/invalid.txt"
done

# visualize
goby test visualize "$dir/udpm_pass.pb.cfg" > "$work/plan.dot"
check 0 goby test visualize "$dir/udpm_pass.pb.cfg" --output_file "$work/plan2.dot"
expect_output "digraph goby_test" "$work/plan.dot"
expect_output "self receive" "$work/plan.dot"
if command -v dot > /dev/null; then
    check 0 dot -Tsvg "$work/plan.dot" -o "$work/plan.svg"
fi

# fixtures: never ready, daemonized descendants cleaned up, interruption
check 2 goby test run "$dir/fixture_not_ready.pb.cfg"
check 0 goby test run "$dir/fixture_cleanup.pb.cfg" -D PIDFILE="$work/daemon.pid"
if kill -0 "$(cat "$work/daemon.pid")" 2> /dev/null; then
    echo "FAILED: daemonized fixture descendant still running"
    kill "$(cat "$work/daemon.pid")"
    fail=1
fi
goby test run "$dir/long_wait.pb.cfg" > "$work/interrupted.txt" &
pid=$!
sleep 2
kill -INT $pid
wait $pid
rc=$?
[ $rc -eq 2 ] || { echo "FAILED: interrupted run exited $rc, expected 2"; fail=1; }
expect_output "interrupted" "$work/interrupted.txt"

if $zeromq; then
    check 0 goby test run "$dir/zeromq_pass.pb.cfg"
    check 0 goby test run "$dir/mixed_pass.pb.cfg"
    check 2 goby test run "$dir/zeromq_missing_app.pb.cfg"
    if $yaml; then
        check 0 goby test run "$dir/zeromq_pass.yml"
    else
        # without yaml-cpp a YAML file is a configuration error
        check 2 goby test run "$dir/zeromq_pass.yml"
    fi
else
    # a zeromq endpoint in a build without zeromq is a configuration error
    check 2 goby test run "$dir/zeromq_pass.pb.cfg"
fi

exit $fail
