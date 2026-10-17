# Doom 3 BFG PS2: explicit EE build foundation.
# The default ELF is the production loop; native smoke executables are built separately.
# The unmodified Quake II reference is docs/reference/quake2.Makefile.

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

PS2DEV ?= $(HOME)/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk
PCSX2 ?= /Applications/PCSX2.app/Contents/MacOS/PCSX2
CONSOLE_STANDARD ?= ntsc
UI_DATA_ROOT ?= gamedata/d3_bfg
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
ifneq ($(filter platform-probe smoke-platform,$(MAKECMDGOALS)),)
	ifneq ($(filter headless-core smoke smoke-negative,$(MAKECMDGOALS)),)
		$(error platform-probe and headless-core must run separately)
	endif
	CORE_BOOT := 0
else ifneq ($(filter headless-core smoke smoke-negative,$(MAKECMDGOALS)),)
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
endif
ifneq ($(filter link-game,$(MAKECMDGOALS)),)
	ifeq ($(SCRIPT_PROBE),1)
		$(error link-game requires the ordinary campaign object tree, not SCRIPT_PROBE=1)
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
CORE_ARCHIVE = $(OUTPUT_DIR)/libd3bfg_core.a
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
CODEC_C_OBJS = $(addprefix $(OUTPUT_DIR)/codecs/src/,$(VENDOR_C_SRC:.c=.o))
CODEC_CXX_OBJS = $(addprefix $(OUTPUT_DIR)/codecs/src/,$(VENDOR_CXX_SRC:.cpp=.o))
RESIDENT_LINK_SOURCES = $(CAMPAIGN_CXX_SRC) $(GAME_BACKEND_CXX_SRC) $(RESIDENT_SUPPORT_CXX_SRC) $(PS2_CXX_SRC) $(CORE_C_SRC) $(VENDOR_C_SRC) $(VENDOR_CXX_SRC)
RESIDENT_LINK_OBJS = $(CAMPAIGN_OBJS) $(GAME_BACKEND_OBJS) $(RESIDENT_SUPPORT_OBJS) $(CORE_C_OBJS) $(CODEC_C_OBJS) $(CODEC_CXX_OBJS)

TEST_CAMPAIGN_OBJS = $(addprefix $(OUTPUT_DIR)/test-campaign/src/,$(CAMPAIGN_CXX_SRC:.cpp=.o))
TEST_BACKEND_OBJS = $(addprefix $(OUTPUT_DIR)/test-backend/src/,$(GAME_BACKEND_CXX_SRC:.cpp=.o))
TEST_SUPPORT_OBJS = $(addprefix $(OUTPUT_DIR)/test-support/src/,$(RESIDENT_SUPPORT_CXX_SRC:.cpp=.o) $(GAME_TEST_CXX_SRC:.cpp=.o))
TEST_SUPPORT_SIZE_OPT_CXX_SRC = $(filter $(SIZE_OPT_CXX_SRC),$(RESIDENT_SUPPORT_CXX_SRC) $(GAME_TEST_CXX_SRC))
TEST_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/test-backend/src/,$(GAME_SIZE_OPT_CXX_SRC:.cpp=.o)) $(addprefix $(OUTPUT_DIR)/test-support/src/,$(TEST_SUPPORT_SIZE_OPT_CXX_SRC:.cpp=.o))
GAME_TEST_LINK_SOURCES = $(CAMPAIGN_CXX_SRC) $(GAME_BACKEND_CXX_SRC) $(RESIDENT_SUPPORT_CXX_SRC) $(GAME_TEST_CXX_SRC) $(CORE_C_SRC) $(VENDOR_C_SRC) $(VENDOR_CXX_SRC)
GAME_TEST_LINK_OBJS = $(TEST_CAMPAIGN_OBJS) $(TEST_BACKEND_OBJS) $(TEST_SUPPORT_OBJS) $(CORE_C_OBJS) $(CODEC_C_OBJS) $(CODEC_CXX_OBJS)

