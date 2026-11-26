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
#   make test-script-image       # the host-cooked retail program equals an EE compile's, byte for byte
#   make test-ui                 # every UI screen: one retail session per standard (boot menu, console, fonts, movies, dialog, shell, menu screens, quit dialog) and the invalid-resource cases
#   make test-world              # the cooked graybox through id's render world: portal culling and captures, one production session per standard
#   make test-world-retail       # mars_city1 cooked from the retail .bproc: area 2 and its doorway, captures against references, the R2 probes, a frame-logged tour, load cycles, per standard
#   make run-world-retail        # debug: fly through cooked mars_city1 with the pad (ps2_world fly)
#   make BUILD=release stage-perf-world-retail  # a console package that plays mars_city1's camera tour into framelog.txt
#
# Tooling:
#   make tools                   # audit sources and build the host tools
#   make resource-unpack         # standalone C++20 host resource reader/extractor (CMake)
#   make asset-reexport          # C++20 host converter of retail textures to paletted GSTX (CMake)
#   make world-cook              # cook the authored graybox map into a D3WD world file
#   make world-cook-retail       # cook mars_city1's retail .bproc (RETAIL_DATA_ROOT) into a D3WD world file
#   make script-cook             # compile the authored headless script into the cooked program (D3SP) packages load
#   make script-cook-retail      # compile the retail game script (RETAIL_DATA_ROOT) into its cooked program
#                                # (WORLD_TOPOLOGY=strips cooks plain strips; pair it with another WORLD_COOK_OUTPUT)
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
WORLD_DATA_ROOT ?= src/tests/data/graybox_map
WORLD_COOK_JOB ?= $(WORLD_DATA_ROOT)/graybox_world.json
WORLD_COOK_OUTPUT ?= build/world
WORLD_RETAIL_JOB ?= config/world_mars_city1.json
PYTHON ?= python3
EE_TOOL_PREFIX ?= $(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-

include $(PS2SDK)/samples/Makefile.pref
include config/sources.mk
# The check variant links the cook's image writer to serialize its own compile.
ifeq ($(SCRIPT_CHECK),1)
	GAME_BACKEND_CXX_SRC += tools/script_cook/script_image_writer.cpp
endif

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
# .pdr (MIPS procedure descriptors) and .comment are never loaded; the unstripped ELF keeps them.
STRIP_FLAGS = --strip-all -R .pdr -R .comment
ifeq ($(filter $(STRIP_ELF),0 1),)
	$(error STRIP_ELF must be 0 or 1)
endif

SCRIPT_PROBE ?= 0
# SCRIPT_CHECK=1 builds a production variant (build/<config>-scriptcheck) that compiles the script
# at boot with id's compiler, as production did before the cook, and logs the image the cook's
# writer makes of that compile: test-script-image holds it to the host cook's, byte for byte.
SCRIPT_CHECK ?= 0
ifeq ($(filter $(SCRIPT_CHECK),0 1),)
	$(error SCRIPT_CHECK must be 0 or 1)
endif
ifeq ($(SCRIPT_CHECK),1)
  ifneq ($(SCRIPT_PROBE),0)
    $(error SCRIPT_CHECK builds production only)
  endif
  ifneq ($(filter-out all release,$(or $(MAKECMDGOALS),all)),)
    $(error SCRIPT_CHECK builds production only: make SCRIPT_CHECK=1 [release])
  endif
endif
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
OUTPUT_DIR = build/$(BUILD)$(if $(filter 1,$(SCRIPT_PROBE)),-script,)$(if $(filter 1,$(SCRIPT_CHECK)),-scriptcheck,)
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

# Release matches debug's -O2: -O3 added about 0.7 MB of loaded code for no measured gain.
# Code alignment padding is off in both configurations, so they keep one code layout (about
# 36 KB of release code): PCSX2 cannot show its effect on EE instruction fetch, so only a
# hardware measurement could justify it.
ALIGNFLAGS = -fno-align-functions -fno-align-jumps -fno-align-loops -fno-align-labels
# The subset of -ffast-math that keeps results exact on the EE FPU, which never traps and has
# no NaN: -fno-signed-zeros lets GCC emit the R5900's min.s/max.s for min/max selections
# (about 440 sites, each replacing a compare/branch/move triple). Reassociation and
# reciprocal math stay off so scalar references remain bit-exact against the VU0 paths.
MATHFLAGS = -fno-math-errno -fno-trapping-math -fno-signed-zeros
ifeq ($(BUILD),release)
	OPTFLAGS = -O2 $(ALIGNFLAGS) $(MATHFLAGS)
	DBGFLAGS =
	CONFIG_DEFS = -DPS2_D3BFG_DEBUG=0 -DPS2_D3BFG_ASSERTS=0 -DPS2_D3BFG_PROFILE=0 -DNDEBUG
else
	OPTFLAGS = -O2 $(ALIGNFLAGS) $(MATHFLAGS)
	DBGFLAGS = -gdwarf-2 -gz
	CONFIG_DEFS = -DPS2_D3BFG_DEBUG=1 -DPS2_D3BFG_ASSERTS=1 -DPS2_D3BFG_PROFILE=1 -D_DEBUG=1
endif

# Instrumentation: the frame log (ps2_framelog, with the timing counters only it reads) and the GPU
# sampler (what held a chain up, read while the EE waits for it, and ps2_world fill). On in every
# build until a Profile configuration carries them and Release drops them (IMPLEMENTATION_PLAN.md,
# "Profile build").
INSTRUMENT_DEFS = -DPS2_D3BFG_FRAME_LOG=1 -DPS2_D3BFG_GPU_SAMPLER=1

COMMON_DEFS = -DPS2_D3BFG $(CONFIG_DEFS) $(INSTRUMENT_DEFS)$(if $(filter 1,$(SCRIPT_CHECK)), -DID_SCRIPT_COMPILER -DID_SCRIPT_CHECK,)
LANGFLAGS = -std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-strict-aliasing
SDK_INCS = -isystem $(PS2SDK)/ee/include -isystem $(PS2SDK)/common/include
# build/vu holds the VU programs' generated headers (VU_HEADERS), which ps2/renderer/vu.h
# includes by bare name.
PROJECT_INCS = -Isrc -Isrc/neo -isystem src/external -Ibuild/vu
ENGINE_BRIDGE_INCS = -isystem src/neo
# One section per function and object lets the resident runnable link discard unreferenced code.
SECTIONFLAGS = -ffunction-sections -fdata-sections
COMMON_CXXFLAGS = -D_EE -G0 $(OPTFLAGS) $(SECTIONFLAGS) $(DBGFLAGS) $(LANGFLAGS) $(COMMON_DEFS) $(SDK_INCS) $(PROJECT_INCS) -MD -MP

# Apply the complete GCC diagnostics to every new project source. SDK headers
# are system headers; project headers remain checked.
EE_CXX_WARNFLAGS = -Wall -Wextra -Werror \
	-Wshadow -Wdouble-promotion -Wconversion -Wsign-conversion \
	-Wformat=2 -Wno-format-nonliteral -Wundef -Wpointer-arith \
	-Wcast-align -Wwrite-strings -Wredundant-decls -Wnull-dereference \
	-Wnon-virtual-dtor -Woverloaded-virtual -Wvla \
	-Wlogical-op -Wduplicated-cond -Wduplicated-branches

PS2_CXXFLAGS = $(COMMON_CXXFLAGS) $(EE_CXX_WARNFLAGS) -DID_PS2_CORE=1 -DPS2_D3BFG_TESTS=1

# Legacy code keeps all language restrictions and -Wall -Wextra, minus the
# reviewed categories in LEGACY_SHARED_WARNFLAGS, which apply to id's sources only.
# Single-precision literals avoid software-double helpers on the EE.
NEO_CXXFLAGS = $(COMMON_CXXFLAGS) -Wall -Wextra -fsingle-precision-constant
CORE_CXXFLAGS = $(NEO_CXXFLAGS) -DID_PS2_CORE=1 -DPS2_D3BFG_TESTS=1
FOUNDATION_HEADER_FLAGS = -UID_PS2_CORE -DPS2_D3BFG_FOUNDATION=1
CAMPAIGN_CXXFLAGS = $(NEO_CXXFLAGS) -DPS2_D3BFG_TESTS=0
GAME_RUNTIME_CXXFLAGS = $(filter-out -DPS2_D3BFG_TESTS=1,$(PS2_CXXFLAGS)) -DPS2_D3BFG_TESTS=0
# Production loads the cooked script program (script_cook); the test images keep id's compiler and
# its structures, because their fixtures compile scripts at run time. Every object of an image
# must agree, so each test image group passes this.
SCRIPT_COMPILER_FLAGS = -DID_SCRIPT_COMPILER
GAME_TEST_CXXFLAGS = $(NEO_CXXFLAGS) -DPS2_D3BFG_TESTS=1 $(SCRIPT_COMPILER_FLAGS)

# Applied only to id's engine objects (CORE/CAMPAIGN/game-test groups, all under
# src/neo); the backend keeps EE_CXX_WARNFLAGS and -Werror. Each category was
# reviewed in GCC 15's debug and release builds; real defects were fixed in the
# source instead, and the rest are listed by reason:
#   style of id's interfaces: MSVC pragmas, unused interface parameters, copy and
#     memset conventions, ineffective const casts, mutable string literals;
#   upstream idioms: member init order, partial enum switches, idClass event
#     callback casts, deliberate fallthroughs, intentionally hidden overloads,
#     FourCC constants, partial aggregate initializers, local function
#     declarations, mixed tab/space indentation;
#   NULL used as integer 0 or '\0' (pointer-arith, conversion-null);
#   dead upstream locals (unused, set-but-unused variables);
#   sign and range checks on non-negative values, enum/float arithmetic;
#   flow-analysis false positives from inlined idlib containers and math
#     (maybe-uninitialized, array-bounds, stringop-overflow).
LEGACY_SHARED_WARNFLAGS = -Wno-unknown-pragmas -Wno-unused-parameter \
	-Wno-deprecated-copy -Wno-class-memaccess -Wno-ignored-qualifiers -Wno-write-strings \
	-Wno-reorder -Wno-switch -Wno-cast-function-type -Wno-implicit-fallthrough \
	-Wno-overloaded-virtual -Wno-multichar -Wno-missing-field-initializers \
	-Wno-vexing-parse -Wno-misleading-indentation \
	-Wno-pointer-arith -Wno-conversion-null \
	-Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-sign-compare -Wno-type-limits -Wno-deprecated-enum-float-conversion \
	-Wno-maybe-uninitialized -Wno-array-bounds -Wno-stringop-overflow

CORE_TEST_DRIVER_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_TEST_DRIVER_CXX_SRC:.cpp=.o))
CORE_OBJS = $(addprefix $(OUTPUT_DIR)/core/src/,$(CORE_CXX_SRC:.cpp=.o) $(CORE_FRAMEWORK_CXX_SRC:.cpp=.o))
CORE_BACKEND_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_BACKEND_CXX_SRC:.cpp=.o))
CAMPAIGN_OBJS = $(addprefix $(OUTPUT_DIR)/campaign/src/,$(CAMPAIGN_CXX_SRC:.cpp=.o))
GAME_BACKEND_OBJS = $(addprefix $(OUTPUT_DIR)/game-backend/src/,$(GAME_BACKEND_CXX_SRC:.cpp=.o))
SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(SIZE_OPT_CXX_SRC:.cpp=.o))
ENGINE_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/core/src/,$(patsubst %.cpp,%.o,$(filter $(ENGINE_SIZE_OPT_CXX_SRC),$(CORE_CXX_SRC) $(CORE_FRAMEWORK_CXX_SRC)))) \
	$(addprefix $(OUTPUT_DIR)/campaign/src/,$(ENGINE_SIZE_OPT_CXX_SRC:.cpp=.o)) \
	$(addprefix $(OUTPUT_DIR)/test-campaign/src/,$(ENGINE_SIZE_OPT_CXX_SRC:.cpp=.o))
