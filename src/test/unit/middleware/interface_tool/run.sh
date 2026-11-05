#!/bin/bash
# Checks 'goby interface' against the deployments in interfaces/: each case in cases.txt is drawn
# and compared with expected/<case>.dot, by 'goby interface visualize' and, when it was built,
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
    local name=$1 actual=$2 what=$3
    if [ "$update" = update ]; then
        cp "$actual" "$dir/expected/$name.dot"
    elif ! diff -u "$dir/expected/$name.dot" "$actual" > "$work/diff"; then
        echo "FAILED: $what output for '$name' differs from expected/$name.dot"
        head -40 "$work/diff"
        fail=1
    fi
}

while IFS='|' read -r name deployment goby_flags clang_flags; do
    name=$(echo $name); deployment=$(echo $deployment)
    [ -z "$name" ] || [ "${name:0:1}" = "#" ] && continue

    goby interface visualize "$deployment" $goby_flags --output_file "$work/$name.dot" ||
        { echo "FAILED: goby interface visualize $deployment $goby_flags"; fail=1; continue; }
    compare "$name" "$work/$name.dot" "goby interface visualize"

    if [ -x "$bin/goby_clang_tool" ] && [ "$update" != update ]; then
        goby_clang_tool -viz $clang_flags -outdir "$work" -o "$name.clang.dot" "$deployment" ||
            { echo "FAILED: goby_clang_tool -viz $deployment $clang_flags"; fail=1; continue; }
        compare "$name" "$work/$name.clang.dot" "goby_clang_tool -viz"
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

check 1 goby interface visualize missing_interface.yml
expect_output "interface-missing"
check 1 goby interface visualize no_such_deployment.yml
expect_output "deployment-missing"
check 1 goby interface no_such_action

[ $fail -eq 0 ] && echo "PASSED"
exit $fail
