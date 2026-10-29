# =================================================================================================
# Doom 3 BFG PS2 build targets
# The default ELF is the game loop; native smoketest executables are built separately.
# =================================================================================================
#
# Build, packaging and test commands (run from the repository root). Targets follow one
# pattern: build-<image> builds a test image, stage-<package> stages a runnable package,
# run-<package> launches it in PCSX2, and test-<area> runs automated checks.
#
# Game:
#   make                         # production d3bfg.elf, debug
#   make release                 # production d3bfg.elf, release
#
# Test images (build/<config>/tests/<image>/d3bfg_tests.elf):
#   make build-core              # scalar Doom foundation and integration tests
#   make build-platform          # SDK/timer/alignment probe
#   make build-game              # native campaign with the game and map fixtures
#   make build-script            # isolated native script compiler
#
# Packages (build/<config>/<package>/, each the production ELF):
#   make stage-headless          # authored boot data, headless
#   make stage-headless-retail   # the local retail containers, headless
#   make stage-console           # visible console (CONSOLE_STANDARD=ntsc or pal)
#   make stage-ui-fonts          # the English font resource screen
#   make stage-ui-movies         # inactive shell/dialog SWFs and the English dictionary
#   make stage-ui-menu           # boots into the native shell menu
#   make run-<package>           # stage and launch any of these, until quit (make run = run-headless)
#
# Tests:
#   make test-host               # host format/logic/runner tests and ASan/UBSan harnesses
#   make test-platform           # the SDK platform probe in PCSX2
#   make test-core               # core smoke run in PCSX2
#   make test-common             # partial-startup and expected-failure checks
#   make test-renderer           # NTSC/PAL sprites/indexed UI, native pixels, reload and failure probes
#   make run-renderer            # the renderer scene plus a 60-second inspection hold; F8 also saves a capture
#   make test-script             # isolated native script compiler checks
#   make test-game               # native game/player/tick/reload and failure checks
#   make test-map                # native authored map/world/PVS/AAS/AI startup and ticks
#   make test-headless           # 120 frames, live console commands and shutdown
#   make test-headless-retail    # retail startup ledgers, 120 frames and shutdown
#   make test-ui                 # every UI screen: one retail session per standard (boot menu, console, fonts, movies, dialog, shell, menu screens, quit dialog) and the invalid-resource cases
#
# Tooling:
#   make tools                   # audit sources and build the host tools
#   make resource-unpack         # standalone C++20 host resource reader/extractor (CMake)
#   make asset-reexport          # C++20 host converter of retail textures to paletted GSTX (CMake)
#   make ui-inventory            # inspect local retail UI dependencies and feature gaps
#   make ui-cook                 # cook the ignored local retail English resource package
#   make audit-sources           # audit explicit source lists and exclusions
#   make compiledb               # compile_commands.json from real Make rules
#   make clean                   # remove generated build/test artifacts
#
# BUILD=debug is the default; BUILD=release selects an assertions-off build.
# CONSOLE_STANDARD=ntsc (default) or pal selects visible package presentation.
# PCSX2=/path/to/PCSX2 overrides the emulator executable.
# UI_DATA_ROOT and UI_COOK_OUTPUT select the local retail installation/cook output;
# UI_PREVIEWS=DIR (under build/) makes ui-cook also write original/converted PNG previews.
# RETAIL_DATA_ROOT (default UI_DATA_ROOT) selects the installation stage-headless-retail mounts.
# Emulator test targets require an isolated PCSX2 session; run them sequentially.
# Developer setup, controls and diagnostics: docs/DEVELOPMENT.md.

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

