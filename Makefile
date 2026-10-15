# ==============================================================================
# OpenLogi Pure C (C99) Build System
# Native, zero-telemetry Logitech HID++ 2.0 daemon and CLI control tool
# ==============================================================================
#
# Runtime Binaries (bin/):
#   openlogid           - Background daemon (HID++ engine, event tap, JSON-RPC server)
#   openlogictl         - Command-line tool and interactive terminal setup wizard
#   openlogi_discover   - Live hardware discovery and register inspection tool
#
# Test Suites (bin/test_*):
#   18 test suites, 91 unit and regression tests (~480 assertions)
#   100% AddressSanitizer and UndefinedBehaviorSanitizer clean
#
# Primary Targets:
#   make [all]          - Build all libraries, CLI tools, and 18 test suites
#   make test           - Run full test suite runner with visual progress dashboard
#   make test VERBOSE=1 - Run tests with per-test timing details
#   make asan           - Rebuild and test with AddressSanitizer & UBSan (0 leaks)
#   make tools          - Build only the runtime binaries (openlogid, openlogictl, etc.)
#   make strip          - Strip binaries and display lightweight sizes (~360 KB total)
#   make install        - Install binaries to $(DESTDIR)$(BINDIR) (default: /usr/local/bin)
#   make uninstall      - Remove binaries from $(DESTDIR)$(BINDIR)
#   make clean          - Remove build/ and bin/ directories
#   make help / info    - Display target reference and build environment details
# ==============================================================================

CC ?= clang
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic -O2 -g -Iinclude -Ivendor/yyjson -Ivendor/tomlc99

ifneq ($(OPENLOGI_VERSION),)
    CPPFLAGS += -DOPENLOGI_AGENT_VERSION=\"$(OPENLOGI_VERSION)-c\"
endif

# Auto-select prefix: use /usr/local if writable (e.g. under sudo), else ~/.local
ifeq ($(shell test -w /usr/local/bin 2>/dev/null && echo 1 || echo 0),1)
    DEFAULT_PREFIX ?= /usr/local
else
    DEFAULT_PREFIX ?= $(HOME)/.local
endif

PREFIX ?= $(DEFAULT_PREFIX)
BINDIR ?= $(PREFIX)/bin

BUILD_DIR ?= build
BIN_DIR ?= bin
CODE_SIGN_IDENTITY ?= -

UNAME_S := $(shell uname -s)

SRC_HID = src/hid/hidpp_packet.c src/hid/hidpp_features.c src/hid/hidpp_diversion.c src/hid/hid_mock.c
SRC_CORE = src/core/signals.c src/core/gesture.c src/core/config.c src/core/config_watch.c \
           src/core/single_instance.c src/core/button_runtime.c src/core/scroll_math.c \
           src/core/device_reapply.c src/core/dpi_cycle.c src/core/armed_session.c \
           src/core/scroll_pipeline.c src/core/capture_plan.c src/core/spaces_shortcuts.c \
           src/core/hires_wheel.c src/core/tool_path.c src/core/action_queue.c \
           src/core/daemon_state.c src/core/onboarding.c src/core/input_runtime.c src/core/daemon_safety.c src/core/action_job.c
SRC_IPC = src/ipc/ipc_server.c src/ipc/ipc_client.c
SRC_PLATFORM =
SRC_HOOK =
SRC_INJECT =
LDFLAGS = -pthread

ifeq ($(UNAME_S),Darwin)
    SRC_PLATFORM += src/platform/macos/hid_iokit.c src/platform/macos/app_watch.c src/platform/macos/onboarding_macos.c
    SRC_HOOK += src/hook/hook_macos.c
    SRC_INJECT += src/inject/inject_macos.c
    LDFLAGS += -framework IOKit -framework CoreFoundation -framework ApplicationServices -framework AppKit
    ifeq ($(RELEASE),1)
        LDFLAGS += -Wl,-dead_strip
    endif
endif

VENDOR_SRC = vendor/yyjson/yyjson.c vendor/tomlc99/toml.c

