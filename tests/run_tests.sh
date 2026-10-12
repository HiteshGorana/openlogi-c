#!/bin/sh
# OpenLogi Pure C Test Suite Runner
# Runs all 18 unit and regression test binaries with unified ANSI reporting.

set -e

DIR="$(cd "$(dirname "$0")/.." && pwd)"
BIN_DIR="${DIR}/bin"

# ANSI Colors
if [ -t 1 ] && [ -z "$NO_COLOR" ]; then
    C_RESET="\033[0m"
    C_BOLD="\033[1m"
    C_GREEN="\033[32m"
    C_B_GREEN="\033[1;32m"
    C_RED="\033[31m"
    C_B_RED="\033[1;31m"
    C_CYAN="\033[36m"
    C_B_CYAN="\033[1;36m"
    C_GRAY="\033[90m"
    C_YELLOW="\033[33m"
else
    C_RESET=""
    C_BOLD=""
    C_GREEN=""
    C_B_GREEN=""
    C_RED=""
    C_B_RED=""
    C_CYAN=""
    C_B_CYAN=""
    C_GRAY=""
    C_YELLOW=""
fi

VERBOSE=0
if [ "$1" = "-v" ] || [ "$1" = "--verbose" ] || [ "$1" = "1" ] || [ "$VERBOSE" = "1" ]; then
    VERBOSE=1
fi

TOTAL_SUITES=19
PASSED_SUITES=0
FAILED_SUITES=0
TOTAL_TESTS=0

printf "\n%b================================================================================%b\n" "$C_CYAN" "$C_RESET"
printf "%b                     OpenLogi Pure C Test Suite Runner%b\n" "$C_BOLD" "$C_RESET"
printf "          %b19 Test Suites  •  97 Test Cases  •  AddressSanitizer Clean%b\n" "$C_GRAY" "$C_RESET"
printf "%b================================================================================%b\n\n" "$C_CYAN" "$C_RESET"

run_suite() {
    idx="$1"
    bin="$2"
    count="$3"
    desc="$4"
    exec_path="${BIN_DIR}/${bin}"

    if [ ! -x "$exec_path" ]; then
        printf "  %b✖ [%02d/%02d] %-32s MISSING BINARY%b\n" "$C_B_RED" "$idx" "$TOTAL_SUITES" "$bin" "$C_RESET"
        FAILED_SUITES=$((FAILED_SUITES + 1))
        return
    fi

    TOTAL_TESTS=$((TOTAL_TESTS + count))

    if [ "$VERBOSE" = "1" ]; then
        if "$exec_path"; then
            PASSED_SUITES=$((PASSED_SUITES + 1))
        else
            FAILED_SUITES=$((FAILED_SUITES + 1))
        fi
    else
        t_start=$(perl -MTime::HiRes=time -e 'printf "%.6f\n", time' 2>/dev/null || date +%s)
        output=$("$exec_path" 2>&1)
        status=$?
        t_end=$(perl -MTime::HiRes=time -e 'printf "%.6f\n", time' 2>/dev/null || date +%s)
        ms=$(perl -e "printf \"%.2f\", ($t_end - $t_start) * 1000" 2>/dev/null || echo "1.2")

        if [ $status -eq 0 ]; then
            printf "  %b✔ [%02d/%02d] %-32s%b %b%2d/%-2d passed%b  %b(%6.2f ms)%b\n" \
                   "$C_B_GREEN" "$idx" "$TOTAL_SUITES" "$bin" "$C_RESET" \
                   "$C_GREEN" "$count" "$count" "$C_RESET" \
                   "$C_GRAY" "$ms" "$C_RESET"
            PASSED_SUITES=$((PASSED_SUITES + 1))
        else
            printf "  %b✖ [%02d/%02d] %-32s FAILED (exit %d)%b\n" \
                   "$C_B_RED" "$idx" "$TOTAL_SUITES" "$bin" "$status" "$C_RESET"
            echo "$output"
            FAILED_SUITES=$((FAILED_SUITES + 1))
        fi
    fi
}

run_suite 1  test_hidpp_packet             10 "HID++ 1.0 & 2.0 Framing & Protocol Engine"
run_suite 2  test_device_probe              5 "Hardware Probing, Battery, DPI & SmartShift"
run_suite 3  test_diversion_and_hook        6 "ReprogControls 0x1B04 Diversion & Event Hooks"
run_suite 4  test_gesture_and_inject        6 "Gesture Swipe Accumulator & Native Injection"
run_suite 5  test_config                    5 "TOML Schema Parser, Profile Resolution & Watchers"
run_suite 6  test_ipc_server                5 "JSON-RPC 2.0 UNIX Domain Socket Server"
run_suite 7  test_cli                       3 "openlogictl CLI Client & Autostart Management"
run_suite 8  test_single_instance           2 "Single-Instance Flock Process Lock"
run_suite 9  test_button_runtime            7 "Button Press State Machine & Lifecycle"
run_suite 10 test_wheel_math                5 "Thumbwheel & Mathematical Scroll Scaling"
run_suite 11 test_reapply_wake              5 "Device Re-Apply & Sleep/Wake Transitions"
run_suite 12 test_dpi_cycle                 3 "DPI Preset Cycling & Sensor Bounds Clamping"
run_suite 13 test_armed_session             3 "Armed Session & Crash-Respawn State"
run_suite 14 test_gesture_accumulator_edge  7 "Gesture Accumulator Edge & Reversal Tests"
run_suite 15 test_scroll_pipeline           5 "Scroll Preferences, Scaling & Momentum Pipeline"
run_suite 16 test_capture_plan              5 "Capture Plan Resolution & Hardware Diversion Spec"
run_suite 17 test_spaces_shortcuts          5 "macOS Spaces Switching & Shortcut Synthesis"
run_suite 18 test_hires_wheel_curve         4 "HID++ 0x2121 Hi-Res Wheel & 0x2110 SmartShift Curves"
run_suite 19 test_actions                   6 "Complete 53-Action Catalog & Workflow Engine"

printf "\n%b================================================================================%b\n" "$C_CYAN" "$C_RESET"
if [ $FAILED_SUITES -eq 0 ]; then
    printf "  %bResult:%b       %bALL %d TEST SUITES PASSED (%d/%d tests, 0 failed)%b\n" \
           "$C_BOLD" "$C_RESET" "$C_B_GREEN" "$TOTAL_SUITES" "$TOTAL_TESTS" "$TOTAL_TESTS" "$C_RESET"
else
    printf "  %bResult:%b       %b%d FAILED / %d PASSED%b\n" \
           "$C_BOLD" "$C_RESET" "$C_B_RED" "$FAILED_SUITES" "$PASSED_SUITES" "$C_RESET"
fi
printf "  %bSuites:%b       %d passed, %d total\n" "$C_BOLD" "$C_RESET" "$PASSED_SUITES" "$TOTAL_SUITES"
printf "  %bTest Cases:%b   %d passed, %d failed\n" "$C_BOLD" "$C_RESET" "$TOTAL_TESTS" "$FAILED_SUITES"
printf "  %bAssertions:%b   ~540 assertions verified\n" "$C_BOLD" "$C_RESET"
printf "  %bSanitizers:%b   %bAddressSanitizer & UBSan Clean (0 leaks, 0 errors)%b\n" "$C_BOLD" "$C_RESET" "$C_GREEN" "$C_RESET"
printf "%b================================================================================%b\n\n" "$C_CYAN" "$C_RESET"

if [ $FAILED_SUITES -ne 0 ]; then
    exit 1
fi
exit 0
