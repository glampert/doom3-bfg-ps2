# Doom 3 BFG PS2: explicit EE build foundation.
# The default ELF runs the validated headless core foundation, not campaign gameplay.
# The unmodified Quake II reference is docs/reference/quake2.Makefile.

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

PS2DEV ?= $(HOME)/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk
PCSX2 ?= /Applications/PCSX2.app/Contents/MacOS/PCSX2
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
ifeq ($(SCRIPT_PROBE),1)
	PS2_CXX_SRC = $(SCRIPT_BOOT_CXX_SRC)
	CORE_BOOT_CXX_SRC =
endif

EE_BIN = $(OUTPUT_DIR)/d3bfg_unstripped.elf
GAME_ELF = $(OUTPUT_DIR)/d3bfg.elf
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
PROJECT_INCS = -Isrc -Isrc/neo
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

PS2_CXXFLAGS = $(COMMON_CXXFLAGS) $(EE_CXX_WARNFLAGS) -DID_PS2_CORE=1 -DPS2_D3BFG_CORE_TESTS=$(CORE_BOOT)

# Legacy code keeps visible warnings and all language restrictions. Any
# necessary warning suppression belongs on the individual object, with a reason.
# Single-precision literals avoid software-double helpers on the EE.
NEO_CXXFLAGS = $(COMMON_CXXFLAGS) -Wall -Wextra -fsingle-precision-constant
CORE_CXXFLAGS = $(NEO_CXXFLAGS) -DID_PS2_CORE=1
FOUNDATION_HEADER_FLAGS = -UID_PS2_CORE -DPS2_D3BFG_FOUNDATION=1
CAMPAIGN_CXXFLAGS = $(NEO_CXXFLAGS)

# Observed in GCC 15's scalar idlib build: MSVC pragmas, unused public interface
# parameters, legacy copy/memset conventions, ineffective const casts, and old
# mutable string-literal interfaces. Apply only to explicitly listed upstream
# objects. Format, bounds, uninitialized and destructor diagnostics stay visible.
LEGACY_SHARED_WARNFLAGS = -Wno-unknown-pragmas -Wno-unused-parameter \
	-Wno-deprecated-copy -Wno-class-memaccess -Wno-ignored-qualifiers -Wno-write-strings

PS2_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(PS2_CXX_SRC:.cpp=.o))
CORE_OBJS = $(addprefix $(OUTPUT_DIR)/core/src/,$(CORE_CXX_SRC:.cpp=.o) $(CORE_FRAMEWORK_CXX_SRC:.cpp=.o))
CORE_BACKEND_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_BACKEND_CXX_SRC:.cpp=.o))
CORE_C_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_C_SRC:.c=.o))
CAMPAIGN_OBJS = $(addprefix $(OUTPUT_DIR)/campaign/src/,$(CAMPAIGN_CXX_SRC:.cpp=.o))
GAME_BACKEND_OBJS = $(addprefix $(OUTPUT_DIR)/game-backend/src/,$(GAME_BACKEND_CXX_SRC:.cpp=.o))
SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(SIZE_OPT_CXX_SRC:.cpp=.o))
GAME_SIZE_OPT_CXX_SRC = $(filter $(SIZE_OPT_CXX_SRC),$(GAME_BACKEND_CXX_SRC))
GAME_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/game-backend/src/,$(GAME_SIZE_OPT_CXX_SRC:.cpp=.o))
RESIDENT_SUPPORT_OBJS = $(addprefix $(OUTPUT_DIR)/resident-support/src/,$(RESIDENT_SUPPORT_CXX_SRC:.cpp=.o))
RESIDENT_SIZE_OPT_CXX_SRC = $(filter $(SIZE_OPT_CXX_SRC),$(RESIDENT_SUPPORT_CXX_SRC))
RESIDENT_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/resident-support/src/,$(RESIDENT_SIZE_OPT_CXX_SRC:.cpp=.o))
RESIDENT_LINK_SOURCES = $(CAMPAIGN_CXX_SRC) $(GAME_BACKEND_CXX_SRC) $(RESIDENT_SUPPORT_CXX_SRC) $(CORE_C_SRC)
RESIDENT_LINK_OBJS = $(CAMPAIGN_OBJS) $(GAME_BACKEND_OBJS) $(RESIDENT_SUPPORT_OBJS) $(CORE_C_OBJS)