GAME_SIZE_OPT_CXX_SRC = $(filter $(SIZE_OPT_CXX_SRC),$(GAME_BACKEND_CXX_SRC))
GAME_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/game-backend/src/,$(GAME_SIZE_OPT_CXX_SRC:.cpp=.o))
RESIDENT_SUPPORT_OBJS = $(addprefix $(OUTPUT_DIR)/resident-support/src/,$(RESIDENT_SUPPORT_CXX_SRC:.cpp=.o) $(PS2_CXX_SRC:.cpp=.o))
RESIDENT_SIZE_OPT_CXX_SRC = $(filter $(SIZE_OPT_CXX_SRC),$(RESIDENT_SUPPORT_CXX_SRC) $(PS2_CXX_SRC))
RESIDENT_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/resident-support/src/,$(RESIDENT_SIZE_OPT_CXX_SRC:.cpp=.o))
RESIDENT_LINK_SOURCES = $(CAMPAIGN_CXX_SRC) $(GAME_BACKEND_CXX_SRC) $(RESIDENT_SUPPORT_CXX_SRC) $(PS2_CXX_SRC)
RESIDENT_LINK_OBJS = $(CAMPAIGN_OBJS) $(GAME_BACKEND_OBJS) $(RESIDENT_SUPPORT_OBJS)

