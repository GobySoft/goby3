#!/bin/bash
# Checks 'goby interface' against the deployments in interfaces/: each case in cases.txt is drawn
# and compared with expected/<case>.<dot|d2>, by 'goby interface visualize' and, when it was built,
# by 'goby_clang_tool -viz', which shares the same code. Then validate, list, explain, diff,
# init, launch and systemd are checked for their exit codes and what they report.
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

# list
check 0 goby interface list modules.yml
expect_output "^auv/vehicle/nav_driver  *2  *2/2  *1/1  *-  *1/0$"
check 0 goby interface list dynamic_fleet.yml platforms
expect_output "^bot3  *1  *-$"
check 0 goby interface list fleet.yml groups -o JSON
expect_output '"group": "nav::fix"'
check 2 goby interface list no_such_deployment.yml

# explain: why a subscription is (not) met
check 0 goby interface explain modules.yml depth
expect_output "auv/vehicle/controller (Controller) \[interprocess\] nav::depth .*required: NOT MET"
expect_output "interprocess stops at its module (one gobyd); it needs intermodule"
check 0 goby interface explain dynamic_fleet.yml hub_command
expect_output "publishes fleet::hub_command;151-152, not fleet::hub_command;153"
expect_output "bot1/mission_manager (MissionManager) .*: met"
check 0 goby interface explain dynamic_unbound.yml 'hub_command|engineering'
expect_output "only a dynamic publisher whose numeric is set at run time reaches it"
expect_output "a dynamic: bind can say whether it is fleet::engineering"
check 0 goby interface explain fleet.yml nav::fix -o JSON
expect_output '"met": true'
check 1 goby interface explain fleet.yml no_such_group

# diff: exits 0 when the same and 1 when different, as diff(1)
check 0 goby interface diff fleet.yml run_fleet.yml
expect_output "no differences"
check 1 goby interface diff modules.yml modules_merged.yml
expect_output "+ connection   \[interprocess\] auv/vehicle/payload (Payload) to auv/vehicle/controller (Controller): nav::depth"
expect_output "\* met  *auv/vehicle/controller (Controller) \[interprocess\] nav::depth"
check 1 goby interface diff "$dir/deployments/elsewhere.yml" -I "$dir/interfaces" --interface_dir_b "$dir/deployments/v2"
expect_output "2 connections removed"
check 1 goby interface diff modules_merged.yml modules.yml -o JSON
expect_output '"newly_unmet"'
check 2 goby interface diff fleet.yml no_such_deployment.yml

# init: one platform running everything found
check 0 goby interface init "$dir/interfaces" --platform auv --output_file "$work/init.yml"
grep -q "^      - nav_driver$" "$work/init.yml" || { echo "FAILED: init did not list nav_driver"; fail=1; }
check 1 goby interface validate "$work/init.yml" -I "$dir/interfaces"
expect_output "12 applications"
check 1 goby interface init "$dir/deployments"

# launch: goby_launch files from the deployment's run: lists
for platform in hub bot2; do
    goby interface launch launch_fleet.yml --platform $platform --output_file "$work/$platform.launch" ||
        { echo "FAILED: goby interface launch --platform $platform"; fail=1; }
    if [ "$update" = update ]; then
        cp "$work/$platform.launch" "$dir/launch/$platform.launch"
    elif ! diff -u "$dir/launch/$platform.launch" "$work/$platform.launch"; then
        echo "FAILED: launch file for $platform differs from launch/$platform.launch"; fail=1
    fi
done
check 0 goby interface launch launch_fleet.yml --outdir "$work/outdir"
for unit in hub bot1 bot2; do
    [ -x "$work/outdir/$unit.launch" ] || { echo "FAILED: --outdir did not write $unit.launch"; fail=1; }
done
check 1 goby interface launch launch_fleet.yml
expect_output "choose one of bot1, bot2, hub"
check 1 goby interface launch launch_problems.yml
expect_output "launch-order.*mission_manager is launched before gobyd"
expect_output "hold-incomplete.*leaves out mission_manager"
expect_output "coroner-expected.*names nav_driver"
expect_output "launch-hash.*logger"
check 1 goby interface validate launch_problems.yml
expect_output "launch-hash"