ifeq ($(CORE_BOOT),1)
	BOOT_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_BOOT_CXX_SRC:.cpp=.o))
	LINK_OBJS = $(PS2_OBJS) $(BOOT_OBJS) $(CORE_OBJS) $(CORE_BACKEND_OBJS) $(CORE_C_OBJS)
	LINK_SOURCES = $(PS2_CXX_SRC) $(CORE_BOOT_CXX_SRC) $(CORE_CXX_SRC) $(CORE_FRAMEWORK_CXX_SRC) $(CORE_BACKEND_CXX_SRC) $(CORE_C_SRC)
	MILESTONE = headless-core
	REPORT_FLAGS = $(OUTPUT_DIR)/.backend-flags.json $(OUTPUT_DIR)/.core-flags.json $(OUTPUT_DIR)/.vendor-flags.json $(OUTPUT_DIR)/.link-flags.json
else
	BOOT_OBJS =
	LINK_OBJS = $(PS2_OBJS)
	LINK_SOURCES = $(PS2_CXX_SRC)
	MILESTONE = platform-probe
	REPORT_FLAGS = $(OUTPUT_DIR)/.backend-flags.json $(OUTPUT_DIR)/.link-flags.json
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
EE_LDFLAGS = -T$(EE_LINKFILE) -L$(PS2SDK)/ee/lib -Wl,-zmax-page-size=128 -Wl,-Map,$(OUTPUT_DIR)/d3bfg.map
EE_LIBS = -lkernel -lm
ifeq ($(CORE_BOOT),1)
	EE_LIBS += -lpatches
endif
# Isolated compiler fixture only: discard game-object/save/interpreter methods whose services
# are deliberately absent. Never use this gate as evidence of a resident game link.
ifeq ($(SCRIPT_PROBE),1)
	EE_LDFLAGS += -Wl,--gc-sections
	SCRIPT_CXXFLAGS = $(CAMPAIGN_CXXFLAGS) $(LEGACY_SHARED_WARNFLAGS) -ffunction-sections -fdata-sections
$(OUTPUT_DIR)/.script-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(SCRIPT_CXXFLAGS) --sources $(SCRIPT_CXX_SRC)
$(SCRIPT_OBJS): $(OUTPUT_DIR)/script/src/%.o: src/%.cpp $(OUTPUT_DIR)/.script-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(SCRIPT_CXXFLAGS) -c $< -o $@
$(OUTPUT_DIR)/src/tests/smoketests/script_boot.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE
endif

.PHONY: all release platform-probe headless-core compile-core compile-game link-game inventory compiledb tools run script-probe test-script test-common test-host smoke smoke-negative smoke-platform clean FORCE

all: inventory $(GAME_ELF) $(OUTPUT_DIR)/build-report.json
release: all
platform-probe: all
headless-core: all

inventory:
	@$(PYTHON) $(SCRIPTS)/audit_sources.py

# A stamp is rewritten only when its compiler identity, flags or source list
# changes. This fixes the reference's stale-object behavior after flag edits.
$(OUTPUT_DIR)/.backend-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) --cold-sources $(SIZE_OPT_CXX_SRC) --heap-vendor-includes $(HEAP_VENDOR_INCS) --engine-bridge-includes $(ENGINE_BRIDGE_INCS) --foundation-header-flags $(FOUNDATION_HEADER_FLAGS) --full-header-backend $(RENDER_STUB_CXX_SRC) ps2/system/common_campaign.cpp ps2/renderer/model_import.cpp ps2/system/sys_services.cpp ps2/audio/sound_backend.cpp ps2/system/common_foundation.cpp ps2/system/offline_session.cpp tests/smoketests/renderer_tests.cpp tests/smoketests/audio_tests.cpp tests/smoketests/offline_tests.cpp --sources $(PS2_CXX_SRC) $(CORE_BACKEND_CXX_SRC) $(BOOT_OBJS)