TEST_CAMPAIGN_OBJS = $(addprefix $(OUTPUT_DIR)/test-campaign/src/,$(CAMPAIGN_CXX_SRC:.cpp=.o))
TEST_BACKEND_OBJS = $(addprefix $(OUTPUT_DIR)/test-backend/src/,$(GAME_BACKEND_CXX_SRC:.cpp=.o))
TEST_SUPPORT_OBJS = $(addprefix $(OUTPUT_DIR)/test-support/src/,$(RESIDENT_SUPPORT_CXX_SRC:.cpp=.o) $(GAME_TEST_CXX_SRC:.cpp=.o))
TEST_SUPPORT_SIZE_OPT_CXX_SRC = $(filter $(SIZE_OPT_CXX_SRC),$(RESIDENT_SUPPORT_CXX_SRC) $(GAME_TEST_CXX_SRC))
TEST_SIZE_OPT_OBJS = $(addprefix $(OUTPUT_DIR)/test-backend/src/,$(GAME_SIZE_OPT_CXX_SRC:.cpp=.o)) $(addprefix $(OUTPUT_DIR)/test-support/src/,$(TEST_SUPPORT_SIZE_OPT_CXX_SRC:.cpp=.o))
GAME_TEST_LINK_SOURCES = $(CAMPAIGN_CXX_SRC) $(GAME_BACKEND_CXX_SRC) $(RESIDENT_SUPPORT_CXX_SRC) $(GAME_TEST_CXX_SRC)
GAME_TEST_LINK_OBJS = $(TEST_CAMPAIGN_OBJS) $(TEST_BACKEND_OBJS) $(TEST_SUPPORT_OBJS)

