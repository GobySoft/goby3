#!/bin/bash
# Runs goby_clang_tool -gen on dynamic_groups.cpp and checks what it records for each way the
# fixture makes a DynamicGroup, and the numerics of static groups defined with constant expressions.
#
# Usage: run.sh <goby bin dir> <this directory> <build dir with compile_commands.json>
set -u
bin=$1
dir=$2
build=$3

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$bin/goby_clang_tool" -gen -target dynamic_groups -outdir "$work" -p "$build" \
    "$dir/dynamic_groups.cpp" > "$work/gen.log" 2>&1 || { cat "$work/gen.log"; exit 1; }

# one line per entry, without its thread: "group=...;scheme=...;type=...;..."
awk '
    /^ *- group: / { if (entry != "") print entry; sub(/^ *- /, ""); entry = $0; next }
    entry != "" && /^ +[a-z_]+: / && !/^ +- / { line = $0; sub(/^ +/, "", line);
                                                if (line !~ /^thread: /) entry = entry ";" line; next }
    { if (entry != "") print entry; entry = "" }
    END { if (entry != "") print entry }
' "$work/dynamic_groups_interface.yml" | sed 's/: /=/g' > "$work/entries"

status=goby::test::middleware::protobuf::DynamicGroupsStatus
fail=0
expect() {
    grep -qxF -- "$1" "$work/entries" || { echo "FAILED: no entry '$1'"; fail=1; }
}
expect "group=dynamic_test::literal;scheme=PROTOBUF;type=$status;dynamic=true"
expect "group=dynamic_test::status;3;scheme=PROTOBUF;type=$status;dynamic=true"
expect "group=dynamic_test::status;scheme=PROTOBUF;type=$status;dynamic=true;numeric=runtime"
expect "group=dynamic_test::command;scheme=PROTOBUF;type=$status;dynamic=true;numeric=runtime"
expect "group=\"*\";scheme=PROTOBUF;type=$status;dynamic=true;expr=this->runtime_group_"
expect "group=dynamic_test::command;scheme=PROTOBUF;type=$status;necessity=required;dynamic=true;numeric=runtime"
expect "group=dynamic_test::local;scheme=PROTOBUF;type=$status;necessity=optional;dynamic=true"

# constant numerics are recorded as their values
expect "group=groups::broadcast;0;scheme=PROTOBUF;type=$status"
expect "group=groups::fleet;105;scheme=PROTOBUF;type=$status"
# and a group without one is recorded without a suffix
expect "group=dynamic_test::plain;scheme=PROTOBUF;type=$status"

# Goby's own forwarding calls pass a const Group&, not a DynamicGroup, and are not recorded
# (publications are repeated on the interthread layer as inner=true)
if [ "$(grep 'dynamic=true' "$work/entries" | grep -vc 'inner=true')" -ne 7 ]; then
    echo "FAILED: expected exactly 7 dynamic entries"; fail=1
fi

# A Julia or Python application's interthread never reaches its C++, so its generator writes a
# manifest, which -gen adds to the interface file after the layers it read from the source
for language in julia python; do
    mkdir -p "$work/$language"
    "$bin/goby_clang_tool" -gen -target dynamic_groups -outdir "$work/$language" -p "$build" \
        -interthread-manifest "$dir/${language}_interthread.yml" \
        "$dir/dynamic_groups.cpp" > "$work/gen.log" 2>&1 || { cat "$work/gen.log"; fail=1; continue; }
    out="$work/$language/dynamic_groups_interface.yml"
    grep -qx "${language}_interthread:" "$out" || { echo "FAILED: no ${language}_interthread section"; fail=1; }
    # the layers read from the source are still there
    grep -qx "interprocess:" "$out" || { echo "FAILED: ${language} manifest displaced the interprocess layer"; fail=1; }
done
grep -q "capacity: 10, overflow: drop_oldest" "$work/julia/dynamic_groups_interface.yml" ||
    { echo "FAILED: the Julia subscription lost its queue settings"; fail=1; }

# outer_threads draws a glue call on the thread that makes it, matching the group by its runtime
# string or, as here, by the C++ expression interface.yml names it with; without the manifest the
# call stays on the class that made it
grep -A3 -m1 "^    - group: dynamic_test::plain$" "$work/julia/dynamic_groups_interface.yml" |
    grep -q "thread: Reporter" || { echo "FAILED: groups::plain was not drawn on Reporter"; fail=1; }
grep -A3 -m1 "^    - group: dynamic_test::plain$" "$work/python/dynamic_groups_interface.yml" |
    grep -q "thread: Reporter" && { echo "FAILED: groups::plain moved without outer_threads"; fail=1; }

# a manifest the interface tools could not read is refused rather than copied in
mkdir -p "$work/bad"
if "$bin/goby_clang_tool" -gen -target dynamic_groups -outdir "$work/bad" -p "$build" \
       -interthread-manifest "$dir/bad_interthread.yml" "$dir/dynamic_groups.cpp" > "$work/bad.log" 2>&1; then
    echo "FAILED: a Python manifest with capacity was accepted"; fail=1
elif ! grep -q "unknown key 'python_interthread.subscribes.capacity'" "$work/bad.log"; then
    echo "FAILED: the bad manifest was refused for the wrong reason"; cat "$work/bad.log"; fail=1
fi

if "$bin/goby_clang_tool" -gen -target dynamic_groups -outdir "$work/bad" -p "$build" \
       -interthread-manifest "$dir/bad_outer_threads.yml" "$dir/dynamic_groups.cpp" > "$work/bad.log" 2>&1; then
    echo "FAILED: outer_threads on interthread was accepted"; fail=1
elif ! grep -q "it is interprocess or intermodule" "$work/bad.log"; then
    echo "FAILED: the bad outer_threads was refused for the wrong reason"; cat "$work/bad.log"; fail=1
fi

[ $fail -eq 0 ] && echo "PASSED" || { echo "--- entries:"; cat "$work/entries"; }
exit $fail