$(OUTPUT_DIR)/.core-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(CORE_CXXFLAGS) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --foundation-header-flags $(FOUNDATION_HEADER_FLAGS) --full-header-core neo/framework/Common.cpp neo/framework/PlayerProfile.cpp neo/sys/Snapshot.cpp neo/sys/LightweightCompression.cpp neo/sys/Snapshot_Jobs.cpp neo/sys/sys_localuser.cpp neo/sys/sys_signin.cpp --sources $(CORE_CXX_SRC) $(CORE_FRAMEWORK_CXX_SRC)

$(OUTPUT_DIR)/.campaign-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(CAMPAIGN_CXXFLAGS) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --sources $(CAMPAIGN_CXX_SRC)

VENDOR_CFLAGS = -D_EE -G0 $(OPTFLAGS) $(DBGFLAGS) -std=gnu11 -fno-strict-aliasing -Wall -Wextra $(COMMON_DEFS) $(SDK_INCS) $(PROJECT_INCS) -MD -MP

$(OUTPUT_DIR)/.vendor-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CC) $(VENDOR_CFLAGS) --sources $(CORE_C_SRC)

$(OUTPUT_DIR)/.link-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(OPTFLAGS) $(EE_LDFLAGS) $(EE_LIBS) --objects $(LINK_OBJS) --milestone $(MILESTONE)

$(OUTPUT_DIR)/.strip-flags.json: FORCE $(SCRIPTS)/build_metadata.py
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) STRIP_ELF=$(STRIP_ELF) STRIP=$(EE_STRIP)

$(PS2_OBJS) $(BOOT_OBJS) $(CORE_BACKEND_OBJS): $(OUTPUT_DIR)/src/%.o: src/%.cpp $(OUTPUT_DIR)/.backend-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(PS2_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(CXX_VENDOR_INCS_FOR) -c $< -o $@

$(CORE_C_OBJS): $(OUTPUT_DIR)/src/%.o: src/%.c $(OUTPUT_DIR)/.vendor-flags.json
	@mkdir -p $(dir $@)
	$(EE_CC) $(VENDOR_CFLAGS) -c $< -o $@

$(SIZE_OPT_OBJS) $(GAME_SIZE_OPT_OBJS) $(RESIDENT_SIZE_OPT_OBJS): CXX_OPTFLAGS_FOR = -Os

# Only heap.cpp imports dlmalloc's declaration header. Angle inclusion marks
# that vendor header as system without hiding warnings in heap.cpp or heap.h.
$(OUTPUT_DIR)/src/ps2/system/heap.o: CXX_VENDOR_INCS_FOR = $(HEAP_VENDOR_INCS)

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
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --cold-sources $(GAME_SIZE_OPT_CXX_SRC) --sources $(GAME_BACKEND_CXX_SRC)

$(GAME_BACKEND_OBJS): $(OUTPUT_DIR)/game-backend/src/%.o: src/%.cpp $(OUTPUT_DIR)/.game-backend-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(PS2_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE -c $< -o $@

$(OUTPUT_DIR)/.resident-support-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --heap-vendor-includes $(HEAP_VENDOR_INCS) --cold-sources $(RESIDENT_SIZE_OPT_CXX_SRC) --sources $(RESIDENT_SUPPORT_CXX_SRC)

$(RESIDENT_SUPPORT_OBJS): $(OUTPUT_DIR)/resident-support/src/%.o: src/%.cpp $(OUTPUT_DIR)/.resident-support-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(PS2_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE $(CXX_VENDOR_INCS_FOR) -c $< -o $@

$(OUTPUT_DIR)/resident-support/src/ps2/system/heap.o: CXX_VENDOR_INCS_FOR = $(HEAP_VENDOR_INCS)

# The foundation uses the real Common layout and retained session/profile value types.
$(OUTPUT_DIR)/core/src/neo/framework/Common.o: CORE_HEADER_FLAGS = $(FOUNDATION_HEADER_FLAGS)
$(addprefix $(OUTPUT_DIR)/core/src/,$(filter-out neo/framework/Common.o,$(CORE_FRAMEWORK_CXX_SRC:.cpp=.o))): CORE_HEADER_FLAGS =
$(addprefix $(OUTPUT_DIR)/core/src/,neo/framework/PlayerProfile.o neo/sys/Snapshot.o neo/sys/LightweightCompression.o neo/sys/Snapshot_Jobs.o neo/sys/sys_localuser.o neo/sys/sys_signin.o): CORE_HEADER_FLAGS = -UID_PS2_CORE
$(addprefix $(OUTPUT_DIR)/src/,$(RENDER_STUB_CXX_SRC:.cpp=.o) ps2/system/common_campaign.o ps2/renderer/model_import.o ps2/system/sys_services.o ps2/audio/sound_backend.o ps2/system/common_foundation.o ps2/system/offline_session.o): CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) $(FOUNDATION_HEADER_FLAGS)
$(OUTPUT_DIR)/src/tests/smoketests/renderer_tests.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) $(FOUNDATION_HEADER_FLAGS)
$(OUTPUT_DIR)/src/ps2/system/lifecycle.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS)
$(CORE_OBJS) $(CAMPAIGN_OBJS): NEO_WARNFLAGS_FOR = $(LEGACY_SHARED_WARNFLAGS)