# VU1 microprograms, written in VUSL: oxide-vusl -> dvp-as (the rule is with the host tools
# below). Each .vusl compiles into a .vsm, assembled into .vutext with <name>_CodeStart/_CodeEnd
# link symbols (VUSL_DECLARE_PROGRAM in ps2/renderer/vu.h), and a C++ header, <name>.vusl.h,
# with the program's memory layout: vu.h includes them and checks the host's structs and
# constants against them. Until vu::Init references the symbols, --gc-sections drops the code
# from the runnable and only the gate link carries it. No EE compiler flag reaches this
# toolchain, so the output is identical in both configurations: it is built once into build/vu/
# and linked into the production, game test and core test images. The VU rule writes a .d beside
# each object, so the link reports hash the .vusl and its includes like any compiled source.
VUSL_PATH  = src/ps2/renderer/vu1progs
VUSL_SRC   = $(addprefix ps2/renderer/vu1progs/,$(VUSL_FILES))
VU_OBJS    = $(addprefix build/vu/,$(VUSL_FILES:.vusl=.o))
VU_HEADERS = $(addprefix build/vu/,$(VUSL_FILES:.vusl=.vusl.h))
VUSL_INCS  = $(wildcard $(VUSL_PATH)/*.vush)
OXIDE_VCL_PATH   = src/external/oxide-vcl
OXIDE_VCL_BUILD  = build/tools/oxide-vcl
OXIDE_VUSL       = $(OXIDE_VCL_BUILD)/release/oxide-vusl
OXIDE_VUSL_FLAGS = --warnings-as-errors
RESIDENT_LINK_SOURCES += $(VUSL_SRC)
RESIDENT_LINK_OBJS += $(VU_OBJS)
GAME_TEST_LINK_SOURCES += $(VUSL_SRC)
GAME_TEST_LINK_OBJS += $(VU_OBJS)

# The IOP storage drivers a console boot starts after resetting the IOP, embedded in production's
# boot data (config/linkfile.ld) since the drive is out of reach until they run. iop_modules.S
# .incbin's the SDK's own IRX files; the drivers started later are loose files in the package's
# irx/ directory. The object is config-independent, like the VU programs: built once into
# build/iop/. Its .d names only the .S, so the IRX files are listed as prerequisites here.
IRX_PATH = $(PS2SDK)/iop/irx
IOP_BOOT_MODULES = iomanX fileXio ps2dev9 ps2atad ps2hdd ps2fs bdm bdmfs_fatfs usbd usbmass_bd
IOP_MODULES_SRC = ps2/system/iop_modules.S
IOP_MODULES_OBJ = build/iop/iop_modules.o
RESIDENT_LINK_SOURCES += $(IOP_MODULES_SRC)
RESIDENT_LINK_OBJS += $(IOP_MODULES_OBJ)

ifeq ($(CORE_BOOT),1)
	BOOT_OBJS = $(addprefix $(OUTPUT_DIR)/src/,$(CORE_BOOT_CXX_SRC:.cpp=.o))
	LINK_OBJS = $(CORE_TEST_DRIVER_OBJS) $(BOOT_OBJS) $(CORE_OBJS) $(CORE_BACKEND_OBJS) $(VU_OBJS)
	LINK_SOURCES = $(CORE_TEST_DRIVER_CXX_SRC) $(CORE_BOOT_CXX_SRC) $(CORE_CXX_SRC) $(CORE_FRAMEWORK_CXX_SRC) $(CORE_BACKEND_CXX_SRC) $(VUSL_SRC)
	MILESTONE = build-core
	REPORT_FLAGS = $(OUTPUT_DIR)/.backend-flags.json $(OUTPUT_DIR)/.core-flags.json $(CORE_TEST_DIR)/.link-flags.json
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
# The SDK's linkfile plus the boot data segment the heap reclaims (see its header); every image.
EE_LINKFILE ?= config/linkfile.ld
EE_LDFLAGS = -T$(EE_LINKFILE) -L$(PS2SDK)/ee/lib -Wl,-zmax-page-size=128 -Wl,-Map,$(CORE_TEST_DIR)/d3bfg_tests.map
# libeedebug carries the assertion builds' CPU exception handler (ps2/debug/crash_report.cpp), and
# libfileXio the debug log's PFS sync (ps2/debug/log_mirror.cpp), both in every core image.
EE_LIBS = -leedebug -lfileXio -lkernel -lm
RENDER_SDK_LIBS = draw graph packet2 dma
RESIDENT_SDK_LIBS = $(RENDER_SDK_LIBS) pad kbd fileXio eedebug kernel m patches
ifeq ($(CORE_BOOT),1)
	EE_LIBS = $(addprefix -l,$(RESIDENT_SDK_LIBS))
endif
# Isolated compiler fixture only: discard game-object/save/interpreter methods whose services
# are deliberately absent. Never use this gate as evidence of a resident game link.
ifeq ($(SCRIPT_PROBE),1)
	EE_LDFLAGS += -Wl,--gc-sections
	SCRIPT_CXXFLAGS = $(GAME_TEST_CXXFLAGS) $(LEGACY_SHARED_WARNFLAGS)
$(OUTPUT_DIR)/.script-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(SCRIPT_CXXFLAGS) --sources $(SCRIPT_CXX_SRC)
$(SCRIPT_OBJS): $(OUTPUT_DIR)/script/src/%.o: src/%.cpp $(OUTPUT_DIR)/.script-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(SCRIPT_CXXFLAGS) -c $< -o $@
$(OUTPUT_DIR)/src/tests/smoketests/script_boot.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE $(SCRIPT_COMPILER_FLAGS)
endif

.PHONY: texture-fixture all release stage-headless run-headless test-headless stage-headless-retail run-headless-retail test-headless-retail stage-console run-console test-ui test-world test-world-retail stage-world-retail run-world-retail stage-perf-world-retail run-perf-world-retail ui-inventory ui-cook resource-unpack asset-reexport world-cook world-cook-retail script-cook script-cook-retail test-script-image build-platform build-core build-game test-map test-game test-renderer run-renderer audit-sources compiledb tools run build-script test-script test-common test-host test-core test-platform core-test-image vu clean FORCE

release: all
build-platform: core-test-image
build-core: core-test-image
core-test-image: audit-sources $(TEST_ELF) $(CORE_TEST_DIR)/build-report.json

audit-sources:
	@$(PYTHON) $(SCRIPTS)/audit_sources.py

# A stamp is rewritten only when its compiler identity, flags or source list
# changes. This fixes the reference's stale-object behavior after flag edits.
$(OUTPUT_DIR)/.backend-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) --cold-sources $(SIZE_OPT_CXX_SRC) --engine-bridge-includes $(ENGINE_BRIDGE_INCS) --foundation-header-flags $(FOUNDATION_HEADER_FLAGS) --full-header-backend ps2/ui/assets.cpp ps2/input/usercmd_stub.cpp tests/smoketests/input_tests.cpp ps2/game/multiplayer_stub.cpp tests/smoketests/deferred_tests.cpp $(RENDER_BINDING_CXX_SRC) ps2/system/common_campaign.cpp ps2/renderer/model_import.cpp ps2/ui/draw.cpp ps2/renderer/image_backend.cpp ps2/audio/sound_backend.cpp ps2/system/common_foundation.cpp ps2/system/offline_session.cpp tests/smoketests/renderer_tests.cpp tests/smoketests/renderer_gs_tests.cpp tests/smoketests/audio_tests.cpp tests/smoketests/offline_tests.cpp --sources $(CORE_TEST_DRIVER_CXX_SRC) $(CORE_BACKEND_CXX_SRC) $(CORE_BOOT_CXX_SRC)

$(OUTPUT_DIR)/.core-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(CORE_CXXFLAGS) --cold-sources $(ENGINE_SIZE_OPT_CXX_SRC) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --foundation-header-flags $(FOUNDATION_HEADER_FLAGS) --full-header-core neo/framework/Common.cpp neo/framework/PlayerProfile.cpp neo/sys/sys_localuser.cpp neo/sys/sys_signin.cpp --sources $(CORE_CXX_SRC) $(CORE_FRAMEWORK_CXX_SRC)

$(OUTPUT_DIR)/.campaign-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(CAMPAIGN_CXXFLAGS) --cold-sources $(ENGINE_SIZE_OPT_CXX_SRC) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --sources $(CAMPAIGN_CXX_SRC)

$(CORE_TEST_DIR)/.link-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(OPTFLAGS) $(EE_LDFLAGS) $(EE_LIBS) --objects $(LINK_OBJS) --milestone $(MILESTONE)

$(CORE_TEST_DIR)/.strip-flags.json: FORCE $(SCRIPTS)/build_metadata.py
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) STRIP_ELF=$(STRIP_ELF) STRIP=$(EE_STRIP) $(STRIP_FLAGS)

$(CORE_TEST_DRIVER_OBJS) $(BOOT_OBJS) $(CORE_BACKEND_OBJS): $(OUTPUT_DIR)/src/%.o: src/%.cpp $(OUTPUT_DIR)/.backend-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(PS2_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(CXX_VENDOR_INCS_FOR) -c $< -o $@

$(SIZE_OPT_OBJS) $(GAME_SIZE_OPT_OBJS) $(RESIDENT_SIZE_OPT_OBJS) $(TEST_SIZE_OPT_OBJS) $(ENGINE_SIZE_OPT_OBJS): CXX_OPTFLAGS_FOR = -Os

$(OUTPUT_DIR)/src/ps2/ui/paletted_image.o $(OUTPUT_DIR)/src/ps2/renderer/gs.o $(OUTPUT_DIR)/src/ps2/renderer/dma.o $(OUTPUT_DIR)/src/ps2/renderer/vu.o $(OUTPUT_DIR)/src/ps2/renderer/world.o $(OUTPUT_DIR)/src/ps2/renderer/world_textures.o $(OUTPUT_DIR)/src/ps2/renderer/texture.o $(OUTPUT_DIR)/src/ps2/renderer/capture.o $(OUTPUT_DIR)/src/ps2/ui/draw.o $(OUTPUT_DIR)/src/ps2/renderer/image_backend.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS)

# Checked backend code includes upstream public contracts through this seam.
# Only neo headers become system headers; ps2 and test headers stay checked.
$(OUTPUT_DIR)/src/ps2/system/filesystem.o $(OUTPUT_DIR)/src/ps2/system/sys.o $(BOOT_OBJS): CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS)

# These checks inspect real campaign contracts; the session test exercises the offline replacement.
$(OUTPUT_DIR)/src/tests/smoketests/type_query_engine_tests.o $(OUTPUT_DIR)/src/tests/smoketests/audio_tests.o $(OUTPUT_DIR)/src/tests/smoketests/offline_tests.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE

$(CORE_OBJS): $(OUTPUT_DIR)/core/src/%.o: src/%.cpp $(OUTPUT_DIR)/.core-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(CORE_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(CORE_HEADER_FLAGS) $(NEO_WARNFLAGS_FOR) -c $< -o $@

$(CAMPAIGN_OBJS): $(OUTPUT_DIR)/campaign/src/%.o: src/%.cpp $(OUTPUT_DIR)/.campaign-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(CAMPAIGN_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(NEO_WARNFLAGS_FOR) -c $< -o $@

$(OUTPUT_DIR)/.game-backend-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(GAME_RUNTIME_CXXFLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --cold-sources $(GAME_SIZE_OPT_CXX_SRC) --sources $(GAME_BACKEND_CXX_SRC)

$(GAME_BACKEND_OBJS): $(OUTPUT_DIR)/game-backend/src/%.o: src/%.cpp $(OUTPUT_DIR)/.game-backend-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(GAME_RUNTIME_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE -c $< -o $@

$(OUTPUT_DIR)/.resident-support-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(GAME_RUNTIME_CXXFLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --cold-sources $(RESIDENT_SIZE_OPT_CXX_SRC) --sources $(RESIDENT_SUPPORT_CXX_SRC) $(PS2_CXX_SRC)

$(RESIDENT_SUPPORT_OBJS): $(OUTPUT_DIR)/resident-support/src/%.o: src/%.cpp $(OUTPUT_DIR)/.resident-support-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(GAME_RUNTIME_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE $(CXX_VENDOR_INCS_FOR) -c $< -o $@

$(OUTPUT_DIR)/.test-campaign-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(GAME_TEST_CXXFLAGS) --cold-sources $(ENGINE_SIZE_OPT_CXX_SRC) --source-warning-policy $(LEGACY_SHARED_WARNFLAGS) --sources $(CAMPAIGN_CXX_SRC)

$(TEST_CAMPAIGN_OBJS): $(OUTPUT_DIR)/test-campaign/src/%.o: src/%.cpp $(OUTPUT_DIR)/.test-campaign-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(GAME_TEST_CXXFLAGS) $(CXX_OPTFLAGS_FOR) $(LEGACY_SHARED_WARNFLAGS) -c $< -o $@

$(OUTPUT_DIR)/.test-backend-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) $(SCRIPT_COMPILER_FLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --cold-sources $(GAME_SIZE_OPT_CXX_SRC) --sources $(GAME_BACKEND_CXX_SRC)

$(TEST_BACKEND_OBJS): $(OUTPUT_DIR)/test-backend/src/%.o: src/%.cpp $(OUTPUT_DIR)/.test-backend-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(PS2_CXXFLAGS) $(SCRIPT_COMPILER_FLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE -c $< -o $@

$(OUTPUT_DIR)/.test-support-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile config/sources.mk
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) $(PS2_CXXFLAGS) $(SCRIPT_COMPILER_FLAGS) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE --cold-sources $(TEST_SUPPORT_SIZE_OPT_CXX_SRC) --sources $(RESIDENT_SUPPORT_CXX_SRC) $(GAME_TEST_CXX_SRC)

$(TEST_SUPPORT_OBJS): $(OUTPUT_DIR)/test-support/src/%.o: src/%.cpp $(OUTPUT_DIR)/.test-support-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(PS2_CXXFLAGS) $(SCRIPT_COMPILER_FLAGS) $(CXX_OPTFLAGS_FOR) $(ENGINE_BRIDGE_INCS) -UID_PS2_CORE $(CXX_VENDOR_INCS_FOR) -c $< -o $@

# The foundation uses the real Common layout and retained session/profile value types.
$(OUTPUT_DIR)/core/src/neo/framework/Common.o: CORE_HEADER_FLAGS = $(FOUNDATION_HEADER_FLAGS)
$(addprefix $(OUTPUT_DIR)/core/src/,$(filter-out neo/framework/Common.o,$(CORE_FRAMEWORK_CXX_SRC:.cpp=.o))): CORE_HEADER_FLAGS =
$(addprefix $(OUTPUT_DIR)/core/src/,neo/framework/PlayerProfile.o neo/sys/sys_localuser.o neo/sys/sys_signin.o): CORE_HEADER_FLAGS = -UID_PS2_CORE
$(addprefix $(OUTPUT_DIR)/src/,$(RENDER_BINDING_CXX_SRC:.cpp=.o) ps2/ui/assets.o ps2/ui/draw.o ps2/renderer/image_backend.o ps2/input/usercmd_stub.o ps2/game/multiplayer_stub.o ps2/system/common_campaign.o ps2/renderer/model_import.o ps2/audio/sound_backend.o ps2/system/common_foundation.o ps2/system/offline_session.o): CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) $(FOUNDATION_HEADER_FLAGS)
$(OUTPUT_DIR)/src/tests/smoketests/renderer_tests.o $(OUTPUT_DIR)/src/tests/smoketests/renderer_gs_tests.o $(OUTPUT_DIR)/src/tests/smoketests/deferred_tests.o $(OUTPUT_DIR)/src/tests/smoketests/input_tests.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS) $(FOUNDATION_HEADER_FLAGS)
$(OUTPUT_DIR)/src/ps2/system/lifecycle.o: CXX_VENDOR_INCS_FOR = $(ENGINE_BRIDGE_INCS)
$(CORE_OBJS) $(CAMPAIGN_OBJS): NEO_WARNFLAGS_FOR = $(LEGACY_SHARED_WARNFLAGS)

$(EE_BIN): $(LINK_OBJS) $(EE_LINKFILE) $(CORE_TEST_DIR)/.link-flags.json
	@mkdir -p $(dir $@)
	$(EE_CXX) $(OPTFLAGS) -o $@ $(LINK_OBJS) $(EE_LDFLAGS) $(EE_LIBS)

$(TEST_ELF): $(EE_BIN) $(CORE_TEST_DIR)/.strip-flags.json
ifeq ($(STRIP_ELF),1)
	$(EE_STRIP) $(STRIP_FLAGS) -o $@ $<
else
	cp -f $< $@
endif

$(CORE_TEST_DIR)/.source-identity.json: FORCE $(LINK_OBJS) $(SCRIPTS)/build_metadata.py
	@$(PYTHON) $(SCRIPTS)/build_metadata.py identity --output $@ --sources $(addprefix src/,$(LINK_SOURCES)) --dependencies $(LINK_OBJS:.o=.d) --policies Makefile config/sources.mk config/source_inventory.json $(EE_LINKFILE) $(SCRIPTS)/build_metadata.py

$(CORE_TEST_DIR)/build-report.json: $(EE_BIN) $(TEST_ELF) $(REPORT_FLAGS) $(CORE_TEST_DIR)/.source-identity.json $(SCRIPTS)/build_metadata.py
	@$(PYTHON) $(SCRIPTS)/build_metadata.py report --elf $(EE_BIN) --runnable-elf $(TEST_ELF) --identity $(CORE_TEST_DIR)/.source-identity.json --output $(CORE_TEST_DIR)/build-report --compiler $(EE_CXX) --configuration $(BUILD) --milestone $(MILESTONE) --flags $(REPORT_FLAGS) --sources $(addprefix src/,$(LINK_SOURCES))

# Production retained-object link with no test sources or fixture hooks. Failed gates remove stale ELFs.
$(OUTPUT_DIR)/.resident-sdk-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(EE_CXX) -T$(EE_LINKFILE) -L$(PS2SDK)/ee/lib $(addprefix -l,$(RESIDENT_SDK_LIBS))

all: audit-sources $(RESIDENT_LINK_OBJS) $(OUTPUT_DIR)/.resident-sdk-flags.json
	@$(PYTHON) $(SCRIPTS)/link_resident.py --compiler $(EE_CXX) --configuration $(BUILD) --output $(OUTPUT_DIR) --kind game --name d3bfg --linkfile $(EE_LINKFILE) --sdk-lib $(PS2SDK)/ee/lib --libraries $(RESIDENT_SDK_LIBS) --sources $(addprefix src/,$(RESIDENT_LINK_SOURCES)) --objects $(RESIDENT_LINK_OBJS) --flags $(OUTPUT_DIR)/.campaign-flags.json $(OUTPUT_DIR)/.game-backend-flags.json $(OUTPUT_DIR)/.resident-support-flags.json $(OUTPUT_DIR)/.resident-sdk-flags.json

# The same retained campaign is compiled with fixture hooks into a separate native executable.
build-game: audit-sources $(GAME_TEST_LINK_OBJS) $(OUTPUT_DIR)/.resident-sdk-flags.json
	@$(PYTHON) $(SCRIPTS)/link_resident.py --compiler $(EE_CXX) --configuration $(BUILD) --output $(GAME_TEST_DIR) --kind tests --name d3bfg_tests --linkfile $(EE_LINKFILE) --sdk-lib $(PS2SDK)/ee/lib --libraries $(RESIDENT_SDK_LIBS) --sources $(addprefix src/,$(GAME_TEST_LINK_SOURCES)) --objects $(GAME_TEST_LINK_OBJS) --flags $(OUTPUT_DIR)/.test-campaign-flags.json $(OUTPUT_DIR)/.test-backend-flags.json $(OUTPUT_DIR)/.test-support-flags.json $(OUTPUT_DIR)/.resident-sdk-flags.json

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

tools: audit-sources resource-unpack asset-reexport build/tools/script_cook
	@echo "Host tools: build/tools/resource_unpack, build/tools/asset_reexport, build/tools/script_cook; Python tools: $(SCRIPTS)/."

# Production loads a cooked script program (base/script/doom_main.d3sp), compiled on the host by
# id's own compiler against the game's event table (src/tools/script_cook/events.txt, which
# src/tools/scripts/script_events.py regenerates): the authored headless fixture's for the
# packages on authored data, the retail game's for the retail ones.
SCRIPT_COOK_OUTPUT ?= build/script
SCRIPT_EVENTS = src/tools/script_cook/events.txt

script-cook: build/tools/script_cook
	build/tools/script_cook --events $(SCRIPT_EVENTS) --source-dir src/tests/data/headless/base --output $(SCRIPT_COOK_OUTPUT)/headless/doom_main.d3sp

script-cook-retail: build/tools/script_cook
	build/tools/script_cook --events $(SCRIPT_EVENTS) --resources "$(RETAIL_DATA_ROOT)/_common.resources" --resources "$(RETAIL_DATA_ROOT)/_ordered.resources" --output $(SCRIPT_COOK_OUTPUT)/retail/doom_main.d3sp

# The host cook against the EE: production's old compile-at-boot path (SCRIPT_CHECK=1) compiles the
# retail script in PCSX2 and must make the identical image.
test-script-image: script-cook-retail
	$(MAKE) SCRIPT_CHECK=1 BUILD=$(BUILD) all
	$(PYTHON) $(SCRIPTS)/run_headless.py --build $(BUILD) --emulator $(PCSX2) --case retail --data-root "$(RETAIL_DATA_ROOT)" --script-check $(SCRIPT_COOK_OUTPUT)/retail/doom_main.d3sp

resource-unpack: build/tools/resource_unpack

asset-reexport: build/tools/asset_reexport

ui-inventory:
	$(PYTHON) $(SCRIPTS)/inventory_retail_ui.py --data-root "$(UI_DATA_ROOT)" --output "$(UI_COOK_OUTPUT)" --config "$(UI_RETAIL_CONFIG)"

ui-cook: build/tools/asset_reexport
	$(PYTHON) $(SCRIPTS)/inventory_retail_ui.py --data-root "$(UI_DATA_ROOT)" --output "$(UI_COOK_OUTPUT)" --config "$(UI_RETAIL_CONFIG)" --cook $(if $(UI_PREVIEWS),--previews "$(UI_PREVIEWS)")

world-cook: build/tools/asset_reexport
	build/tools/asset_reexport world --data "$(WORLD_DATA_ROOT)" --job "$(WORLD_COOK_JOB)" --output "$(WORLD_COOK_OUTPUT)"

world-cook-retail: build/tools/asset_reexport
	build/tools/asset_reexport world --data "$(RETAIL_DATA_ROOT)" --job "$(WORLD_RETAIL_JOB)" --output "$(WORLD_COOK_OUTPUT)" $(if $(WORLD_TOPOLOGY),--topology $(WORLD_TOPOLOGY))

stage-headless: all script-cook
	$(PYTHON) $(SCRIPTS)/stage_headless.py --build $(BUILD)

run: run-headless
run-headless: stage-headless
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/headless/d3bfg.elf

test-headless: all script-cook
	$(PYTHON) $(SCRIPTS)/run_headless.py --build $(BUILD) --emulator $(PCSX2)

stage-headless-retail: all script-cook-retail
	$(PYTHON) $(SCRIPTS)/stage_headless.py --build $(BUILD) --retail-data "$(RETAIL_DATA_ROOT)"

run-headless-retail: stage-headless-retail
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/headless-retail/d3bfg.elf

test-headless-retail: all script-cook-retail
	$(PYTHON) $(SCRIPTS)/run_headless.py --build $(BUILD) --emulator $(PCSX2) --case retail --data-root "$(RETAIL_DATA_ROOT)"

stage-console: all script-cook
	$(PYTHON) $(SCRIPTS)/stage_headless.py --build $(BUILD) --presentation $(CONSOLE_STANDARD) --catalog "$(UI_COOK_OUTPUT)"

run-console: stage-console
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/console/d3bfg.elf

# One session per standard boots into the retail menu and visits every retail screen; each
# invalid-resource case ends its process in a fatal.
test-ui: all script-cook
	$(PYTHON) $(SCRIPTS)/run_ui_session.py --build $(BUILD) --source "$(UI_COOK_OUTPUT)" --emulator $(PCSX2)
	$(PYTHON) $(SCRIPTS)/run_retail_ui.py --build $(BUILD) --source "$(UI_COOK_OUTPUT)" --emulator $(PCSX2)
	$(PYTHON) $(SCRIPTS)/run_retail_movies.py --build $(BUILD) --source "$(UI_COOK_OUTPUT)" --emulator $(PCSX2)

# One production session per standard loads the cooked graybox into id's render world and draws it
# through the portal flow from fixed cameras.
test-world: all world-cook script-cook
	$(PYTHON) $(SCRIPTS)/run_world.py --build $(BUILD) --catalog "$(UI_COOK_OUTPUT)" --world "$(WORLD_COOK_OUTPUT)/graybox.d3wd" --emulator $(PCSX2)

test-world-retail: all world-cook-retail script-cook
	$(PYTHON) $(SCRIPTS)/run_world_retail.py --build $(BUILD) --catalog "$(UI_COOK_OUTPUT)" --world "$(WORLD_COOK_OUTPUT)/mars_city1.d3wd" --data "$(RETAIL_DATA_ROOT)" --emulator $(PCSX2)

# Debug builds: fly through mars_city1 with the pad (ps2_world fly), from the player start.
stage-world-retail: all world-cook-retail script-cook
	$(PYTHON) $(SCRIPTS)/run_world_retail.py --build $(BUILD) --catalog "$(UI_COOK_OUTPUT)" --world "$(WORLD_COOK_OUTPUT)/mars_city1.d3wd" --data "$(RETAIL_DATA_ROOT)" --standard $(CONSOLE_STANDARD) --stage $(OUTPUT_DIR)/world-retail

run-world-retail: stage-world-retail
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/world-retail/d3bfg.elf

# The console perf run (BUILD=release for the timings that count): the package plays mars_city1's
# camera tour into framelog.txt by itself; copy it to a USB drive or the hard disk and boot its ELF.
# src/tools/scripts/frame_log.py summarises the log.
stage-perf-world-retail: all world-cook-retail script-cook
	$(PYTHON) $(SCRIPTS)/run_world_retail.py --build $(BUILD) --catalog "$(UI_COOK_OUTPUT)" --world "$(WORLD_COOK_OUTPUT)/mars_city1.d3wd" --data "$(RETAIL_DATA_ROOT)" --standard $(CONSOLE_STANDARD) --stage $(OUTPUT_DIR)/perf-world-retail --perf

run-perf-world-retail: stage-perf-world-retail
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/perf-world-retail/d3bfg.elf

.PHONY: stage-ui-fonts run-ui-fonts stage-ui-movies run-ui-movies stage-ui-menu run-ui-menu

stage-ui-fonts: all script-cook
	$(PYTHON) $(SCRIPTS)/stage_retail_ui.py --build $(BUILD) --standard $(CONSOLE_STANDARD) --source "$(UI_COOK_OUTPUT)"

run-ui-fonts: stage-ui-fonts
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/ui-fonts/d3bfg.elf

stage-ui-movies: all script-cook
	$(PYTHON) $(SCRIPTS)/stage_retail_ui.py --build $(BUILD) --standard $(CONSOLE_STANDARD) --source "$(UI_COOK_OUTPUT)" --movies

run-ui-movies: stage-ui-movies
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/ui-movies/d3bfg.elf

stage-ui-menu: all script-cook
	$(PYTHON) $(SCRIPTS)/stage_retail_ui.py --build $(BUILD) --standard $(CONSOLE_STANDARD) --source "$(UI_COOK_OUTPUT)" --movies --menu

run-ui-menu: stage-ui-menu
	$(PCSX2) -batch -elf $(abspath $(OUTPUT_DIR))/ui-menu/d3bfg.elf

test-core: build-core
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario core

test-platform: build-platform
	$(PYTHON) $(SCRIPTS)/run_pcsx2_test.py --emulator $(PCSX2) --elf $(TEST_ELF) --scenario platform

test-renderer: build-core world-cook texture-fixture
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

run-renderer: build-core world-cook texture-fixture
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

build/tools/script_cook: $(HOST_TOOLS_CMAKE)/CMakeCache.txt FORCE
	@MAKEFLAGS= cmake --build $(HOST_TOOLS_CMAKE) --parallel --target script_cook

# VU microprograms. oxide-vusl, the VUSL compiler they are built with, comes from oxide-vcl
# (https://github.com/glampert/oxide-vcl), a git submodule (src/external/oxide-vcl) built by cargo
# into build/tools/oxide-vcl/, so every VU build runs the pinned version. It compiles VUSL to VCL
# and hands that to oxide-vcl in the same process, in oxide mode with every fix and optimization.
# The up-to-date check is made here, on the crates' sources and manifests, so the rule runs again
# only when checking out a new commit changes them, and a make with nothing to do never starts
# cargo. Every VU program depends on the binary, so a new oxide-vusl rebuilds them all.
OXIDE_VCL_SRCS = $(OXIDE_VCL_PATH)/Cargo.toml $(OXIDE_VCL_PATH)/Cargo.lock \
                 $(shell find $(OXIDE_VCL_PATH)/crates -path '*/tests' -prune -o -type f -print 2>/dev/null)

$(OXIDE_VUSL): $(wildcard $(OXIDE_VCL_SRCS))
	@test -f $(OXIDE_VCL_PATH)/Cargo.toml || \
		{ echo "$(OXIDE_VCL_PATH) is empty - run 'git submodule update --init'"; exit 1; }
	@cargo build --quiet --release --locked --bin oxide-vusl \
		--manifest-path $(OXIDE_VCL_PATH)/Cargo.toml --target-dir $(OXIDE_VCL_BUILD)
	@touch $@

# oxide-vusl writes the listing and the header together, or neither when the program has an
# error; --warnings-as-errors fails the build on any warning, the preprocessor's or the
# compiler's. Its notes (a value kept in the work area for want of registers) are not warnings.
# In oxide mode oxide-vcl rewrites or rejects an immediate that does not fit its field (its 'imm'
# fix) rather than leaving dvp-as to truncate it. An 'operand out of range' warning from dvp-as on
# a branch longer than 1023 instructions is harmless: VU1 micro memory is exactly 2048
# instructions and the PC wraps, so the low 11 bits dvp-as keeps reach the same instruction. The
# .vsm is kept beside the object for reading, and the .d written last lets the link reports hash
# the program's inputs like a compiled source's. One recipe makes both targets: a pattern rule
# with two targets runs once for the pair.
vu: $(VU_OBJS) $(VU_HEADERS)

$(IOP_MODULES_OBJ): src/$(IOP_MODULES_SRC) $(addprefix $(IRX_PATH)/,$(IOP_BOOT_MODULES:=.irx))
	@mkdir -p $(dir $@)
	$(EE_CC) -D_EE -G0 -MD -MP -Wa,-I$(IRX_PATH) -c $< -o $@

build/vu/%.o build/vu/%.vusl.h: $(VUSL_PATH)/%.vusl $(VUSL_INCS) $(OXIDE_VUSL)
	@mkdir -p $(dir $@)
	$(OXIDE_VUSL) $(OXIDE_VUSL_FLAGS) --header=build/vu/$*.vusl.h -obuild/vu/$*.vsm $<
	dvp-as build/vu/$*.vsm -o build/vu/$*.o
	@printf '%s: %s\n' 'build/vu/$*.o' '$< $(VUSL_INCS)' > build/vu/$*.d

# vu.h includes the generated headers, so no EE object can be built before them. Order-only:
# once an object exists, its -MD dependency file names the headers it really includes, and that
# is what rebuilds it when one changes.
$(CORE_TEST_DRIVER_OBJS) $(BOOT_OBJS) $(CORE_BACKEND_OBJS) $(CORE_OBJS) $(CAMPAIGN_OBJS) $(GAME_BACKEND_OBJS) $(RESIDENT_SUPPORT_OBJS) $(TEST_CAMPAIGN_OBJS) $(TEST_BACKEND_OBJS) $(TEST_SUPPORT_OBJS) $(SCRIPT_OBJS): | $(VU_HEADERS)

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
build/tests/gstx_format_tests: src/tests/host/gstx_format_tests.cpp src/ps2/ui/gstx_format.cpp src/ps2/ui/gstx_format.h src/ps2/fnv.h $(ASSET_LIB_SRC) $(ASSET_LIB_HEADERS) build/tests/.asset-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -ffp-contract=off -Isrc/tools src/tests/host/gstx_format_tests.cpp src/ps2/ui/gstx_format.cpp $(ASSET_LIB_SRC) -o $@

# The world cook (proc reader, strips, D3WD) and the target's D3WD validator against the graybox
# fixture and synthetic grids.
build/tests/world_tests: src/tests/host/world_tests.cpp src/ps2/renderer/world_format.cpp src/ps2/renderer/world_format.h src/ps2/fnv.h $(ASSET_LIB_SRC) $(ASSET_LIB_HEADERS) build/tests/.asset-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -ffp-contract=off -Isrc/tools src/tests/host/world_tests.cpp src/ps2/renderer/world_format.cpp $(ASSET_LIB_SRC) -o $@

# The GS memory emulation, GSTX v2 cook and the target's GSTX v2 validator against each other.
build/tests/texture_format_tests: src/tests/host/texture_format_tests.cpp src/ps2/renderer/texture_format.cpp src/ps2/renderer/texture_format.h src/ps2/fnv.h $(ASSET_LIB_SRC) $(ASSET_LIB_HEADERS) build/tests/.asset-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -ffp-contract=off -Isrc/tools src/tests/host/texture_format_tests.cpp src/ps2/renderer/texture_format.cpp $(ASSET_LIB_SRC) -o $@

# The GSTX v2 files the renderer gate uploads (formulas shared with renderer_capture.py).
build/tests/lighting_tests: src/tests/host/lighting_tests.cpp $(ASSET_LIB_SRC) $(ASSET_LIB_HEADERS) build/tests/.asset-flags.json
	@mkdir -p $(@D)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -ffp-contract=off -Isrc/tools src/tests/host/lighting_tests.cpp $(ASSET_LIB_SRC) -o $@

build/tests/texture_fixture: src/tests/host/texture_fixture.cpp $(ASSET_LIB_SRC) $(ASSET_LIB_HEADERS) build/tests/.asset-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) -ffp-contract=off -Isrc/tools src/tests/host/texture_fixture.cpp $(ASSET_LIB_SRC) -o $@