PS2DEV ?= $(HOME)/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk
PCSX2 ?= /Applications/PCSX2.app/Contents/MacOS/PCSX2
CONSOLE_STANDARD ?= ntsc
UI_DATA_ROOT ?= gamedata/d3_bfg
RETAIL_DATA_ROOT ?= $(UI_DATA_ROOT)
UI_COOK_OUTPUT ?= build/ui-retail
UI_RETAIL_CONFIG ?= config/ui_retail.json
PYTHON ?= python3
EE_TOOL_PREFIX ?= $(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-

include $(PS2SDK)/samples/Makefile.pref
include config/sources.mk

ifneq ($(filter release,$(MAKECMDGOALS)),)
	BUILD := release
else
	BUILD ?= debug
endif

ifeq ($(filter $(BUILD),debug release),)
	$(error BUILD must be debug or release)
endif

# Keep the SDK-only probe and Doom foundation explicit in both builds and reports.
ifneq ($(filter build-platform test-platform,$(MAKECMDGOALS)),)
	ifneq ($(filter build-core test-core,$(MAKECMDGOALS)),)
		$(error build-platform and build-core must run separately)
	endif
	CORE_BOOT := 0
else ifneq ($(filter build-core test-core,$(MAKECMDGOALS)),)
	CORE_BOOT := 1
else
	CORE_BOOT ?= 1
endif

ifeq ($(filter $(CORE_BOOT),0 1),)
	$(error CORE_BOOT must be 0 or 1)
endif

STRIP_ELF ?= 1
ifeq ($(filter $(STRIP_ELF),0 1),)
	$(error STRIP_ELF must be 0 or 1)
endif

SCRIPT_PROBE ?= 0
ifeq ($(filter $(SCRIPT_PROBE),0 1),)
	$(error SCRIPT_PROBE must be 0 or 1)
endif
ifeq ($(SCRIPT_PROBE),1)
	ifneq ($(CORE_BOOT),1)
		$(error SCRIPT_PROBE requires CORE_BOOT=1)
	endif
	ifneq ($(filter all release,$(or $(MAKECMDGOALS),all)),)
		$(error the production link requires the ordinary campaign object tree, not SCRIPT_PROBE=1)
	endif
endif
OUTPUT_DIR = build/$(BUILD)$(if $(filter 1,$(SCRIPT_PROBE)),-script,)
# Select the core-stage provider without another test-enable compiler define.
ifeq ($(CORE_BOOT),0)
	CORE_TEST_DRIVER_CXX_SRC += $(PLATFORM_TEST_CXX_SRC)
endif
ifeq ($(SCRIPT_PROBE),1)
	CORE_TEST_DRIVER_CXX_SRC = $(SCRIPT_BOOT_CXX_SRC)
	CORE_BOOT_CXX_SRC =
endif

CORE_TEST_DIR = $(OUTPUT_DIR)/tests/$(if $(filter 1,$(SCRIPT_PROBE)),script,$(if $(filter 1,$(CORE_BOOT)),core,platform))
EE_BIN = $(CORE_TEST_DIR)/d3bfg_tests_unstripped.elf
TEST_ELF = $(CORE_TEST_DIR)/d3bfg_tests.elf
GAME_ELF = $(OUTPUT_DIR)/d3bfg.elf
GAME_TEST_DIR = $(OUTPUT_DIR)/tests/game
GAME_TEST_ELF = $(GAME_TEST_DIR)/d3bfg_tests.elf
SCRIPTS = src/tools/scripts

ifeq ($(BUILD),release)
	OPTFLAGS = -O3
	DBGFLAGS =
	CONFIG_DEFS = -DPS2_D3BFG_DEBUG=0 -DPS2_D3BFG_ASSERTS=0 -DPS2_D3BFG_PROFILE=0 -DNDEBUG
else
	OPTFLAGS = -O2
	DBGFLAGS = -gdwarf-2 -gz
	CONFIG_DEFS = -DPS2_D3BFG_DEBUG=1 -DPS2_D3BFG_ASSERTS=1 -DPS2_D3BFG_PROFILE=1 -D_DEBUG=1
endif

COMMON_DEFS = -DPS2_D3BFG $(CONFIG_DEFS)
LANGFLAGS = -std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-strict-aliasing
SDK_INCS = -isystem $(PS2SDK)/ee/include -isystem $(PS2SDK)/common/include
PROJECT_INCS = -Isrc -Isrc/neo -isystem src/external
HEAP_VENDOR_INCS = -isystem src/external
ENGINE_BRIDGE_INCS = -isystem src/neo
COMMON_CXXFLAGS = -D_EE -G0 $(OPTFLAGS) $(DBGFLAGS) $(LANGFLAGS) $(COMMON_DEFS) $(SDK_INCS) $(PROJECT_INCS) -MD -MP

# Apply the complete GCC diagnostics to every new project source. SDK headers
# are system headers; project headers remain checked.
EE_CXX_WARNFLAGS = -Wall -Wextra -Werror \
	-Wshadow -Wdouble-promotion -Wconversion -Wsign-conversion \
	-Wformat=2 -Wno-format-nonliteral -Wundef -Wpointer-arith \
	-Wcast-align -Wwrite-strings -Wredundant-decls -Wnull-dereference \
	-Wnon-virtual-dtor -Woverloaded-virtual -Wvla \
	-Wlogical-op -Wduplicated-cond -Wduplicated-branches

PS2_CXXFLAGS = $(COMMON_CXXFLAGS) $(EE_CXX_WARNFLAGS) -DID_PS2_CORE=1 -DPS2_D3BFG_TESTS=1

# Legacy code keeps visible warnings and all language restrictions. Any
# necessary warning suppression belongs on the individual object, with a reason.
# Single-precision literals avoid software-double helpers on the EE.
NEO_CXXFLAGS = $(COMMON_CXXFLAGS) -Wall -Wextra -fsingle-precision-constant
CORE_CXXFLAGS = $(NEO_CXXFLAGS) -DID_PS2_CORE=1 -DPS2_D3BFG_TESTS=1
FOUNDATION_HEADER_FLAGS = -UID_PS2_CORE -DPS2_D3BFG_FOUNDATION=1
CAMPAIGN_CXXFLAGS = $(NEO_CXXFLAGS) -DPS2_D3BFG_TESTS=0
GAME_RUNTIME_CXXFLAGS = $(filter-out -DPS2_D3BFG_TESTS=1,$(PS2_CXXFLAGS)) -DPS2_D3BFG_TESTS=0
GAME_TEST_CXXFLAGS = $(NEO_CXXFLAGS) -DPS2_D3BFG_TESTS=1

# Observed in GCC 15's scalar idlib build: MSVC pragmas, unused public interface
# parameters, legacy copy/memset conventions, ineffective const casts, and old
# mutable string-literal interfaces. Apply only to explicitly listed upstream
# objects. Format, bounds, uninitialized and destructor diagnostics stay visible.
LEGACY_SHARED_WARNFLAGS = -Wno-unknown-pragmas -Wno-unused-parameter \
	-Wno-deprecated-copy -Wno-class-memaccess -Wno-ignored-qualifiers -Wno-write-strings

CORE_TEST_DRIVER_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_TEST_DRIVER_CXX_SRC:.cpp=.o))
CORE_OBJS = $(addprefix $(OUTPUT_DIR)/core/src/,$(CORE_CXX_SRC:.cpp=.o) $(CORE_FRAMEWORK_CXX_SRC:.cpp=.o))
CORE_BACKEND_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_BACKEND_CXX_SRC:.cpp=.o))
CORE_C_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_C_SRC:.c=.o))
CAMPAIGN_OBJS = $(addprefix $(OUTPUT_DIR)/campaign/src/,$(CAMPAIGN_CXX_SRC:.cpp=.o))
GAME_BACKEND_OBJS = $(addprefix $(OUTPUT_DIR)/game-backend/src/,$(GAME_BACKEND_CXX_SRC:.cpp=.o))
SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(SIZE_OPT_CXX_SRC:.cpp=.o))
GAME_SIZE_OPT_CXX_SRC = $(filter $(SIZE_OPT_CXX_SRC),$(GAME_BACKEND_CXX_SRC))
GAME_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/game-backend/src/,$(GAME_SIZE_OPT_CXX_SRC:.cpp=.o))
RESIDENT_SUPPORT_OBJS = $(addprefix $(OUTPUT_DIR)/resident-support/src/,$(RESIDENT_SUPPORT_CXX_SRC:.cpp=.o) $(PS2_CXX_SRC:.cpp=.o))
RESIDENT_SIZE_OPT_CXX_SRC = $(filter $(SIZE_OPT_CXX_SRC),$(RESIDENT_SUPPORT_CXX_SRC) $(PS2_CXX_SRC))
RESIDENT_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/resident-support/src/,$(RESIDENT_SIZE_OPT_CXX_SRC:.cpp=.o))
RESIDENT_LINK_SOURCES = $(CAMPAIGN_CXX_SRC) $(GAME_BACKEND_CXX_SRC) $(RESIDENT_SUPPORT_CXX_SRC) $(PS2_CXX_SRC) $(CORE_C_SRC)
RESIDENT_LINK_OBJS = $(CAMPAIGN_OBJS) $(GAME_BACKEND_OBJS) $(RESIDENT_SUPPORT_OBJS) $(CORE_C_OBJS)