OBJ_HID = $(patsubst src/%.c, $(BUILD_DIR)/src/%.o, $(SRC_HID))
OBJ_CORE = $(patsubst src/%.c, $(BUILD_DIR)/src/%.o, $(SRC_CORE))
OBJ_IPC = $(patsubst src/%.c, $(BUILD_DIR)/src/%.o, $(SRC_IPC))
OBJ_PLATFORM = $(patsubst src/%.c, $(BUILD_DIR)/src/%.o, $(SRC_PLATFORM))
OBJ_HOOK = $(patsubst src/%.c, $(BUILD_DIR)/src/%.o, $(SRC_HOOK))
OBJ_INJECT = $(patsubst src/%.c, $(BUILD_DIR)/src/%.o, $(SRC_INJECT))
OBJ_VENDOR = $(patsubst vendor/%.c, $(BUILD_DIR)/vendor/%.o, $(VENDOR_SRC))

CORE_OBJS = $(OBJ_HID) $(OBJ_CORE) $(OBJ_IPC) $(OBJ_PLATFORM) $(OBJ_HOOK) $(OBJ_INJECT) $(OBJ_VENDOR)

TEST_BINS = $(BIN_DIR)/test_hidpp_packet \
            $(BIN_DIR)/test_device_probe \
            $(BIN_DIR)/test_diversion_and_hook \
            $(BIN_DIR)/test_gesture_and_inject \
            $(BIN_DIR)/test_config \
            $(BIN_DIR)/test_ipc_server \
            $(BIN_DIR)/test_cli \
            $(BIN_DIR)/test_single_instance \
            $(BIN_DIR)/test_button_runtime \
            $(BIN_DIR)/test_wheel_math \
            $(BIN_DIR)/test_reapply_wake \
            $(BIN_DIR)/test_dpi_cycle \
            $(BIN_DIR)/test_armed_session \
            $(BIN_DIR)/test_gesture_accumulator_edge \
            $(BIN_DIR)/test_scroll_pipeline \
            $(BIN_DIR)/test_capture_plan \
            $(BIN_DIR)/test_spaces_shortcuts \
            $(BIN_DIR)/test_hires_wheel_curve \
            $(BIN_DIR)/test_actions \
            $(BIN_DIR)/test_action_queue \
            $(BIN_DIR)/test_lightweight_standby \
            $(BIN_DIR)/test_onboarding \
            $(BIN_DIR)/test_device_ownership

TOOLS = $(BIN_DIR)/openlogi $(BIN_DIR)/openlogid $(BIN_DIR)/openlogictl $(BIN_DIR)/openlogi_discover

.PHONY: all test clean dirs tools install uninstall strip asan help info app compdb

all: dirs $(CORE_OBJS) $(TEST_BINS) $(TOOLS) compile_commands.json

compdb compile_commands.json:
	@python3 scripts/gen_compile_commands.py

app: $(BIN_DIR)/openlogi
ifeq ($(UNAME_S),Darwin)
	@mkdir -p $(BIN_DIR)/OpenLogi.app/Contents/MacOS
	@mkdir -p $(BIN_DIR)/OpenLogi.app/Contents/Resources
	@cp -f $(BIN_DIR)/openlogi $(BIN_DIR)/OpenLogi.app/Contents/MacOS/openlogi
	@ln -sf openlogi $(BIN_DIR)/OpenLogi.app/Contents/MacOS/openlogid
	@ln -sf openlogi $(BIN_DIR)/OpenLogi.app/Contents/MacOS/openlogictl
	@ln -sf openlogi $(BIN_DIR)/OpenLogi.app/Contents/MacOS/openlogi_discover
	@cp -f packaging/macos/Info.plist $(BIN_DIR)/OpenLogi.app/Contents/Info.plist
	@if [ -f packaging/macos/AppIcon.icns ]; then cp -f packaging/macos/AppIcon.icns $(BIN_DIR)/OpenLogi.app/Contents/Resources/AppIcon.icns; fi
	@codesign --force --sign "$(CODE_SIGN_IDENTITY)" --identifier io.github.openlogi.agent "$(BIN_DIR)/OpenLogi.app"
	@codesign --verify --strict "$(BIN_DIR)/OpenLogi.app"
	@echo "[+] Successfully created $(BIN_DIR)/OpenLogi.app"
endif