texture-fixture: build/tests/texture_fixture
	@mkdir -p build/textures
	./build/tests/texture_fixture build/textures

HOST_HEAP_SOURCES = src/tests/smoketests/type_query_tests.cpp src/tests/smoketests/type_query_bridge.cpp src/tests/host/heap_tests.cpp src/tests/smoketests/heap_tests.cpp src/tests/smoketests/class_alloc_tests.cpp src/ps2/game/class_alloc.cpp src/ps2/system/heap.cpp src/ps2/system/heap_allocator.cpp src/ps2/system/heap_small.cpp src/ps2/system/heap_large.cpp src/ps2/system/log.cpp

build/tests/.heap-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_HEAP_SOURCES)

build/tests/heap_tests: $(HOST_HEAP_SOURCES) src/tests/smoketests/heap_tests.h src/tests/smoketests/class_alloc_tests.h src/tests/smoketests/type_query_tests.h src/ps2/type_query.h src/ps2/game/class_alloc.h src/ps2/system/heap.h src/ps2/system/heap_allocator.h src/ps2/system/log.h src/ps2/common.h build/tests/.heap-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(HOST_TEST_FLAGS) $(HOST_HEAP_SOURCES) -o $@

# The heap's allocator core over a private arena, with assertions (tracking, poison) and without.
HOST_HEAP_CORE_SOURCES = src/tests/host/heap_core_tests.cpp src/ps2/system/heap_allocator.cpp src/ps2/system/heap_small.cpp src/ps2/system/heap_large.cpp src/ps2/system/log.cpp
HOST_HEAP_CORE_HEADERS = src/ps2/system/heap_allocator.h src/ps2/system/log.h src/ps2/common.h