TEST_CAMPAIGN_OBJS = $(addprefix $(OUTPUT_DIR)/test-campaign/src/,$(CAMPAIGN_CXX_SRC:.cpp=.o))
TEST_BACKEND_OBJS = $(addprefix $(OUTPUT_DIR)/test-backend/src/,$(GAME_BACKEND_CXX_SRC:.cpp=.o))
TEST_SUPPORT_OBJS = $(addprefix $(OUTPUT_DIR)/test-support/src/,$(RESIDENT_SUPPORT_CXX_SRC:.cpp=.o) $(GAME_TEST_CXX_SRC:.cpp=.o))
TEST_SUPPORT_SIZE_OPT_CXX_SRC = $(filter $(SIZE_OPT_CXX_SRC),$(RESIDENT_SUPPORT_CXX_SRC) $(GAME_TEST_CXX_SRC))
TEST_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/test-backend/src/,$(GAME_SIZE_OPT_CXX_SRC:.cpp=.o)) $(addprefix $(OUTPUT_DIR)/test-support/src/,$(TEST_SUPPORT_SIZE_OPT_CXX_SRC:.cpp=.o))
GAME_TEST_LINK_SOURCES = $(CAMPAIGN_CXX_SRC) $(GAME_BACKEND_CXX_SRC) $(RESIDENT_SUPPORT_CXX_SRC) $(GAME_TEST_CXX_SRC) $(CORE_C_SRC)
GAME_TEST_LINK_OBJS = $(TEST_CAMPAIGN_OBJS) $(TEST_BACKEND_OBJS) $(TEST_SUPPORT_OBJS) $(CORE_C_OBJS)

ifeq ($(CORE_BOOT),1)
	BOOT_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_BOOT_CXX_SRC:.cpp=.o))
	LINK_OBJS = $(CORE_TEST_DRIVER_OBJS) $(BOOT_OBJS) $(CORE_OBJS) $(CORE_BACKEND_OBJS) $(CORE_C_OBJS)
	LINK_SOURCES = $(CORE_TEST_DRIVER_CXX_SRC) $(CORE_BOOT_CXX_SRC) $(CORE_CXX_SRC) $(CORE_FRAMEWORK_CXX_SRC) $(CORE_BACKEND_CXX_SRC) $(CORE_C_SRC)
	MILESTONE = build-core
	REPORT_FLAGS = $(OUTPUT_DIR)/.backend-flags.json $(OUTPUT_DIR)/.core-flags.json $(OUTPUT_DIR)/.vendor-flags.json $(CORE_TEST_DIR)/.link-flags.json
else
	BOOT_OBJS =
	LINK_OBJS = $(CORE_TEST_DRIVER_OBJS)
	LINK_SOURCES = $(CORE_TEST_DRIVER_CXX_SRC)
	MILESTONE = build-platform
	REPORT_FLAGS = $(OUTPUT_DIR)/.backend-flags.json $(CORE_TEST_DIR)/.link-flags.json
endif

ifeq ($(SCRIPT_PROBE),1)
	SCRIPT_OBJS = $(addprefix $(OUTPUT_DIR)/script/src/,$(SCRIPT_CXX_SRC:.cpp=.o))
	LINK_OBJS += $(SCRIPT_OBJS)
	LINK_SOURCES += $(SCRIPT_CXX_SRC)
	MILESTONE = build-script
	REPORT_FLAGS += $(OUTPUT_DIR)/.script-flags.json
endif

# Do not garbage-collect registration objects to manufacture a smaller link.
# The loaded PT_LOAD sizes are recorded independently of ELF stripping.
EE_LINKFILE ?= $(PS2SDK)/ee/startup/linkfile
EE_LDFLAGS = -T$(EE_LINKFILE) -L$(PS2SDK)/ee/lib -Wl,-zmax-page-size=128 -Wl,-Map,$(CORE_TEST_DIR)/d3bfg_tests.map
EE_LIBS = -lkernel -lm
RENDER_SDK_LIBS = draw graph packet2 dma
RESIDENT_SDK_LIBS = $(RENDER_SDK_LIBS) pad kbd kernel m patches
ifeq ($(CORE_BOOT),1)
	EE_LIBS = $(addprefix -l,$(RESIDENT_SDK_LIBS))
endif
# Isolated compiler fixture only: discard game-object/save/interpreter methods whose services
# are deliberately absent. Never use this gate as evidence of a resident game link.
ifeq ($(SCRIPT_PROBE),1)
	EE_LDFLAGS += -Wl,--gc-sections
	SCRIPT_CXXFLAGS = $(GAME_TEST_CXXFLAGS) $(LEGACY_SHARED_WARNFLAGS) -ffunction-sections -fdata-sections
$(OUTPUT_DIR)/.script-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(SCRIPT_CXXFLAGS) --sources $(SCRIPT_CXX_SRC)
$(SCRIPT_OBJS): $(OUTPUT_DIR)/script/src/%.o: src/%.cpp $(OUTPUT_DIR)/.script-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(SCRIPT_CXXFLAGS) -c $< -o $@
$(OUTPUT_DIR)/src/tests/smoketests/script_boot.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE
endif

.PHONY: all release stage-headless run-headless test-headless stage-headless-retail run-headless-retail test-headless-retail stage-console run-console test-ui ui-inventory ui-cook resource-unpack asset-reexport build-platform build-core build-game test-map test-game test-renderer run-renderer audit-sources compiledb tools run build-script test-script test-common test-host test-core test-platform core-test-image clean FORCE

release: all
build-platform: core-test-image
build-core: core-test-image
core-test-image: audit-sources $(TEST_ELF) $(CORE_TEST_DIR)/build-report.json

audit-sources:
	@$(PYTHON) $(SCRIPTS)/audit_sources.py