# launch_check: a hand-written launch file against the deployment
check 1 goby interface launch_check launch_fleet.yml "$dir/launch/bot1_by_hand.launch" --platform bot1
expect_output "launched-only  *pHelmIvP"
expect_output "not-launched  *goby_coroner"
expect_output "kill  *MOOSDB  *goby_terminate in the launch file, SIGTERM in the deployment"
expect_output "env  *MOOSDB"
check 0 goby interface launch_check launch_fleet.yml "$work/outdir/bot1.launch" --platform bot1
expect_output "no differences"
check 1 goby interface launch_check launch_fleet.yml "$dir/launch/bot1_by_hand.launch" --platform bot1 -o JSON
expect_output '"kind": "launched-only"'

# goby launch <deployment>.yml writes the platform's launch file beside it and hands it to
# goby_launch (-h here, which prints its usage and exits 1, so nothing is started)
if ! command -v goby_launch > /dev/null; then
    export PATH=$PATH:$dir/../../../../../scripts
fi
cp launch_fleet.yml "$work/"
check 1 goby launch "$work/launch_fleet.yml" --platform bot --instance 2 -I "$dir/interfaces" -h
if [ "$update" != update ] && ! diff -q "$dir/launch/bot2.launch" <(sed "s#$work/##" "$work/.launch_fleet.bot.2.launch") > /dev/null; then
    echo "FAILED: goby launch did not write .launch_fleet.bot.2.launch as goby interface launch does"; fail=1
fi

# systemd: one unit per run: entry (and a target), compared with expected/systemd/<unit>/
for unit in bot1 topside; do
    check 0 goby interface systemd systemd_fleet.yml --platform $unit -D fleet_mode=autonomy --outdir "$work/systemd/$unit"
    if [ "$update" = update ]; then
        rm -rf "$dir/expected/systemd/$unit"; mkdir -p "$dir/expected/systemd"
        cp -r "$work/systemd/$unit" "$dir/expected/systemd/$unit"
    elif ! diff -ru "$dir/expected/systemd/$unit" "$work/systemd/$unit"; then
        echo "FAILED: systemd units for $unit differ from expected/systemd/$unit"; fail=1
    fi
done
# a when: decided false (or not at all) gets no unit, and a coroner expecting it is warned about
check 0 goby interface systemd systemd_fleet.yml --platform bot1 -D fleet_mode=manual
expect_output "systemd-dropped.*goby_coroner waits for mission_manager"
check 0 goby interface systemd systemd_fleet.yml --platform bot1
expect_output "systemd-when.*mission_manager"
! grep -q "^# --- mission_manager.service" "$work/out" ||
    { echo "FAILED: undecided when: still wrote mission_manager.service"; fail=1; }
check 0 goby interface systemd launch_fleet.yml --outdir "$work/systemd/all"
for unit in hub bot1 bot2; do
    [ -f "$work/systemd/all/$unit/launch_fleet-$unit.target" ] || { echo "FAILED: --outdir did not write $unit/launch_fleet-$unit.target"; fail=1; }
done
check 1 goby interface systemd launch_fleet.yml
expect_output "choose one of bot1, bot2, hub"
# --check: the installed units against the rendered ones
check 0 goby interface systemd systemd_fleet.yml --platform bot1 -D fleet_mode=autonomy --check --unit_dir "$work/systemd/bot1"
expect_output "no differences"
echo "LimitNOFILE=4096" >> "$work/systemd/bot1/nav_driver.service"
rm "$work/systemd/bot1/logger.service"
check 1 goby interface systemd systemd_fleet.yml --platform bot1 -D fleet_mode=autonomy --check --unit_dir "$work/systemd/bot1"
expect_output "differs  *.*nav_driver.service"
expect_output "missing  *.*logger.service"
check 1 goby interface systemd systemd_problems.yml --platform bot
expect_output "systemd-template.*no_such.service.in"
expect_output "deployment-schema.*unknown key 'restart'"

check 1 goby interface visualize missing_interface.yml
expect_output "interface-missing"
check 1 goby interface visualize no_such_deployment.yml
expect_output "deployment-missing"
check 2 goby interface visualize fleet.yml --output_file "$work/no_such_dir/fleet.dot"
expect_output "cannot write .*no_such_dir/fleet.dot"
check 1 goby interface no_such_action

[ $fail -eq 0 ] && echo "PASSED"
exit $fail