$(EE_BIN): $(LINK_OBJS) $(OUTPUT_DIR)/.link-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(OPTFLAGS) -o $@ $(LINK_OBJS) $(EE_LDFLAGS) $(EE_LIBS)

$(GAME_ELF): $(EE_BIN) $(OUTPUT_DIR)/.strip-flags.json
ifeq ($(STRIP_ELF),1)
	$(EE_STRIP) --strip-all -o $@ $<
else
	cp -f $< $@
endif

$(OUTPUT_DIR)/.source-identity.json: FORCE $(LINK_OBJS) $(SCRIPTS)/build_metadata.py
	@$(PYTHON) $(SCRIPTS)/build_metadata.py identity --output $@ --sources $(addprefix src/,$(LINK_SOURCES)) --dependencies $(LINK_OBJS:.o=.d) --policies Makefile config/sources.mk config/source_inventory.json $(SCRIPTS)/build_metadata.py

$(OUTPUT_DIR)/build-report.json: $(EE_BIN) $(GAME_ELF) $(REPORT_FLAGS) $(OUTPUT_DIR)/.source-identity.json $(SCRIPTS)/build_metadata.py
	@$(PYTHON) $(SCRIPTS)/build_metadata.py report --elf $(EE_BIN) --runnable-elf $(GAME_ELF) --identity $(OUTPUT_DIR)/.source-identity.json --output $(OUTPUT_DIR)/build-report --compiler $(EE_CXX) --configuration $(BUILD) --milestone $(MILESTONE) --flags $(REPORT_FLAGS) --sources $(addprefix src/,$(LINK_SOURCES))

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
compile-game: inventory $(CAMPAIGN_OBJS) $(GAME_BACKEND_OBJS)
	@echo "Campaign compile gate: $(words $(CAMPAIGN_OBJS)) retained EE units; replacements remain listed in the inventory."

# Real retained-object link, with an explicit non-game entry. Failure reports never produce a runnable ELF.
link-game: inventory $(RESIDENT_LINK_OBJS)
	@$(PYTHON) $(SCRIPTS)/link_resident.py --compiler $(EE_CXX) --configuration $(BUILD) --output $(OUTPUT_DIR)/resident --linkfile $(EE_LINKFILE) --sdk-lib $(PS2SDK)/ee/lib --sources $(addprefix src/,$(RESIDENT_LINK_SOURCES)) --objects $(RESIDENT_LINK_OBJS) --flags $(OUTPUT_DIR)/.campaign-flags.json $(OUTPUT_DIR)/.game-backend-flags.json $(OUTPUT_DIR)/.resident-support-flags.json $(OUTPUT_DIR)/.vendor-flags.json

compiledb:
	@$(PYTHON) $(SCRIPTS)/gen_compile_commands.py --make $(MAKE) --build $(BUILD)

script-probe:
	$(MAKE) --no-print-directory BUILD=$(BUILD) SCRIPT_PROBE=1 all