ifeq ($(CORE_BOOT),1)
	BOOT_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_BOOT_CXX_SRC:.cpp=.o))
	LINK_OBJS = $(CORE_TEST_DRIVER_OBJS) $(BOOT_OBJS) $(CORE_OBJS) $(CORE_BACKEND_OBJS) $(CORE_C_OBJS)
	LINK_SOURCES = $(CORE_TEST_DRIVER_CXX_SRC) $(CORE_BOOT_CXX_SRC) $(CORE_CXX_SRC) $(CORE_FRAMEWORK_CXX_SRC) $(CORE_BACKEND_CXX_SRC) $(CORE_C_SRC)
	MILESTONE = headless-core
	REPORT_FLAGS = $(OUTPUT_DIR)/.backend-flags.json $(OUTPUT_DIR)/.core-flags.json $(OUTPUT_DIR)/.vendor-flags.json $(CORE_TEST_DIR)/.link-flags.json
	LINK_OBJS += $(CODEC_C_OBJS) $(CODEC_CXX_OBJS)
	LINK_SOURCES += $(VENDOR_C_SRC) $(VENDOR_CXX_SRC)
	REPORT_FLAGS += $(OUTPUT_DIR)/.codec-c-flags.json $(OUTPUT_DIR)/.codec-cxx-flags.json
else
	BOOT_OBJS =
	LINK_OBJS = $(CORE_TEST_DRIVER_OBJS)
	LINK_SOURCES = $(CORE_TEST_DRIVER_CXX_SRC)
	MILESTONE = platform-probe
	REPORT_FLAGS = $(OUTPUT_DIR)/.backend-flags.json $(CORE_TEST_DIR)/.link-flags.json
endif

ifeq ($(SCRIPT_PROBE),1)
	SCRIPT_OBJS = $(addprefix $(OUTPUT_DIR)/script/src/,$(SCRIPT_CXX_SRC:.cpp=.o))
	LINK_OBJS += $(SCRIPT_OBJS)
	LINK_SOURCES += $(SCRIPT_CXX_SRC)
	MILESTONE = script-compiler-probe
	REPORT_FLAGS += $(OUTPUT_DIR)/.script-flags.json
endif

# Do not garbage-collect registration objects to manufacture a smaller link.
# The loaded PT_LOAD sizes are recorded independently of ELF stripping.
EE_LINKFILE ?= $(PS2SDK)/ee/startup/linkfile
EE_LDFLAGS = -T$(EE_LINKFILE) -L$(PS2SDK)/ee/lib -Wl,-zmax-page-size=128 -Wl,-Map,$(CORE_TEST_DIR)/d3bfg_tests.map
EE_LIBS = -lkernel -lm
RENDER_SDK_LIBS = draw graph packet2 dma
RESIDENT_SDK_LIBS = $(RENDER_SDK_LIBS) kernel m patches
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

.PHONY: all release headless run-headless test-headless console run-console test-console ui run-ui test-ui ui-inventory ui-cook resource-unpack platform-probe headless-core compile-core compile-game headless-game test-map test-game test-renderer run-renderer link-game inventory compiledb tools run script-probe test-script test-common test-host smoke smoke-negative smoke-platform core-tests clean FORCE

all: link-game
release: all
platform-probe: core-tests
headless-core: core-tests
core-tests: inventory $(TEST_ELF) $(CORE_TEST_DIR)/build-report.json

inventory:
	@$(PYTHON) $(SCRIPTS)/audit_sources.py