build/tests/.heap-core-flags.json: FORCE $(SCRIPTS)/build_metadata.py Makefile
	@$(PYTHON) $(SCRIPTS)/build_metadata.py stamp $@ $(HOST_CXX) $(HOST_TEST_FLAGS) --sources $(HOST_HEAP_CORE_SOURCES)

build/tests/heap_core_tests_%: $(HOST_HEAP_CORE_SOURCES) $(HOST_HEAP_CORE_HEADERS) build/tests/.heap-core-flags.json
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(filter-out -DPS2_D3BFG_ASSERTS=1,$(HOST_TEST_FLAGS)) -DPS2_D3BFG_ASSERTS=$* $(HOST_HEAP_CORE_SOURCES) -o $@

HOST_COMMON_SOURCES = src/tests/host/common_tests.cpp src/ps2/system/log.cpp src/ps2/system/heap.cpp src/ps2/system/heap_allocator.cpp src/ps2/system/heap_small.cpp src/ps2/system/heap_large.cpp src/ps2/game/class_alloc.cpp src/ps2/game/script_error.cpp
HOST_COMMON_HEADERS = src/ps2/game/script_error.h src/ps2/common.h src/ps2/system/log.h src/ps2/system/heap.h src/ps2/system/heap_allocator.h src/ps2/game/class_alloc.h

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