dirs:
	@mkdir -p $(BUILD_DIR)/src/hid
	@mkdir -p $(BUILD_DIR)/src/core
	@mkdir -p $(BUILD_DIR)/src/ipc
	@mkdir -p $(BUILD_DIR)/src/hook
	@mkdir -p $(BUILD_DIR)/src/inject
	@mkdir -p $(BUILD_DIR)/src/platform/macos
	@mkdir -p $(BUILD_DIR)/src/cli
	@mkdir -p $(BUILD_DIR)/src/daemon
	@mkdir -p $(BUILD_DIR)/vendor/yyjson
	@mkdir -p $(BUILD_DIR)/vendor/tomlc99
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vendor/%.o: vendor/%.c
	@mkdir -p $(dir $@)
	# Compile vendor sources with relaxed warnings and optional sanitizer instrumentation
	$(CC) $(CPPFLAGS) -std=c99 -O2 -w $(EXTRA_VENDOR_CFLAGS) -Ivendor/yyjson -Ivendor/tomlc99 -c $< -o $@

$(BIN_DIR)/test_hidpp_packet: tests/test_hidpp_packet.c $(BUILD_DIR)/src/hid/hidpp_packet.o
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD_DIR)/src/hid/hidpp_packet.o $(LDFLAGS) -o $@

$(BIN_DIR)/test_device_probe: tests/test_device_probe.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_diversion_and_hook: tests/test_diversion_and_hook.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_gesture_and_inject: tests/test_gesture_and_inject.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_config: tests/test_config.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_ipc_server: tests/test_ipc_server.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_cli: tests/test_cli.c $(CORE_OBJS) $(BIN_DIR)/openlogictl
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_single_instance: tests/test_single_instance.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_button_runtime: tests/test_button_runtime.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_wheel_math: tests/test_wheel_math.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_reapply_wake: tests/test_reapply_wake.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_dpi_cycle: tests/test_dpi_cycle.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_armed_session: tests/test_armed_session.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_gesture_accumulator_edge: tests/test_gesture_accumulator_edge.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_scroll_pipeline: tests/test_scroll_pipeline.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_capture_plan: tests/test_capture_plan.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_spaces_shortcuts: tests/test_spaces_shortcuts.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_hires_wheel_curve: tests/test_hires_wheel_curve.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_actions: tests/test_actions.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_action_queue: tests/test_action_queue.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_lightweight_standby: tests/test_lightweight_standby.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/test_onboarding: tests/test_onboarding.c $(BUILD_DIR)/src/core/onboarding.o
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(BIN_DIR)/test_device_ownership: tests/test_device_ownership.c $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_OBJS) $(LDFLAGS) -o $@

$(BIN_DIR)/openlogi: $(BUILD_DIR)/src/cli/openlogi.o $(BUILD_DIR)/src/daemon/main.o $(BUILD_DIR)/src/cli/openlogictl.o $(BUILD_DIR)/src/cli/openlogi_discover.o $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@
	@ln -sf openlogi $(BIN_DIR)/openlogid
	@ln -sf openlogi $(BIN_DIR)/openlogictl
	@ln -sf openlogi $(BIN_DIR)/openlogi_discover

$(BIN_DIR)/openlogid $(BIN_DIR)/openlogictl $(BIN_DIR)/openlogi_discover: $(BIN_DIR)/openlogi

test: dirs $(TEST_BINS) $(TOOLS)
	@./tests/run_tests.sh $(VERBOSE)

tools: dirs $(TOOLS)

strip: all
	strip $(BIN_DIR)/openlogi
	@ls -lh $(TOOLS)

install: tools
	@mkdir -p $(DESTDIR)$(BINDIR)
	@install -m 755 $(BIN_DIR)/openlogi $(DESTDIR)$(BINDIR)/openlogi
	@ln -sf openlogi $(DESTDIR)$(BINDIR)/openlogid
	@ln -sf openlogi $(DESTDIR)$(BINDIR)/openlogictl
	@ln -sf openlogi $(DESTDIR)$(BINDIR)/openlogi_discover