# A stamp is rewritten only when its compiler identity, flags or source list
# changes. This fixes the reference's stale-object behavior after flag edits.
$(OUTPUT_DIR)/.backend-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) --cold-sources $(SIZE_OPT_CXX_SRC) --heap-vendor-includes $(HEAP_VENDOR_INCS) --engine-bridge-includes $(ENGINE_BRIDGE_INCS) --foundation-header-flags $(FOUNDATION_HEADER_FLAGS) --full-header-backend ps2/input/usercmd_stub.cpp tests/smoketests/input_tests.cpp ps2/game/multiplayer_stub.cpp ps2/system/save_metadata.cpp tests/smoketests/deferred_tests.cpp $(RENDER_STUB_CXX_SRC) ps2/system/common_campaign.cpp ps2/renderer/model_import.cpp ps2/renderer/ui_draw.cpp ps2/renderer/image_backend.cpp ps2/system/sys_services.cpp ps2/audio/sound_backend.cpp ps2/system/common_foundation.cpp ps2/system/offline_session.cpp tests/smoketests/renderer_tests.cpp tests/smoketests/renderer_gs_tests.cpp tests/smoketests/audio_tests.cpp tests/smoketests/offline_tests.cpp --sources $(CORE_TEST_DRIVER_CXX_SRC) $(CORE_BACKEND_CXX_SRC) $(CORE_BOOT_CXX_SRC)

$(OUTPUT_DIR)/.core-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(CORE_CXXFLAGS) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --foundation-header-flags $(FOUNDATION_HEADER_FLAGS) --full-header-core neo/framework/Common.cpp neo/framework/PlayerProfile.cpp neo/sys/Snapshot.cpp neo/sys/LightweightCompression.cpp neo/sys/Snapshot_Jobs.cpp neo/sys/sys_localuser.cpp neo/sys/sys_signin.cpp --sources $(CORE_CXX_SRC) $(CORE_FRAMEWORK_CXX_SRC)

$(OUTPUT_DIR)/.campaign-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(CAMPAIGN_CXXFLAGS) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --sources $(CAMPAIGN_CXX_SRC)

VENDOR_CFLAGS = -D_EE -G0 $(OPTFLAGS) $(DBGFLAGS) -std=gnu11 -fno-strict-aliasing -Wall -Wextra $(COMMON_DEFS) $(SDK_INCS) $(PROJECT_INCS) -MD -MP
CODEC_CFLAGS = $(VENDOR_CFLAGS) -DMY_ZCALLOC
CODEC_CXXFLAGS = $(NEO_CXXFLAGS) $(LEGACY_SHARED_WARNFLAGS)

$(OUTPUT_DIR)/.codec-c-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CC) $(CODEC_CFLAGS) --sources $(VENDOR_C_SRC)

$(OUTPUT_DIR)/.codec-cxx-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(CODEC_CXXFLAGS) --sources $(VENDOR_CXX_SRC)

$(CODEC_C_OBJS): $(OUTPUT_DIR)/codecs/src/%.o: src/%.c $(OUTPUT_DIR)/.codec-c-flags.json
	@mkdir -p $(dir $@)
	$(EE_CC) $(CODEC_CFLAGS) -c $< -o $@

$(CODEC_CXX_OBJS): $(OUTPUT_DIR)/codecs/src/%.o: src/%.cpp $(OUTPUT_DIR)/.codec-cxx-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(CODEC_CXXFLAGS) -c $< -o $@

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
$(OUTPUT_DIR)/src/ps2/renderer/ui_page_image.o $(OUTPUT_DIR)/src/ps2/renderer/cmd_buffer.o $(OUTPUT_DIR)/src/ps2/renderer/texture.o $(OUTPUT_DIR)/src/ps2/renderer/ui_draw.o $(OUTPUT_DIR)/src/ps2/renderer/image_backend.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS)

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
$(addprefix $(OUTPUT_DIR)/src/,$(RENDER_STUB_CXX_SRC:.cpp=.o) ps2/renderer/ui_draw.o ps2/renderer/image_backend.o ps2/input/usercmd_stub.o ps2/game/multiplayer_stub.o ps2/system/save_metadata.o ps2/system/common_campaign.o ps2/renderer/model_import.o ps2/system/sys_services.o ps2/audio/sound_backend.o ps2/system/common_foundation.o ps2/system/offline_session.o): CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) $(FOUNDATION_HEADER_FLAGS)
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