# A stamp is rewritten only when its compiler identity, flags or source list
# changes. This fixes the reference's stale-object behavior after flag edits.
$(OUTPUT_DIR)/.backend-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) --cold-sources $(SIZE_OPT_CXX_SRC) --heap-vendor-includes $(HEAP_VENDOR_INCS) --engine-bridge-includes $(ENGINE_BRIDGE_INCS) --foundation-header-flags $(FOUNDATION_HEADER_FLAGS) --full-header-backend ps2/ui/assets.cpp ps2/input/usercmd_stub.cpp tests/smoketests/input_tests.cpp ps2/game/multiplayer_stub.cpp ps2/system/save_metadata.cpp tests/smoketests/deferred_tests.cpp $(RENDER_STUB_CXX_SRC) ps2/system/common_campaign.cpp ps2/renderer/model_import.cpp ps2/ui/draw.cpp ps2/renderer/image_backend.cpp ps2/system/sys_services.cpp ps2/audio/sound_backend.cpp ps2/system/common_foundation.cpp ps2/system/offline_session.cpp tests/smoketests/renderer_tests.cpp tests/smoketests/renderer_gs_tests.cpp tests/smoketests/audio_tests.cpp tests/smoketests/offline_tests.cpp --sources $(CORE_TEST_DRIVER_CXX_SRC) $(CORE_BACKEND_CXX_SRC) $(CORE_BOOT_CXX_SRC)

$(OUTPUT_DIR)/.core-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(CORE_CXXFLAGS) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --foundation-header-flags $(FOUNDATION_HEADER_FLAGS) --full-header-core neo/framework/Common.cpp neo/framework/PlayerProfile.cpp neo/sys/Snapshot.cpp neo/sys/LightweightCompression.cpp neo/sys/Snapshot_Jobs.cpp neo/sys/sys_localuser.cpp neo/sys/sys_signin.cpp --sources $(CORE_CXX_SRC) $(CORE_FRAMEWORK_CXX_SRC)

$(OUTPUT_DIR)/.campaign-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(CAMPAIGN_CXXFLAGS) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --sources $(CAMPAIGN_CXX_SRC)

VENDOR_CFLAGS = -D_EE -G0 $(OPTFLAGS) $(DBGFLAGS) -std=gnu11 -fno-strict-aliasing -Wall -Wextra $(COMMON_DEFS) $(SDK_INCS) $(PROJECT_INCS) -MD -MP

$(OUTPUT_DIR)/.vendor-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CC) $(VENDOR_CFLAGS) --sources $(CORE_C_SRC)

$(CORE_TEST_DIR)/.link-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(OPTFLAGS) $(EE_LDFLAGS) $(EE_LIBS) --objects $(LINK_OBJS) --milestone $(MILESTONE)

$(CORE_TEST_DIR)/.strip-flags.json: FORCE $(SCRIPTS)/build_metadata.py
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) STRIP_ELF=$(STRIP_ELF) STRIP=$(EE_STRIP)

$(CORE_TEST_DRIVER_OBJS) $(BOOT_OBJS) $(CORE_BACKEND_OBJS): $(OUTPUT_DIR)/src/%.o: src/%.cpp $(OUTPUT_DIR)/.backend-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(PS2_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(CXX_VENDOR_INCS_FOR) -c $< -o $@

$(CORE_C_OBJS): $(OUTPUT_DIR)/src/%.o: src/%.c $(OUTPUT_DIR)/.vendor-flags.json
	@mkdir -p $(dir $@)
	$(EE_CC) $(VENDOR_CFLAGS) -c $< -o $@

$(SIZE_OPT_OBJS) $(GAME_SIZE_OPT_OBJS) $(RESIDENT_SIZE_OPT_OBJS) $(TEST_SIZE_OPT_OBJS): CXX_OPTFLAGS_FOR = -Os

# Only heap.cpp imports dlmalloc's declaration header. Angle inclusion marks
# that vendor header as system without hiding warnings in heap.cpp or heap.h.
$(OUTPUT_DIR)/src/ps2/system/heap.o: CXX_VENDOR_INCS_FOR = $(HEAP_VENDOR_INCS)
$(OUTPUT_DIR)/src/ps2/ui/paletted_image.o $(OUTPUT_DIR)/src/ps2/renderer/gs.o $(OUTPUT_DIR)/src/ps2/renderer/cmd_buffer.o $(OUTPUT_DIR)/src/ps2/renderer/texture.o $(OUTPUT_DIR)/src/ps2/ui/draw.o $(OUTPUT_DIR)/src/ps2/renderer/image_backend.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS)

# Checked backend code includes upstream public contracts through this seam.
# Only neo headers become system headers; ps2 and test headers stay checked.
$(OUTPUT_DIR)/src/ps2/system/core.o $(OUTPUT_DIR)/src/ps2/system/sys.o $(OUTPUT_DIR)/src/ps2/system/sys_filesystem.o $(BOOT_OBJS): CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS)

# These checks inspect real campaign contracts; the session test exercises the offline replacement.
$(OUTPUT_DIR)/src/tests/smoketests/type_query_engine_tests.o $(OUTPUT_DIR)/src/tests/smoketests/audio_tests.o $(OUTPUT_DIR)/src/tests/smoketests/offline_tests.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE

$(CORE_OBJS): $(OUTPUT_DIR)/core/src/%.o: src/%.cpp $(OUTPUT_DIR)/.core-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(CORE_CXXFLAGS) $(CORE_HEADER_FLAGS) $(NEO_WARNFLAGS_FOR) -c $< -o $@

$(CAMPAIGN_OBJS): $(OUTPUT_DIR)/campaign/src/%.o: src/%.cpp $(OUTPUT_DIR)/.campaign-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(CAMPAIGN_CXXFLAGS) $(NEO_WARNFLAGS_FOR) -c $< -o $@