ifeq ($(UNAME_S),Darwin)
	@codesign --force --deep -s - --identifier io.github.openlogi.agent $(DESTDIR)$(BINDIR)/openlogi 2>/dev/null || true
	@# Also install OpenLogi.app to ~/Applications (or /Applications) for macOS System Settings visibility
	@APP_DEST="$(HOME)/Applications/OpenLogi.app"; \
	if [ -w "/Applications" ]; then APP_DEST="/Applications/OpenLogi.app"; fi; \
	if [ -z "$(DESTDIR)" ]; then \
		mkdir -p "$$APP_DEST/Contents/MacOS" "$$APP_DEST/Contents/Resources"; \
		install -m 755 $(BIN_DIR)/openlogi "$$APP_DEST/Contents/MacOS/openlogi"; \
		ln -sf openlogi "$$APP_DEST/Contents/MacOS/openlogid"; \
		ln -sf openlogi "$$APP_DEST/Contents/MacOS/openlogictl"; \
		ln -sf openlogi "$$APP_DEST/Contents/MacOS/openlogi_discover"; \
		cp -f packaging/macos/Info.plist "$$APP_DEST/Contents/Info.plist"; \
		if [ -f packaging/macos/AppIcon.icns ]; then cp -f packaging/macos/AppIcon.icns "$$APP_DEST/Contents/Resources/AppIcon.icns"; fi; \
		codesign --force --deep -s - --identifier io.github.openlogi.agent "$$APP_DEST" 2>/dev/null || true; \
		/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$$APP_DEST" 2>/dev/null || true; \
		touch "$$APP_DEST" 2>/dev/null || true; \
		echo "[+] Created macOS application bundle at $$APP_DEST"; \
	fi
endif
	@echo "[+] Successfully installed OpenLogi C tools to $(DESTDIR)$(BINDIR)"

uninstall:
	@# Stop any active daemon instances
	@-pkill -x openlogi 2>/dev/null || true
	@-pkill -x openlogid 2>/dev/null || true
	@# Unload and remove macOS LaunchAgent
	@-launchctl unload $(HOME)/Library/LaunchAgents/io.github.openlogi.agent.plist 2>/dev/null || true
	@-rm -f $(HOME)/Library/LaunchAgents/io.github.openlogi.agent.plist
	@# Stop and disable Linux systemd user service
	@-systemctl --user stop openlogid.service 2>/dev/null || true
	@-systemctl --user disable openlogid.service 2>/dev/null || true
	@-rm -f $(HOME)/.config/systemd/user/openlogid.service
	@# Remove installed binaries
	@rm -f $(DESTDIR)$(BINDIR)/openlogi
	@rm -f $(DESTDIR)$(BINDIR)/openlogid
	@rm -f $(DESTDIR)$(BINDIR)/openlogictl
	@rm -f $(DESTDIR)$(BINDIR)/openlogi_discover
	@rm -rf $(HOME)/Applications/OpenLogi.app /Applications/OpenLogi.app 2>/dev/null || true
	@# Clean up runtime state socket and pid locks
	@-rm -f $(HOME)/.local/state/openlogi/openlogi.sock /tmp/openlogi.sock
	@-rm -f $(HOME)/.local/state/openlogi/openlogi.lock /tmp/openlogid.lock
	@echo "[+] Successfully uninstalled OpenLogi C tools from $(DESTDIR)$(BINDIR) and stopped daemon."

asan: clean
	$(MAKE) test CFLAGS="$(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer" EXTRA_VENDOR_CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="$(LDFLAGS) -fsanitize=address,undefined"

help info:
	@echo "================================================================================"
	@echo "                      OpenLogi Pure C Build System (C99)"
	@echo "================================================================================"
	@echo "  Target              Description"
	@echo "  ------------------  ----------------------------------------------------------"
	@echo "  make [all]          Build runtime tools, core libraries, and 19 test suites"
	@echo "  make tools          Build only runtime binaries (openlogid, openlogictl, etc.)"
	@echo "  make test           Run all 19 test suites (98 test cases) via visual runner"
	@echo "  make test VERBOSE=1 Run tests with microsecond-level test case timing"
	@echo "  make asan           Run full test suite under ASan & UBSan (0 leaks, 0 errors)"
	@echo "  make strip          Strip release symbols and show minimal binary sizes"
	@echo "  make install        Install binaries to $(DESTDIR)$(BINDIR)"
	@echo "  make uninstall      Remove installed binaries from $(DESTDIR)$(BINDIR)"
	@echo "  make clean          Remove build/ and bin/ directories"
	@echo "  make help / info    Display this target reference and build environment"
	@echo "================================================================================"
	@echo "  Platform:   $(UNAME_S)"
	@echo "  Compiler:   $(CC)"
	@echo "  C Flags:    $(CFLAGS)"
	@echo "  Prefix:     $(PREFIX)"
	@echo "================================================================================"

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
