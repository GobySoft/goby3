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

[ $fail -eq 0 ] && echo "PASSED" || { echo "--- entries:"; cat "$work/entries"; }
exit $fail