# The shared logger is used by both the SDK-only probe and the core adapters.
$(CORE_ARCHIVE): $(CORE_OBJS) $(CORE_BACKEND_OBJS) $(CORE_C_OBJS) $(OUTPUT_DIR)/src/ps2/system/log.o
	@rm -f $@.tmp
	$(EE_AR) rcs $@.tmp $^
	mv -f $@.tmp $@
	@$(EE_TOOL_PREFIX)size -A $@ > $(OUTPUT_DIR)/core-sections.txt

compile-core: inventory $(CORE_ARCHIVE)
	@echo "Core compile gate: $(words $(CORE_OBJS)) EE units; archive only, no engine boot claim."

# This is deliberately separate from compile-core: it uses the real campaign
# header boundary and reports the remaining M2b portability blockers.
compile-game: inventory $(CAMPAIGN_OBJS) $(GAME_BACKEND_OBJS) $(CODEC_C_OBJS) $(CODEC_CXX_OBJS)
	@echo "Campaign compile gate: $(words $(CAMPAIGN_OBJS)) retained EE units; replacements remain listed in the inventory."

# Production retained-object link with no test sources or fixture hooks. Failed gates remove stale ELFs.
$(OUTPUT_DIR)/.resident-sdk-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) -T$(EE_LINKFILE) -L$(PS2SDK)/ee/lib $(addprefix -l,$(RESIDENT_SDK_LIBS))

link-game: inventory $(RESIDENT_LINK_OBJS) $(OUTPUT_DIR)/.resident-sdk-flags.json
	@$(PYTHON) $(SCRIPTS)/link_resident.py --compiler $(EE_CXX) --configuration $(BUILD) --output $(OUTPUT_DIR) --kind game --name d3bfg --linkfile $(EE_LINKFILE) --sdk-lib $(PS2SDK)/ee/lib --libraries $(RESIDENT_SDK_LIBS) --sources $(addprefix src/,$(RESIDENT_LINK_SOURCES)) --objects $(RESIDENT_LINK_OBJS) --flags $(OUTPUT_DIR)/.campaign-flags.json $(OUTPUT_DIR)/.game-backend-flags.json $(OUTPUT_DIR)/.resident-support-flags.json $(OUTPUT_DIR)/.vendor-flags.json $(OUTPUT_DIR)/.codec-c-flags.json $(OUTPUT_DIR)/.codec-cxx-flags.json $(OUTPUT_DIR)/.resident-sdk-flags.json

# The same retained campaign is compiled with fixture hooks into a separate native executable.
headless-game: inventory $(GAME_TEST_LINK_OBJS) $(OUTPUT_DIR)/.resident-sdk-flags.json
	@$(PYTHON) $(SCRIPTS)/link_resident.py --compiler $(EE_CXX) --configuration $(BUILD) --output $(GAME_TEST_DIR) --kind tests --name d3bfg_tests --linkfile $(EE_LINKFILE) --sdk-lib $(PS2SDK)/ee/lib --libraries $(RESIDENT_SDK_LIBS) --sources $(addprefix src/,$(GAME_TEST_LINK_SOURCES)) --objects $(GAME_TEST_LINK_OBJS) --flags $(OUTPUT_DIR)/.test-campaign-flags.json $(OUTPUT_DIR)/.test-backend-flags.json $(OUTPUT_DIR)/.test-support-flags.json $(OUTPUT_DIR)/.vendor-flags.json $(OUTPUT_DIR)/.codec-c-flags.json $(OUTPUT_DIR)/.codec-cxx-flags.json $(OUTPUT_DIR)/.resident-sdk-flags.json

test-map: headless-game
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --elf $(GAME_TEST_ELF) --scenario game-map

test-game: headless-game
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

script-probe:
	$(MAKE) --no-print-directory BUILD=$(BUILD) SCRIPT_PROBE=1 core-tests

test-common: headless-core
	$(PYTHON) $(SCRIPTS)/run_common_tests.py --emulator $(PCSX2) --elf $(TEST_ELF)