test-common: all
	$(PYTHON) $(SCRIPTS)/run_common_tests.py --emulator $(PCSX2) --elf $(GAME_ELF)

test-script: script-probe
	$(PYTHON) $(SCRIPTS)/run_script_tests.py --elf build/$(BUILD)-script/d3bfg.elf

tools: inventory
	@echo "Build metadata and source audit tools are Python scripts under $(SCRIPTS)/."

run: all
	$(PCSX2) -batch -elf $(abspath $(GAME_ELF))

smoke: all
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(GAME_ELF) --scenario core

smoke-negative: all
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(GAME_ELF) --scenario core-missing-fixture

smoke-platform: all
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(GAME_ELF) --scenario platform

HOST_CXX ?= clang++

HOST_WARNFLAGS = -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wconversion -Wsign-conversion \
	-Wformat=2 -Wno-format-nonliteral -Wundef -Wpointer-arith -Wcast-align -Wwrite-strings \
	-Wredundant-decls -Wnull-dereference -Wnon-virtual-dtor -Woverloaded-virtual -Wvla

HOST_TEST_FLAGS = -std=c++20 -O1 -g -fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-strict-aliasing -fsized-deallocation -fsanitize=address,undefined -fno-omit-frame-pointer \
	$(HOST_WARNFLAGS) -Isrc -DID_HOST_TEST -DPS2_D3BFG_ASSERTS=1

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
HOST_JPEG_OBJS = $(addprefix build/tests/jpeg/,$(JPEG_TEST_CXX_SRC:.cpp=.o))
HOST_JPEG_PROJECT_SOURCES = src/tests/host/jpeg_decoder_tests.cpp src/ps2/ui/jpeg_decoder.cpp src/ps2/system/heap.cpp

build/tests/.jpeg-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_JPEG_FLAGS) $(HOST_TEST_FLAGS) --sources $(JPEG_TEST_CXX_SRC) $(HOST_JPEG_PROJECT_SOURCES)

$(HOST_JPEG_OBJS): build/tests/jpeg/%.o: src/%.cpp build/tests/.jpeg-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_JPEG_FLAGS) -MMD -MP -c $< -o $@

build/tests/jpeg_decoder_tests: $(HOST_JPEG_PROJECT_SOURCES) $(HOST_JPEG_OBJS) src/ps2/ui/jpeg_decoder.h src/ps2/system/heap.h src/ps2/system/log.h src/ps2/common.h src/neo/renderer/jpeg-6/jpeglib.h src/neo/idlib/sys/sys_alloc_tags.h build/tests/.jpeg-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -DID_PS2_CORE=1 -isystem src/neo $(HOST_JPEG_PROJECT_SOURCES) $(HOST_JPEG_OBJS) -o $@

HOST_FILESYSTEM_SOURCES = src/tests/host/filesystem_tests.cpp src/ps2/system/filesystem.cpp src/ps2/system/log.cpp

build/tests/.filesystem-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_FILESYSTEM_SOURCES)

build/tests/filesystem_tests: $(HOST_FILESYSTEM_SOURCES) src/ps2/system/filesystem.h src/ps2/system/log.h src/ps2/common.h build/tests/.filesystem-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) $(HOST_FILESYSTEM_SOURCES) -o $@

test-host: build/tests/heap_tests build/tests/common_tests_1 build/tests/common_tests_0 build/tests/jpeg_decoder_tests build/tests/filesystem_tests
	./build/tests/heap_tests
	$(PYTHON) -m unittest discover -s src/tests/host -p 'test_*.py'

-include $(HOST_JPEG_OBJS:.o=.d) $(GAME_BACKEND_OBJS:.o=.d) $(RESIDENT_SUPPORT_OBJS:.o=.d)

clean:
	rm -rf build/debug build/release build/tests

FORCE:

-include $(PS2_OBJS:.o=.d) $(BOOT_OBJS:.o=.d) $(CORE_OBJS:.o=.d) $(CORE_BACKEND_OBJS:.o=.d) $(CORE_C_OBJS:.o=.d) $(CAMPAIGN_OBJS:.o=.d)
