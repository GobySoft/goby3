#!/bin/bash
# Checks 'goby interface' against the deployments in interfaces/: each case in cases.txt is drawn
# and compared with expected/<case>.dot, by 'goby interface visualize' and, when it was built,
# by 'goby_clang_tool -viz', which shares the same code.
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

check 1 goby interface visualize missing_interface.yml
expect_output "interface-missing"
check 1 goby interface visualize no_such_deployment.yml
expect_output "deployment-missing"
check 1 goby interface no_such_action

[ $fail -eq 0 ] && echo "PASSED"
exit $fail