test-script: script-probe
	$(PYTHON) $(SCRIPTS)/run_script_tests.py --elf build/$(BUILD)-script/tests/script/d3bfg_tests.elf

tools: inventory resource-unpack
	@echo "Host resource unpacker: build/tools/resource_unpack; Python tools: $(SCRIPTS)/."

resource-unpack: build/tools/resource_unpack

ui-inventory:
	$(PYTHON) $(SCRIPTS)/inventory_retail_ui.py --data-root "$(UI_DATA_ROOT)" --output "$(UI_COOK_OUTPUT)" --config "$(UI_RETAIL_CONFIG)"

ui-cook:
	$(PYTHON) $(SCRIPTS)/inventory_retail_ui.py --data-root "$(UI_DATA_ROOT)" --output "$(UI_COOK_OUTPUT)" --config "$(UI_RETAIL_CONFIG)" --cook

headless: all
	$(PYTHON) $(SCRIPTS)/stage_headless.py --build $(BUILD)

run: run-headless
run-headless: headless
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/headless/d3bfg.elf

test-headless: all
	$(PYTHON) $(SCRIPTS)/run_headless.py --build $(BUILD) --emulator $(PCSX2)

console: all
	$(PYTHON) $(SCRIPTS)/stage_headless.py --build $(BUILD) --presentation $(CONSOLE_STANDARD)

run-console: console
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/console/d3bfg.elf

test-console: all
	$(PYTHON) $(SCRIPTS)/run_console.py --build $(BUILD) --emulator $(PCSX2)

ui: all
	$(PYTHON) $(SCRIPTS)/stage_headless.py --build $(BUILD) --presentation $(CONSOLE_STANDARD) --ui

run-ui: ui
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/ui/d3bfg.elf

test-ui: all
	$(PYTHON) $(SCRIPTS)/run_ui.py --build $(BUILD) --emulator $(PCSX2)

smoke: headless-core
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario core

smoke-negative: headless-core
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario core-missing-fixture

smoke-platform: platform-probe
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario platform

test-renderer: headless-core
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ntsc
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-pal
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

run-renderer: headless-core
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario renderer-ntsc --visual-wait 60 --timeout 90

HOST_CXX ?= clang++

HOST_WARNFLAGS = -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wconversion -Wsign-conversion \
	-Wformat=2 -Wno-format-nonliteral -Wundef -Wpointer-arith -Wcast-align -Wwrite-strings \
	-Wredundant-decls -Wnull-dereference -Wnon-virtual-dtor -Woverloaded-virtual -Wvla

HOST_TEST_FLAGS = -std=c++20 -O1 -g -fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-strict-aliasing -fsized-deallocation -fsanitize=address,undefined -fno-omit-frame-pointer \
	$(HOST_WARNFLAGS) -Isrc -isystem src/external -DID_HOST_TEST -DPS2_D3BFG_ASSERTS=1

HOST_TOOL_FLAGS = -std=c++20 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics $(HOST_WARNFLAGS)
RESOURCE_UNPACK_SRC = src/tools/resource_unpack/main.cpp

build/tools/.resource-unpack-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TOOL_FLAGS) --sources $(RESOURCE_UNPACK_SRC)

build/tools/resource_unpack: $(RESOURCE_UNPACK_SRC) build/tools/.resource-unpack-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TOOL_FLAGS) $(RESOURCE_UNPACK_SRC) -o $@

build/tests/.resource-unpack-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(RESOURCE_UNPACK_SRC)

build/tests/resource_unpack: $(RESOURCE_UNPACK_SRC) build/tests/.resource-unpack-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) $(RESOURCE_UNPACK_SRC) -o $@

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

HOST_JPEG_FLAGS = $(filter-out -Werror,$(HOST_TEST_FLAGS)) -Wno-register -Wno-writable-strings -Isrc/neo
HOST_JPEG_OBJS = $(addprefix build/tests/jpeg/,$(patsubst %.cpp,%.o,$(filter-out external/jpeg-6/jmemnobs.cpp,$(JPEG_TEST_CXX_SRC))))
HOST_JPEG_PROJECT_SOURCES = src/tests/host/jpeg_decoder_tests.cpp src/ps2/ui/jpeg_decoder.cpp src/ps2/system/codec_memory.cpp src/ps2/system/heap.cpp