$(OUTPUT_DIR)/.game-backend-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(GAME_RUNTIME_CXXFLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --cold-sources $(GAME_SIZE_OPT_CXX_SRC) --sources $(GAME_BACKEND_CXX_SRC)

$(GAME_BACKEND_OBJS): $(OUTPUT_DIR)/game-backend/src/%.o: src/%.cpp $(OUTPUT_DIR)/.game-backend-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(GAME_RUNTIME_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE -c $< -o $@

$(OUTPUT_DIR)/.resident-support-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(GAME_RUNTIME_CXXFLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --heap-vendor-includes $(HEAP_VENDOR_INCS) --cold-sources $(RESIDENT_SIZE_OPT_CXX_SRC) --sources $(RESIDENT_SUPPORT_CXX_SRC) $(PS2_CXX_SRC)

$(RESIDENT_SUPPORT_OBJS): $(OUTPUT_DIR)/resident-support/src/%.o: src/%.cpp $(OUTPUT_DIR)/.resident-support-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(GAME_RUNTIME_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE $(CXX_VENDOR_INCS_FOR) -c $< -o $@

$(OUTPUT_DIR)/resident-support/src/ps2/system/heap.o: CXX_VENDOR_INCS_FOR = $(HEAP_VENDOR_INCS)

$(OUTPUT_DIR)/.test-campaign-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(GAME_TEST_CXXFLAGS) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --sources $(CAMPAIGN_CXX_SRC)

$(TEST_CAMPAIGN_OBJS): $(OUTPUT_DIR)/test-campaign/src/%.o: src/%.cpp $(OUTPUT_DIR)/.test-campaign-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(GAME_TEST_CXXFLAGS) $(LEGACY_SHARED_WARNFLAGS) -c $< -o $@

$(OUTPUT_DIR)/.test-backend-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --cold-sources $(GAME_SIZE_OPT_CXX_SRC) --sources $(GAME_BACKEND_CXX_SRC)

$(TEST_BACKEND_OBJS): $(OUTPUT_DIR)/test-backend/src/%.o: src/%.cpp $(OUTPUT_DIR)/.test-backend-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(PS2_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE -c $< -o $@

$(OUTPUT_DIR)/.test-support-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --heap-vendor-includes $(HEAP_VENDOR_INCS) --cold-sources $(TEST_SUPPORT_SIZE_OPT_CXX_SRC) --sources $(RESIDENT_SUPPORT_CXX_SRC) $(GAME_TEST_CXX_SRC)

$(TEST_SUPPORT_OBJS): $(OUTPUT_DIR)/test-support/src/%.o: src/%.cpp $(OUTPUT_DIR)/.test-support-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(PS2_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE $(CXX_VENDOR_INCS_FOR) -c $< -o $@

$(OUTPUT_DIR)/test-support/src/ps2/system/heap.o: CXX_VENDOR_INCS_FOR = $(HEAP_VENDOR_INCS)

# The foundation uses the real Common layout and retained session/profile value types.
$(OUTPUT_DIR)/core/src/neo/framework/Common.o: CORE_HEADER_FLAGS = $(FOUNDATION_HEADER_FLAGS)
$(addprefix $(OUTPUT_DIR)/core/src/,$(filter-out neo/framework/Common.o,$(CORE_FRAMEWORK_CXX_SRC:.cpp=.o))): CORE_HEADER_FLAGS =
$(addprefix $(OUTPUT_DIR)/core/src/,neo/framework/PlayerProfile.o neo/sys/Snapshot.o neo/sys/LightweightCompression.o neo/sys/Snapshot_Jobs.o neo/sys/sys_localuser.o neo/sys/sys_signin.o): CORE_HEADER_FLAGS = -UID_PS2_CORE
$(addprefix $(OUTPUT_DIR)/src/,$(RENDER_STUB_CXX_SRC:.cpp=.o) ps2/ui/assets.o ps2/ui/draw.o ps2/renderer/image_backend.o ps2/input/usercmd_stub.o ps2/game/multiplayer_stub.o ps2/system/save_metadata.o ps2/system/common_campaign.o ps2/renderer/model_import.o ps2/system/sys_services.o ps2/audio/sound_backend.o ps2/system/common_foundation.o ps2/system/offline_session.o): CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) $(FOUNDATION_HEADER_FLAGS)
$(OUTPUT_DIR)/src/tests/smoketests/renderer_tests.o $(OUTPUT_DIR)/src/tests/smoketests/renderer_gs_tests.o $(OUTPUT_DIR)/src/tests/smoketests/deferred_tests.o $(OUTPUT_DIR)/src/tests/smoketests/input_tests.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) $(FOUNDATION_HEADER_FLAGS)
$(OUTPUT_DIR)/src/ps2/system/lifecycle.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS)
$(CORE_OBJS) $(CAMPAIGN_OBJS): NEO_WARNFLAGS_FOR = $(LEGACY_SHARED_WARNFLAGS)

$(EE_BIN): $(LINK_OBJS) $(CORE_TEST_DIR)/.link-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(OPTFLAGS) -o $@ $(LINK_OBJS) $(EE_LDFLAGS) $(EE_LIBS)

$(TEST_ELF): $(EE_BIN) $(CORE_TEST_DIR)/.strip-flags.json
ifeq ($(STRIP_ELF),1)
	$(EE_STRIP) --strip-all -o $@ $<
else
	cp -f $< $@
endif

$(CORE_TEST_DIR)/.source-identity.json: FORCE $(LINK_OBJS) $(SCRIPTS)/build_metadata.py
	@$(PYTHON) $(SCRIPTS)/build_metadata.py identity --output $@ --sources $(addprefix src/,$(LINK_SOURCES)) --dependencies $(LINK_OBJS:.o=.d) --policies Makefile config/sources.mk config/source_inventory.json $(SCRIPTS)/build_metadata.py

$(CORE_TEST_DIR)/build-report.json: $(EE_BIN) $(TEST_ELF) $(REPORT_FLAGS) $(CORE_TEST_DIR)/.source-identity.json $(SCRIPTS)/build_metadata.py
	@$(PYTHON) $(SCRIPTS)/build_metadata.py report --elf $(EE_BIN) --runnable-elf $(TEST_ELF) --identity $(CORE_TEST_DIR)/.source-identity.json --output $(CORE_TEST_DIR)/build-report --compiler $(EE_CXX) --configuration $(BUILD) --milestone $(MILESTONE) --flags $(REPORT_FLAGS) --sources $(addprefix src/,$(LINK_SOURCES))

# Production retained-object link with no test sources or fixture hooks. Failed gates remove stale ELFs.
$(OUTPUT_DIR)/.resident-sdk-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) -T$(EE_LINKFILE) -L$(PS2SDK)/ee/lib $(addprefix -l,$(RESIDENT_SDK_LIBS))

all: audit-sources $(RESIDENT_LINK_OBJS) $(OUTPUT_DIR)/.resident-sdk-flags.json
	@$(PYTHON) $(SCRIPTS)/link_resident.py --compiler $(EE_CXX) --configuration $(BUILD) --output $(OUTPUT_DIR) --kind game --name d3bfg --linkfile $(EE_LINKFILE) --sdk-lib $(PS2SDK)/ee/lib --libraries $(RESIDENT_SDK_LIBS) --sources $(addprefix src/,$(RESIDENT_LINK_SOURCES)) --objects $(RESIDENT_LINK_OBJS) --flags $(OUTPUT_DIR)/.campaign-flags.json $(OUTPUT_DIR)/.game-backend-flags.json $(OUTPUT_DIR)/.resident-support-flags.json $(OUTPUT_DIR)/.vendor-flags.json $(OUTPUT_DIR)/.resident-sdk-flags.json

# The same retained campaign is compiled with fixture hooks into a separate native executable.
build-game: audit-sources $(GAME_TEST_LINK_OBJS) $(OUTPUT_DIR)/.resident-sdk-flags.json
	@$(PYTHON) $(SCRIPTS)/link_resident.py --compiler $(EE_CXX) --configuration $(BUILD) --output $(GAME_TEST_DIR) --kind tests --name d3bfg_tests --linkfile $(EE_LINKFILE) --sdk-lib $(PS2SDK)/ee/lib --libraries $(RESIDENT_SDK_LIBS) --sources $(addprefix src/,$(GAME_TEST_LINK_SOURCES)) --objects $(GAME_TEST_LINK_OBJS) --flags $(OUTPUT_DIR)/.test-campaign-flags.json $(OUTPUT_DIR)/.test-backend-flags.json $(OUTPUT_DIR)/.test-support-flags.json $(OUTPUT_DIR)/.vendor-flags.json $(OUTPUT_DIR)/.resident-sdk-flags.json

test-map: build-game
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-map

test-game: build-game
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-missing-map
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-syntax
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-geometry
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-material
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-player-args
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-player-script
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-player-command
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-player-state
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-ceiling-geometry
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-step-geometry

compiledb:
	@$(PYTHON) $(SCRIPTS)/gen_compile_commands.py --make $(MAKE) --build $(BUILD)

build-script:
	$(MAKE) --no-print-directory BUILD=$(BUILD) SCRIPT_PROBE=1 core-test-image

test-common: build-core
	$(PYTHON) $(SCRIPTS)/run_common_tests.py --emulator $(PCSX2) --elf $(TEST_ELF)

test-script: build-script
	$(PYTHON) $(SCRIPTS)/run_script_tests.py --elf build/$(BUILD)-script/tests/script/d3bfg_tests.elf

tools: audit-sources resource-unpack asset-reexport
	@echo "Host tools: build/tools/resource_unpack, build/tools/asset_reexport; Python tools: $(SCRIPTS)/."

resource-unpack: build/tools/resource_unpack

asset-reexport: build/tools/asset_reexport

ui-inventory:
	$(PYTHON) $(SCRIPTS)/inventory_retail_ui.py --data-root "$(UI_DATA_ROOT)" --output "$(UI_COOK_OUTPUT)" --config "$(UI_RETAIL_CONFIG)"

ui-cook: build/tools/asset_reexport
	$(PYTHON) $(SCRIPTS)/inventory_retail_ui.py --data-root "$(UI_DATA_ROOT)" --output "$(UI_COOK_OUTPUT)" --config "$(UI_RETAIL_CONFIG)" --cook $(if $(UI_PREVIEWS),--previews "$(UI_PREVIEWS)")

stage-headless: all
	$(PYTHON) $(SCRIPTS)/stage_headless.py --build $(BUILD)

run: run-headless
run-headless: stage-headless
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/headless/d3bfg.elf

test-headless: all
	$(PYTHON) $(SCRIPTS)/run_headless.py --build $(BUILD) --emulator $(PCSX2)

stage-headless-retail: all
	$(PYTHON) $(SCRIPTS)/stage_headless.py --build $(BUILD) --retail-data "$(RETAIL_DATA_ROOT)"

run-headless-retail: stage-headless-retail
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/headless-retail/d3bfg.elf

test-headless-retail: all
	$(PYTHON) $(SCRIPTS)/run_headless.py --build $(BUILD) --emulator $(PCSX2) --case retail --data-root "$(RETAIL_DATA_ROOT)"

stage-console: all
	$(PYTHON) $(SCRIPTS)/stage_headless.py --build $(BUILD) --presentation $(CONSOLE_STANDARD) --catalog "$(UI_COOK_OUTPUT)"

run-console: stage-console
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/console/d3bfg.elf

# One session per standard boots into the retail menu and visits every retail screen; each
# invalid-resource case ends its process in a fatal.
test-ui: all
	$(PYTHON) $(SCRIPTS)/run_ui_session.py --build $(BUILD) --source "$(UI_COOK_OUTPUT)" --emulator $(PCSX2)
	$(PYTHON) $(SCRIPTS)/run_retail_ui.py --build $(BUILD) --source "$(UI_COOK_OUTPUT)" --emulator $(PCSX2)
	$(PYTHON) $(SCRIPTS)/run_retail_movies.py --build $(BUILD) --source "$(UI_COOK_OUTPUT)" --emulator $(PCSX2)

.PHONY: stage-ui-fonts run-ui-fonts stage-ui-movies run-ui-movies stage-ui-menu run-ui-menu

stage-ui-fonts: all
	$(PYTHON) $(SCRIPTS)/stage_retail_ui.py --build $(BUILD) --standard $(CONSOLE_STANDARD) --source "$(UI_COOK_OUTPUT)"

run-ui-fonts: stage-ui-fonts
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/ui-fonts/d3bfg.elf

stage-ui-movies: all
	$(PYTHON) $(SCRIPTS)/stage_retail_ui.py --build $(BUILD) --standard $(CONSOLE_STANDARD) --source "$(UI_COOK_OUTPUT)" --movies

run-ui-movies: stage-ui-movies
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/ui-movies/d3bfg.elf

stage-ui-menu: all
	$(PYTHON) $(SCRIPTS)/stage_retail_ui.py --build $(BUILD) --standard $(CONSOLE_STANDARD) --source "$(UI_COOK_OUTPUT)" --movies --menu

run-ui-menu: stage-ui-menu
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/ui-menu/d3bfg.elf

test-core: build-core
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario core

test-platform: build-platform
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario platform

test-renderer: build-core
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ntsc
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-pal
# Render-time checks are assertions: the expected-fatal probes need an assertion build.
ifneq ($(BUILD),release)
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-gif-overflow
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-direct-overflow
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-wait-timeout
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-state
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-bounds
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-quad
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-text
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-indices
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-capacity
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-depth
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-half
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-color2
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-mask-nested
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-mask-leak
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-mask-key
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-mask-count
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ui-mask-capacity
endif

run-renderer: build-core
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ntsc --visual-wait 60 --timeout 90

HOST_CXX ?= clang++

HOST_WARNFLAGS = -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wconversion -Wsign-conversion \
	-Wformat=2 -Wno-format-nonliteral -Wundef -Wpointer-arith -Wcast-align -Wwrite-strings \
	-Wredundant-decls -Wnull-dereference -Wnon-virtual-dtor -Woverloaded-virtual -Wvla

HOST_TEST_FLAGS = -std=c++20 -O1 -g -fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-strict-aliasing -fsized-deallocation -fsanitize=address,undefined -fno-omit-frame-pointer \
	$(HOST_WARNFLAGS) -Isrc -isystem src/external -DID_HOST_TEST -DPS2_D3BFG_ASSERTS=1

RESOURCE_UNPACK_SRC = src/tools/resource_unpack/main.cpp src/tools/assets/resources.cpp

# The C++ host tools are a CMake project (src/tools/CMakeLists.txt) that owns their flags, so
# every host builds them alike, MSVC and CI included. Make configures it into build/tools/cmake/
# and runs its build every time; CMake tracks the sources and does nothing while the tools are
# current. A different HOST_CXX or compiler version rewrites the stamp, which starts a fresh tree.
HOST_TOOLS_CMAKE = build/tools/cmake

build/tools/.cmake-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) -DCMAKE_BUILD_TYPE=Release

$(HOST_TOOLS_CMAKE)/CMakeCache.txt: build/tools/.cmake-flags.json
	@rm -rf $(HOST_TOOLS_CMAKE)
	cmake -S src/tools -B $(HOST_TOOLS_CMAKE) -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(HOST_CXX) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(abspath build/tools) -DCMAKE_TARGET_MESSAGES=OFF

# MAKEFLAGS is cleared so the Makefiles CMake generates don't try to join this make's jobserver,
# which a non-recursive recipe doesn't pass on; --parallel gives them jobs of their own.
build/tools/resource_unpack: $(HOST_TOOLS_CMAKE)/CMakeCache.txt FORCE
	@MAKEFLAGS= cmake --build $(HOST_TOOLS_CMAKE) --parallel --target resource_unpack

build/tools/asset_reexport: $(HOST_TOOLS_CMAKE)/CMakeCache.txt FORCE
	@MAKEFLAGS= cmake --build $(HOST_TOOLS_CMAKE) --parallel --target asset_reexport

build/tests/.resource-unpack-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(RESOURCE_UNPACK_SRC)

build/tests/resource_unpack: $(RESOURCE_UNPACK_SRC) src/tools/assets/resources.h src/tools/assets/bytes.h build/tests/.resource-unpack-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -Isrc/tools $(RESOURCE_UNPACK_SRC) -o $@

# The shared host asset library under sanitizers, with its unit tests.
ASSET_LIB_SRC = $(sort $(wildcard src/tools/assets/*.cpp))
ASSET_LIB_HEADERS = $(sort $(wildcard src/tools/assets/*.h))

build/tests/.asset-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(ASSET_LIB_SRC) src/tests/host/asset_tests.cpp

build/tests/asset_tests: src/tests/host/asset_tests.cpp $(ASSET_LIB_SRC) $(ASSET_LIB_HEADERS) build/tests/.asset-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -ffp-contract=off -Isrc/tools src/tests/host/asset_tests.cpp $(ASSET_LIB_SRC) -o $@

# The target's GSTX admission against files the host converter writes.
build/tests/gstx_format_tests: src/tests/host/gstx_format_tests.cpp src/ps2/ui/gstx_format.cpp src/ps2/ui/gstx_format.h $(ASSET_LIB_SRC) $(ASSET_LIB_HEADERS) build/tests/.asset-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -ffp-contract=off -Isrc/tools src/tests/host/gstx_format_tests.cpp src/ps2/ui/gstx_format.cpp $(ASSET_LIB_SRC) -o $@

HOST_HEAP_SOURCES = src/tests/smoketests/type_query_tests.cpp src/tests/smoketests/type_query_bridge.cpp src/tests/host/heap_tests.cpp src/tests/smoketests/heap_tests.cpp src/tests/smoketests/class_alloc_tests.cpp src/ps2/game/class_alloc.cpp src/ps2/system/heap.cpp src/ps2/system/log.cpp

build/tests/.heap-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_HEAP_SOURCES)

build/tests/heap_tests: $(HOST_HEAP_SOURCES) src/tests/smoketests/heap_tests.h src/tests/smoketests/class_alloc_tests.h src/tests/smoketests/type_query_tests.h src/ps2/type_query.h src/ps2/game/class_alloc.h src/ps2/system/heap.h src/ps2/system/log.h src/ps2/common.h build/tests/.heap-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) $(HOST_HEAP_SOURCES) -o $@

HOST_COMMON_SOURCES = src/tests/host/common_tests.cpp src/ps2/system/log.cpp src/ps2/system/heap.cpp src/ps2/game/class_alloc.cpp src/ps2/game/script_error.cpp
HOST_COMMON_HEADERS = src/ps2/game/script_error.h src/ps2/common.h src/ps2/system/log.h src/ps2/system/heap.h src/ps2/game/class_alloc.h

build/tests/.common-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_COMMON_SOURCES)

build/tests/common_tests_%: $(HOST_COMMON_SOURCES) $(HOST_COMMON_HEADERS) build/tests/.common-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(filter-out -DPS2_D3BFG_ASSERTS=1,$(HOST_TEST_FLAGS)) -DPS2_D3BFG_ASSERTS=$* $(HOST_COMMON_SOURCES) -o $@

HOST_FILESYSTEM_SOURCES = src/tests/host/filesystem_tests.cpp src/ps2/system/filesystem.cpp src/ps2/system/log.cpp

build/tests/.filesystem-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_FILESYSTEM_SOURCES)

build/tests/filesystem_tests: $(HOST_FILESYSTEM_SOURCES) src/ps2/system/filesystem.h src/ps2/system/log.h src/ps2/common.h build/tests/.filesystem-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) $(HOST_FILESYSTEM_SOURCES) -o $@

HOST_PCM_SOURCES = src/tests/host/pcm_wave_tests.cpp src/ps2/audio/pcm_wave.cpp src/ps2/system/log.cpp

build/tests/.pcm-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_PCM_SOURCES)

build/tests/pcm_wave_tests: $(HOST_PCM_SOURCES) src/ps2/audio/pcm_wave.h src/ps2/common.h src/ps2/system/log.h build/tests/.pcm-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) $(HOST_PCM_SOURCES) -o $@

HOST_VOICE_SOURCES = src/tests/host/voice_timeline_tests.cpp src/tests/smoketests/voice_tests.cpp src/ps2/audio/voice_timeline.cpp src/ps2/system/log.cpp

build/tests/.voice-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_VOICE_SOURCES)

build/tests/voice_timeline_tests_%: $(HOST_VOICE_SOURCES) src/tests/smoketests/voice_tests.h src/ps2/audio/voice_timeline.h src/ps2/audio/pcm_wave.h src/ps2/common.h src/ps2/system/log.h build/tests/.voice-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(filter-out -DPS2_D3BFG_ASSERTS=1,$(HOST_TEST_FLAGS)) -DPS2_D3BFG_ASSERTS=$* $(HOST_VOICE_SOURCES) -o $@

build/tests/ui_geometry_tests: src/tests/host/ui_geometry_tests.cpp src/ps2/ui/geometry.cpp src/ps2/ui/geometry.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_geometry_tests.cpp src/ps2/ui/geometry.cpp -o $@

build/tests/ui_page_geometry_tests: src/tests/host/ui_page_geometry_tests.cpp src/ps2/ui/page_geometry.cpp src/ps2/ui/page_geometry.h src/ps2/ui/triangles.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_page_geometry_tests.cpp src/ps2/ui/page_geometry.cpp -o $@

build/tests/ui_font_format_tests: src/tests/host/ui_font_format_tests.cpp src/ps2/ui/font_format.cpp src/ps2/ui/font_format.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_font_format_tests.cpp src/ps2/ui/font_format.cpp -o $@

build/tests/ui_catalog_tests: src/tests/host/ui_catalog_tests.cpp src/ps2/ui/catalog.cpp src/ps2/ui/catalog.h src/ps2/ui/gstx_format.cpp src/ps2/ui/gstx_format.h src/ps2/ui/font_format.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_catalog_tests.cpp src/ps2/ui/catalog.cpp src/ps2/ui/gstx_format.cpp -o $@

build/tests/ui_triangles_tests: src/tests/host/ui_triangles_tests.cpp src/ps2/ui/triangles.cpp src/ps2/ui/masks.cpp src/ps2/ui/masks.h src/ps2/ui/triangles.h src/ps2/ui/geometry.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_triangles_tests.cpp src/ps2/ui/triangles.cpp src/ps2/ui/masks.cpp -o $@

build/tests/charset_tests: src/tests/host/charset_tests.cpp src/tests/smoketests/console_charset.cpp src/tests/smoketests/console_charset.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/charset_tests.cpp src/tests/smoketests/console_charset.cpp -o $@

build/tests/ui_swf_format_tests: src/tests/host/ui_swf_format_tests.cpp src/ps2/ui/swf_format.cpp src/ps2/ui/swf_format.h src/ps2/ui/swf_execution.cpp src/ps2/ui/swf_execution.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_swf_format_tests.cpp src/ps2/ui/swf_format.cpp src/ps2/ui/swf_execution.cpp -o $@

build/tests/swf_execution_tests: src/tests/host/swf_execution_tests.cpp src/ps2/ui/swf_execution.cpp src/ps2/ui/swf_execution.h src/ps2/ui/swf_format.cpp src/ps2/ui/swf_format.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/swf_execution_tests.cpp src/ps2/ui/swf_execution.cpp src/ps2/ui/swf_format.cpp -o $@

build/tests/swf_runtime_tests_%: src/tests/host/swf_runtime_tests.cpp src/ps2/ui/swf_runtime.cpp src/ps2/ui/swf_runtime.h src/ps2/system/log.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(filter-out -DPS2_D3BFG_ASSERTS=1,$(HOST_TEST_FLAGS)) -DPS2_D3BFG_ASSERTS=$* src/tests/host/swf_runtime_tests.cpp src/ps2/ui/swf_runtime.cpp -o $@

test-host: build/tools/asset_reexport build/tests/asset_tests build/tests/gstx_format_tests build/tests/swf_runtime_tests_0 build/tests/swf_runtime_tests_1 build/tests/swf_execution_tests build/tests/ui_swf_format_tests build/tests/ui_font_format_tests build/tests/ui_catalog_tests build/tests/ui_page_geometry_tests build/tests/resource_unpack build/tests/ui_triangles_tests build/tests/charset_tests build/tests/ui_geometry_tests build/tests/heap_tests build/tests/common_tests_1 build/tests/common_tests_0 build/tests/filesystem_tests build/tests/pcm_wave_tests build/tests/voice_timeline_tests_1 build/tests/voice_timeline_tests_0
	./build/tests/asset_tests
	./build/tests/gstx_format_tests
	./build/tests/swf_runtime_tests_0
	./build/tests/swf_runtime_tests_1
	./build/tests/swf_execution_tests
	./build/tests/ui_swf_format_tests
	./build/tests/ui_font_format_tests
	./build/tests/ui_catalog_tests
	./build/tests/ui_page_geometry_tests
	./build/tests/ui_triangles_tests
	./build/tests/charset_tests
	./build/tests/ui_geometry_tests
	./build/tests/heap_tests
	$(PYTHON) -m unittest discover -s src/tests/host/scripts -p 'test_*.py'

-include $(GAME_BACKEND_OBJS:.o=.d) $(RESIDENT_SUPPORT_OBJS:.o=.d) $(TEST_CAMPAIGN_OBJS:.o=.d) $(TEST_BACKEND_OBJS:.o=.d) $(TEST_SUPPORT_OBJS:.o=.d)

clean:
	rm -rf build/debug build/release build/tests

FORCE:

-include $(CORE_TEST_DRIVER_OBJS:.o=.d) $(BOOT_OBJS:.o=.d) $(CORE_OBJS:.o=.d) $(CORE_BACKEND_OBJS:.o=.d) $(CORE_C_OBJS:.o=.d) $(CAMPAIGN_OBJS:.o=.d)
