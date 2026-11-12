#!/bin/bash
# Checks 'goby interface' against the deployments in interfaces/: each case in cases.txt is drawn
# and compared with expected/<case>.<dot|d2>, by 'goby interface visualize' and, when it was built,
# by 'goby_clang_tool -viz', which shares the same code. Then 'goby interface validate' is checked
# for its exit codes and the problems it reports.
#
# Usage: run.sh <goby bin dir> <this directory> [update]
#   update: rewrite expected/ from the current output instead of comparing
set -u
bin=$1
dir=$2
update=${3:-}
export PATH=$bin:$PATH

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$dir/interfaces"

fail=0
check() {
    local expected=$1; shift
    "$@" > "$work/out" 2>&1
    local rc=$?
    if [ $rc -ne $expected ]; then
        echo "FAILED: '$*' exited $rc, expected $expected"; cat "$work/out"
        fail=1
    fi
}
expect_output() {
    local pattern=$1
    grep -q -- "$pattern" "$work/out" || { echo "FAILED: output lacks '$pattern'"; cat "$work/out"; fail=1; }
}
compare() {
    local expected=$1 actual=$2 what=$3
    if [ "$update" = update ]; then
        cp "$actual" "$dir/expected/$expected"
    elif ! diff -u "$dir/expected/$expected" "$actual" > "$work/diff"; then
        echo "FAILED: $what output differs from expected/$expected"
        head -40 "$work/diff"
        fail=1
    fi
}

while IFS='|' read -r name deployment goby_flags clang_flags format; do
    name=$(echo $name); deployment=$(echo $deployment); format=$(echo ${format:-dot})
    [ -z "$name" ] || [ "${name:0:1}" = "#" ] && continue
    expected=$name.$format
    format_flag=$([ "$format" = d2 ] && echo "--format D2")

    goby interface visualize "$deployment" $goby_flags $format_flag --output_file "$work/$expected" ||
        { echo "FAILED: goby interface visualize $deployment $goby_flags"; fail=1; continue; }
    compare "$expected" "$work/$expected" "goby interface visualize"
    if [ "$format" = d2 ]; then
        awk -f "$dir/check_d2.awk" "$work/$expected" || fail=1
        if command -v d2 > /dev/null; then
            d2 "$work/$expected" "$work/$name.svg" > "$work/d2.log" 2>&1 ||
                { echo "FAILED: d2 could not render $expected"; cat "$work/d2.log"; fail=1; }
        fi
    fi

    if [ -x "$bin/goby_clang_tool" ] && [ "$update" != update ]; then
        goby_clang_tool -viz $clang_flags -format=$format -outdir "$work" -o "$name.clang.$format" "$deployment" ||
            { echo "FAILED: goby_clang_tool -viz $deployment $clang_flags"; fail=1; continue; }
        compare "$expected" "$work/$name.clang.$format" "goby_clang_tool -viz"
    fi
done < "$dir/cases.txt"

# validate: exit 0 when valid, 1 on errors (or warnings with --strict), 2 when unreadable
check 1 goby interface validate fleet.yml
expect_output "required-unmet  *auv/controller \[interprocess\] no publisher for nav::depth"
expect_output "2/3 required met"
check 1 goby interface validate modules.yml
expect_output "auv/vehicle/controller \[interprocess\] no publisher for nav::depth"
check 0 goby interface validate goby_gps_interface.yml
check 0 goby interface validate warning_only.yml
expect_output "recommended-unmet"
check 1 goby interface validate warning_only.yml --strict
check 1 goby interface validate run_problems.yml
expect_output "application-duplicate"
expect_output "unknown key 'colour'"
expect_output "no interface file declares application no_such_application"
check 1 goby interface validate run_features.yml
expect_output "recommended-unmet  *topside/topside_display"
expect_output "5 applications"
check 1 goby interface validate run_features.yml -D CONTROLLER=nope
expect_output "no interface file declares application nope"
check 1 goby interface validate fleet.yml -o JSON
expect_output '"code": "required-unmet"'
expect_output '"necessity": "required"'
expect_output '"required_met": 2'
check 2 goby interface validate no_such_deployment.yml

# interface files are found beside the deployment, then with -I or GOBY_INTERFACE_PATH
pushd "$work" > /dev/null
check 1 goby interface validate "$dir/deployments/elsewhere.yml"
expect_output "no interface file declares application nav_driver"
check 0 goby interface validate "$dir/deployments/elsewhere.yml" -I "$dir/interfaces"
GOBY_INTERFACE_PATH="$dir/interfaces" check 0 goby interface validate "$dir/deployments/elsewhere.yml"
popd > /dev/null

# dynamic groups: binds, platform instances and necessity overrides
check 1 goby interface validate dynamic_fleet.yml
expect_output "required-unmet  *bot3/mission_manager \[intervehicle\] no publisher for fleet::hub_command;153"
expect_output "(necessity set by the deployment)"
expect_output "3/4 required met"
expect_output "bind-unused  *platforms\[0\].bind\[2\]"
expect_output "necessity-unused  *necessity\[1\]"
check 1 goby interface validate dynamic_unbound.yml
expect_output "only a dynamic publisher with a numeric set at run time reaches fleet::hub_command;?"
expect_output "dynamic-unbound  *hub/hub_manager"
expect_output "dynamic-unresolved  *hub/hub_manager \[interprocess\]"
check 1 goby interface validate dynamic_unbound.yml --assume_dynamic
expect_output "1/2 required met"

check 1 goby interface visualize missing_interface.yml
expect_output "interface-missing"
check 1 goby interface visualize no_such_deployment.yml
expect_output "deployment-missing"
check 2 goby interface visualize fleet.yml --output_file "$work/no_such_dir/fleet.dot"
expect_output "cannot write .*no_such_dir/fleet.dot"
check 1 goby interface no_such_action

[ $fail -eq 0 ] && echo "PASSED"
exit $fail