build/tests/.jpeg-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_JPEG_FLAGS) $(HOST_TEST_FLAGS) --sources $(JPEG_TEST_CXX_SRC) $(HOST_JPEG_PROJECT_SOURCES)

$(HOST_JPEG_OBJS): build/tests/jpeg/%.o: src/%.cpp build/tests/.jpeg-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_JPEG_FLAGS) -MMD -MP -c $< -o $@

build/tests/jpeg_decoder_tests: $(HOST_JPEG_PROJECT_SOURCES) $(HOST_JPEG_OBJS) src/ps2/ui/jpeg_decoder.h src/ps2/system/heap.h src/ps2/system/log.h src/ps2/common.h src/external/jpeg-6/jpeglib.h src/neo/idlib/sys/sys_alloc_tags.h build/tests/.jpeg-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -DID_PS2_CORE=1 -isystem src/neo $(HOST_JPEG_PROJECT_SOURCES) $(HOST_JPEG_OBJS) -o $@

HOST_CC ?= clang
HOST_ZLIB_FLAGS = -std=gnu11 -O1 -g -fno-strict-aliasing -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -DMY_ZCALLOC
HOST_ZLIB_OBJS = $(addprefix build/tests/zlib/,$(VENDOR_C_SRC:.c=.o))
HOST_CODEC_JPEG_OBJS = $(addprefix build/tests/jpeg/,$(VENDOR_CXX_SRC:.cpp=.o))
HOST_CODEC_SOURCES = src/tests/host/codec_tests.cpp src/tests/smoketests/codec_tests.cpp src/ps2/ui/jpeg_decoder.cpp src/ps2/system/codec_memory.cpp src/ps2/system/heap.cpp src/ps2/system/log.cpp

build/tests/.zlib-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CC) $(HOST_ZLIB_FLAGS) --sources $(VENDOR_C_SRC)

$(HOST_ZLIB_OBJS): build/tests/zlib/%.o: src/%.c build/tests/.zlib-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_ZLIB_FLAGS) -MMD -MP -c $< -o $@

build/tests/.codec-test-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_CODEC_SOURCES)

build/tests/codec_tests: $(HOST_CODEC_SOURCES) $(HOST_ZLIB_OBJS) $(HOST_CODEC_JPEG_OBJS) src/tests/smoketests/codec_fixtures.h src/tests/smoketests/codec_tests.h build/tests/.codec-test-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -isystem src/neo $(HOST_CODEC_SOURCES) $(HOST_ZLIB_OBJS) $(HOST_CODEC_JPEG_OBJS) -o $@

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

build/tests/ui_geometry_tests: src/tests/host/ui_geometry_tests.cpp src/ps2/renderer/ui_geometry.cpp src/ps2/renderer/ui_geometry.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_geometry_tests.cpp src/ps2/renderer/ui_geometry.cpp -o $@

build/tests/ui_page_format_tests: src/tests/host/ui_page_format_tests.cpp src/ps2/renderer/ui_page_format.cpp src/ps2/renderer/ui_page_format.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_page_format_tests.cpp src/ps2/renderer/ui_page_format.cpp -o $@

build/tests/ui_page_geometry_tests: src/tests/host/ui_page_geometry_tests.cpp src/ps2/renderer/ui_page_geometry.cpp src/ps2/renderer/ui_page_geometry.h src/ps2/renderer/ui_triangles.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_page_geometry_tests.cpp src/ps2/renderer/ui_page_geometry.cpp -o $@