build/tests/ui_catalog_tests: src/tests/host/ui_catalog_tests.cpp src/ps2/ui/catalog.cpp src/ps2/ui/catalog.h src/ps2/ui/gstx_format.cpp src/ps2/ui/gstx_format.h src/ps2/fnv.h src/ps2/ui/font_format.h Makefile
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

build/tests/swf_runtime_tests_%: src/tests/host/swf_runtime_tests.cpp src/ps2/ui/swf_runtime.cpp src/ps2/ui/swf_runtime.h src/ps2/system/log.h src/ps2/fnv.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(filter-out -DPS2_D3BFG_ASSERTS=1,$(HOST_TEST_FLAGS)) -DPS2_D3BFG_ASSERTS=$* src/tests/host/swf_runtime_tests.cpp src/ps2/ui/swf_runtime.cpp -o $@

test-host: build/tools/asset_reexport build/tests/asset_tests build/tests/gstx_format_tests build/tests/texture_format_tests build/tests/world_tests build/tests/lighting_tests build/tests/swf_runtime_tests_0 build/tests/swf_runtime_tests_1 build/tests/swf_execution_tests build/tests/ui_swf_format_tests build/tests/ui_font_format_tests build/tests/ui_catalog_tests build/tests/ui_page_geometry_tests build/tests/resource_unpack build/tests/ui_triangles_tests build/tests/charset_tests build/tests/ui_geometry_tests build/tests/heap_tests build/tests/heap_core_tests_0 build/tests/heap_core_tests_1 build/tests/common_tests_1 build/tests/common_tests_0 build/tests/filesystem_tests build/tests/pcm_wave_tests build/tests/voice_timeline_tests_1 build/tests/voice_timeline_tests_0
	./build/tests/asset_tests
	./build/tests/gstx_format_tests
	./build/tests/texture_format_tests
	./build/tests/world_tests
	./build/tests/lighting_tests
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
	./build/tests/heap_core_tests_0
	./build/tests/heap_core_tests_1
	$(PYTHON) -m unittest discover -s src/tests/host/scripts -p 'test_*.py'

-include $(GAME_BACKEND_OBJS:.o=.d) $(RESIDENT_SUPPORT_OBJS:.o=.d) $(TEST_CAMPAIGN_OBJS:.o=.d) $(TEST_BACKEND_OBJS:.o=.d) $(TEST_SUPPORT_OBJS:.o=.d)

clean:
	rm -rf build/debug build/release build/tests build/vu build/iop

FORCE:

-include $(CORE_TEST_DRIVER_OBJS:.o=.d) $(BOOT_OBJS:.o=.d) $(CORE_OBJS:.o=.d) $(CORE_BACKEND_OBJS:.o=.d) $(CAMPAIGN_OBJS:.o=.d) $(SCRIPT_OBJS:.o=.d)
