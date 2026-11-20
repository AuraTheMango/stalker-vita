#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_dir="$(mktemp -d -t stalker-vita-tests.XXXXXX)"
trap 'rm -rf -- "$task_dir"' EXIT
src=source/trunk/vita
cxx_args=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
python3 "$src/tests/prepare_inventory_fixture.py" "$task_dir"
python3 "$src/tests/prepare_menu_fixture.py" "$task_dir"
python3 "$src/tests/prepare_campaign_interaction.py" "$task_dir"
python3 "$src/tests/prepare_campaign_cache_fixture.py" "$task_dir"
python3 "$src/tests/prepare_zone_confirmation_fixture.py" "$task_dir"
python3 "$src/tests/prepare_save_browser_fixture.py" "$task_dir"
for test in settings_hud gameplay inventory_ui dialogue_ui menus campaign_interaction campaign_ui_cache pause_cache zone_confirmation zone_confirmation_ui save_browser; do
    g++ "${cxx_args[@]}" -I "$src/xrGame" -I "$task_dir" "$src/tests/$test.cpp" -o "$task_dir/$test"
    mkdir -p "$task_dir/data-$test"
    "$task_dir/$test" "$task_dir/data-$test"
done
echo "Asset-free host checks passed."