HOST_UI_PAGE_SOURCES = src/tests/host/ui_page_image_tests.cpp src/ps2/renderer/ui_page_image.cpp src/ps2/renderer/ui_page_format.cpp src/ps2/renderer/ui_page_geometry.cpp src/ps2/renderer/texture_layout.cpp src/ps2/system/heap.cpp src/ps2/system/log.cpp
HOST_UI_PAGE_HEADERS = src/ps2/renderer/ui_page_image.h src/ps2/renderer/ui_page_format.h src/ps2/renderer/ui_page_geometry.h src/ps2/renderer/ui_triangles.h src/ps2/renderer/ui_geometry.h src/ps2/renderer/texture.h src/ps2/renderer/gs.h src/ps2/renderer/render_system.h src/ps2/renderer/cmd_buffer.h src/ps2/renderer/ui_draw.h src/ps2/system/heap.h src/ps2/system/log.h src/ps2/common.h src/tests/host/ui_page_stubs/tamtypes.h src/tests/host/ui_page_stubs/ps2/renderer/gif_writer.h src/tests/host/ui_page_stubs/idlib/precompiled.h

build/tests/.ui-page-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_UI_PAGE_SOURCES)

build/tests/ui_page_image_tests_%: $(HOST_UI_PAGE_SOURCES) $(HOST_UI_PAGE_HEADERS) build/tests/.ui-page-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) -Isrc/tests/host/ui_page_stubs $(filter-out -DPS2_D3BFG_ASSERTS=1,$(HOST_TEST_FLAGS)) -DPS2_D3BFG_ASSERTS=$* $(HOST_UI_PAGE_SOURCES) -o $@

build/tests/ui_triangles_tests: src/tests/host/ui_triangles_tests.cpp src/ps2/renderer/ui_triangles.cpp src/ps2/renderer/ui_triangles.h src/ps2/renderer/ui_geometry.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/ui_triangles_tests.cpp src/ps2/renderer/ui_triangles.cpp -o $@

build/tests/charset_tests: src/tests/host/charset_tests.cpp src/ps2/renderer/charset.cpp src/ps2/renderer/charset.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) src/tests/host/charset_tests.cpp src/ps2/renderer/charset.cpp -o $@

test-host: build/tests/ui_page_format_tests build/tests/ui_page_geometry_tests build/tests/ui_page_image_tests_1 build/tests/ui_page_image_tests_0 build/tests/resource_unpack build/tests/ui_triangles_tests build/tests/charset_tests build/tests/ui_geometry_tests build/tests/heap_tests build/tests/common_tests_1 build/tests/common_tests_0 build/tests/jpeg_decoder_tests build/tests/filesystem_tests build/tests/codec_tests build/tests/pcm_wave_tests build/tests/voice_timeline_tests_1 build/tests/voice_timeline_tests_0
	./build/tests/ui_page_format_tests
	./build/tests/ui_page_geometry_tests
	./build/tests/ui_page_image_tests_1
	./build/tests/ui_page_image_tests_0
	./build/tests/ui_triangles_tests
	./build/tests/charset_tests
	./build/tests/ui_geometry_tests
	./build/tests/heap_tests
	$(PYTHON) -m unittest discover -s src/tests/host -p 'test_*.py'

-include $(HOST_JPEG_OBJS:.o=.d) $(HOST_ZLIB_OBJS:.o=.d) $(GAME_BACKEND_OBJS:.o=.d) $(RESIDENT_SUPPORT_OBJS:.o=.d) $(TEST_CAMPAIGN_OBJS:.o=.d) $(TEST_BACKEND_OBJS:.o=.d) $(TEST_SUPPORT_OBJS:.o=.d) $(CODEC_C_OBJS:.o=.d) $(CODEC_CXX_OBJS:.o=.d)

clean:
	rm -rf build/debug build/release build/tests

FORCE:

-include $(CORE_TEST_DRIVER_OBJS:.o=.d) $(BOOT_OBJS:.o=.d) $(CORE_OBJS:.o=.d) $(CORE_BACKEND_OBJS:.o=.d) $(CORE_C_OBJS:.o=.d) $(CAMPAIGN_OBJS:.o=.d)
