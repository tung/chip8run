##############################
### Declarative C Makefile ###
##############################
#
# Interesting goals (p = program, modes = debug release ...):
#
# - all: build all programs
# - run: run first program in PROGRAMS list
# - run-p: run program p
# - clean: clean all programs in current mode
# - clean-p: clean program p in current mode
# - clean-all: clean everything
# - format: reformat *.c and *.h files using clang-format
# - format-check: check that *.c and *.h files are formatted using clang-format
#
# Example usage with Makefile:
#
# PROGRAMS = main other
# 
# main_SRC_DIR = src/
# main_HEADER_DIR = src/
# main_SRCS = src/main.c src/foo.c src/bar.c
# main_CFLAGS =
# main_CPPFLAGS = -DFOO -pthread
# main_LDFLAGS = -pthread
# main_LDLIBS = -ldl -lm -lpthread
#
# other_SRC_DIR = src/
# other_SRCS = src/other.c src/foo.c src/baz.c
#
# # shared: CFLAGS = ...
# # mode-specific: CFLAGS_debug = ...
# # program- and mode-specific: main_CFLAGS_debug = ...
# # program file-specific (C[PP]FLAGS only): main_src/main.c_CFLAGS = ...
# # program/file/mode (C[PP]FLAGS only): main_src/main.c_CFLAGS_debug = ...
#
# # Program file extension (lower-listed here overrides higher-listed):
# #
# # shared: EXT = ...
# # mode-specific: EXT_debug = ...
# # program-specific: main_EXT = ...
# # program- and mode-specific: main_EXT_debug = ...
#
# # Emscripten WebAssembly HTML5 support:
# #
# # shared: EMSDK_ENV_SH = /path/to/emsdk/emsdk_env.sh
# # mode-specific: EMSDK_ENV_SH_web-debug = /path/to/emsdk/emsdk_env.sh
# #
# # Set USE_EMSDK_FROM_ENV to build in an existing Emscripten SDK environment.
# # Set EXT to .html (default) for .html/.js/.wasm outputs or .js for just .js/.wasm.
#
# include c.mk
#
# ALL_CAPS variables below have reasonable defaults, but can be set as needed.

BUILD_DIR ?= build/
ALL_MODES ?= debug release

# check that BUILD_DIR is non-empty
$(if $(BUILD_DIR),,$(error BUILD_DIR must be non-empty))

# check that PROGRAMS is set
$(if $(PROGRAMS),,$(error PROGRAMS must list at least one program))

# check that each program's SRCS, SRC_DIR and HEADER_DIR have usable values
$(foreach p,$(PROGRAMS),\
  $(foreach v,SRCS SRC_DIR,\
    $(if $($(p)_$(v)),,$(error $(p)_$(v) must be set)))\
  $(if $($(p)_HEADER_DIR),,$(eval $(p)_HEADER_DIR = $($(p)_SRC_DIR)))\
  $(foreach d,SRC_DIR HEADER_DIR,\
    $(if $(filter-out $($(p)_$(d)),$($(p)_$(d):/=)),,\
      $(error $(p)_$(d) must end with a '/')))\
  $(foreach s,$($(p)_SRCS),\
    $(if $(filter-out $(s),$(s:$($(p)_SRC_DIR)%=%)),,\
      $(error $(p)_SRCS: $(s) must start with $($(p)_SRC_DIR)))))

FORMAT_SRCS ?= $(sort \
  $(foreach p,$(PROGRAMS),$($(p)_SRCS) $(wildcard $($(p)_HEADER_DIR)*.h)))

# CC ?= cc
# CPP ?= cc -E
# COMPILE.c ?= $(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c
# LINK.o ?= $(CC) $(LDFLAGS) $(TARGET_ARCH)
# RM ?= rm -f

CFLAGS ?= -Wall -Wextra
# CPPFLAGS ?= ...
# LDFLAGS ?= ...
# LDLIBS ?= ...
# TARGET_ARCH ?= ...

ifneq ($(filter debug,$(ALL_MODES)),)
  CFLAGS_debug ?= -g -Og -fno-omit-frame-pointer
  CPPFLAGS_debug ?= -D_DEBUG
  LDFLAGS_debug ?= -g -Og
  # LDLIBS_debug ?= ...
endif

ifneq ($(filter release,$(ALL_MODES)),)
  CFLAGS_release ?= -O3
  # CPPFLAGS_release ?= ...
  LDFLAGS_release ?= -O3
  # LDLIBS_release ?= ...
endif

NO_DEP_GOALS ?= clean clean-all format format-check

current_goals = $(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)
should_make_deps = $(filter-out $(NO_DEP_GOALS),$(current_goals))

ifeq ($(MODE),)
  override MODE = debug
endif

ifeq ($(filter $(MODE),$(ALL_MODES)),)
  $(error Invalid MODE = $(MODE); must be one of: $(ALL_MODES))
endif

CFLAGS += $(CFLAGS_$(MODE))
CPPFLAGS += $(CPPFLAGS_$(MODE))
LDFLAGS += $(LDFLAGS_$(MODE))
LDLIBS += $(LDLIBS_$(MODE))

buildstamp = $(MODE)$(shell v=$$(git describe --always --dirty 2>/dev/null) && \
  printf -- "-git-%s" "$${v}" || printf "")

ifdef EMSDK_ENV_SH_$(MODE)
  EMSDK_ENV_SH = $(EMSDK_ENV_SH_$(MODE))
endif

ifdef EMSDK_ENV_SH
  EXT ?= .html
endif

ifdef USE_EMSDK_FROM_ENV
  ifeq ($(origin CC),default)
    CC = emcc
  endif
  ifeq ($(origin CPP),default)
    CPP = emcc -E
  endif
endif

need_emsdk = $(and $(EMSDK_ENV_SH),$(if $(USE_EMSDK_FROM_ENV),,true))
use_emsdk = $(and $(EMSDK_ENV_SH),$(USE_EMSDK_FROM_ENV))
rerun_with_emsdk = cd "`dirname "$(subst ",\",$(EMSDK_ENV_SH))"`" && \
 . "$(subst ",\",$(EMSDK_ENV_SH))" && cd - && \
 test x$$EMSDK != x && $(MAKE) --no-print-directory USE_EMSDK_FROM_ENV=1 $(MAKECMDGOALS)

.PHONY: all
all:

.PHONY: build
build: $(firstword $(PROGRAMS:%=build-%))

.PHONY: run
run: $(firstword $(PROGRAMS:%=run-%))

.PHONY: clean
clean: $(foreach p,$(PROGRAMS),clean-$(p))

.PHONY: clean-all
clean-all:
	$(RM) -r $(BUILD_DIR)

.PHONY: format
format:
	clang-format -i $(FORMAT_SRCS)

.PHONY: format-check
format-check:
	clang-format --dry-run -Werror $(FORMAT_SRCS)

# create top-level build directory
$(BUILD_DIR):
	mkdir $@

define program_rules =

$(1)_mode_dir = $$(BUILD_DIR)$(1)-$(MODE)/
$(1)_deps_dir = $$($(1)_mode_dir)deps/
$(1)_objs_dir = $$($(1)_mode_dir)objs/
$(1)_gen_dir = $$($(1)_mode_dir)gen/
$(1)_out_dir = $$($(1)_mode_dir)out/
$(1)_bin = $$($(1)_out_dir)$(1)$$(or $$($(1)_EXT_$(MODE)),$$($(1)_EXT),$$(EXT_$(MODE)),$$(EXT))
$(1)_buildstamp_txt = $$($(1)_gen_dir)buildstamp.txt

all: $$($(1)_bin)

.PHONY: build-$(1)
build-$(1): $$($(1)_bin)

ifneq ($$(need_emsdk),)
  .PHONY: run-$(1)
  run-$(1):
	+( $$(rerun_with_emsdk) )
else ifneq ($$(use_emsdk),)
  .PHONY: run-$(1)
  run-$(1): $$($(1)_bin)
	@echo Serving http://127.0.0.1:6931/`basename $$($(1)_bin)`
	emrun --no_browser $$($(1)_bin)
else
  .PHONY: run-$(1)
  run-$(1): $$($(1)_bin)
	( cd $$(dir $$<) && ./$$(notdir $$<) )
endif

.PHONY: clean-$(1)
clean-$(1):
	$$(RM) -r $$($(1)_mode_dir)

$(1)_deps = $$($(1)_SRCS:$$($(1)_SRC_DIR)%.c=$$($(1)_deps_dir)%.d)
$(1)_objs = $$($(1)_SRCS:$$($(1)_SRC_DIR)%.c=$$($(1)_objs_dir)%.o)

# define file-specific _dep and _obj variables to make adding extra rules easier
$$(foreach s,$$($(1)_SRCS),$$(eval $(1)_$$(s)_dep = $$(s:$$($(1)_SRC_DIR)%.c=$$($(1)_deps_dir)%.d)))
$$(foreach s,$$($(1)_SRCS),$$(eval $(1)_$$(s)_obj = $$(s:$$($(1)_SRC_DIR)%.c=$$($(1)_objs_dir)%.o)))

# create program-specific build directories
$$($(1)_deps): | $$($(1)_deps_dir)
$$($(1)_objs): | $$($(1)_objs_dir)
$$($(1)_bin): | $$($(1)_out_dir)
$$($(1)_buildstamp_txt): | $$($(1)_gen_dir)
$$($(1)_deps_dir) $$($(1)_objs_dir) $$($(1)_gen_dir) $$($(1)_out_dir): | $$($(1)_mode_dir)
	mkdir $$@
$$($(1)_mode_dir): | $$(BUILD_DIR)
	mkdir $$@

# generate program-specific *.d files from *.c files
ifneq ($$(need_emsdk),)
  .PHONY: $$($(1)_deps)
  $$($(1)_deps):
	+( $$(rerun_with_emsdk) )
else
  $$($(1)_deps): $$($(1)_deps_dir)%.d: $$($(1)_SRC_DIR)%.c
	$$(CPP) $$(CPPFLAGS) -MM -MT $$(@:$$($(1)_deps_dir)%.d=$$($(1)_objs_dir)%.o) -MT $$@ -MG -MP -MF $$@ $$<
endif

# read in program-specific *.d files; generate them if they don't exist
ifneq ($$(should_make_deps),)
  ifeq ($$(need_emsdk),)
    -include $$($(1)_deps)
  endif
endif

# delete *.d files for program sources that no longer exist
$(1)_stale_deps = $$(filter-out $$($(1)_deps),$$(wildcard $$($(1)_deps_dir)*.d))
ifneq ($$($(1)_stale_deps),)
  $$(shell $$(RM) $$($(1)_stale_deps))
endif

# compile program-specific *.o files from *.c files
ifneq ($(need_emsdk),)
  .PHONY: $$($(1)_objs)
  $$($(1)_objs):
	+( $$(rerun_with_emsdk) )
else
  $$($(1)_objs): $$($(1)_objs_dir)%.o: $$($(1)_SRC_DIR)%.c
	$$(COMPILE.c) -o $$@ $$<
endif

# delete *.o files for program sources that no longer exist
$(1)_stale_objs = $$(filter-out $$($(1)_objs),$$(wildcard $$($(1)_objs_dir)*.o))
ifneq ($$($(1)_stale_objs),)
  $$(shell $$(RM) $$($(1)_stale_objs))
endif

# create program binary in its mode_dir
ifneq ($(need_emsdk),)
  .PHONY: $$($(1)_bin)
  $$($(1)_bin):
	+( $$(rerun_with_emsdk) )
else
  $$($(1)_bin): $$($(1)_objs)
	$$(LINK.o) -o $$@ $$(filter %.o,$$^) $$(LDLIBS)
endif

# set program-specific flags when compiling/linking
$$($(1)_bin): CFLAGS += $$($(1)_CFLAGS) $$($(1)_CFLAGS_$(MODE))
$$($(1)_bin): CPPFLAGS += $$($(1)_CPPFLAGS) $$($(1)_CPPFLAGS_$(MODE))
$$($(1)_bin): LDFLAGS += $$($(1)_LDFLAGS) $$($(1)_LDFLAGS_$(MODE))
$$($(1)_bin): LDLIBS += $$($(1)_LDLIBS) $$($(1)_LDLIBS_$(MODE))
$$($(1)_bin): TARGET_ARCH += $$($(1)_TARGET_ARCH) $$($(1)_TARGET_ARCH_$(MODE))

# set program-specific CPPFLAGS when generating *.d files
$$($(1)_deps): CPPFLAGS += $$($(1)_CPPFLAGS) $$($(1)_CPPFLAGS_$(MODE))

# set program file-specific CFLAGS and CPPFLAGS when compiling *.d and *.o files
$$(foreach v,CFLAGS CPPFLAGS,\
  $$(foreach d,$$($(1)_deps),\
    $$(eval $$(d): \
      $$(v) += $$($(1)_$$(d:$$($(1)_deps_dir)%.d=$$($(1)_SRC_DIR)%.c)_$$(v))))\
  $$(foreach o,$$($(1)_objs),\
    $$(eval $$(o): \
      $$(v) += $$($(1)_$$(o:$$($(1)_objs_dir)%.o=$$($(1)_SRC_DIR)%.c)_$$(v)))))

# create program-specific buildstamp.txt
$$($(1)_buildstamp_txt):
	printf "%s" "$$(buildstamp)" > $$($(1)_buildstamp_txt)

# delete buildstamp.txt if it's stale so it can be recreated
ifneq ($$(file <$$($(1)_buildstamp_txt)),$$(buildstamp))
  $$(shell $$(RM) $$($(1)_buildstamp_txt))
endif

# recreate buildstamp.txt for *.o file of first program_SRCS file
$$(firstword $$($(1)_objs)): $$($(1)_buildstamp_txt)

# define BUILDSTAMP when compiling first program_SRCS file
$$(firstword $$($(1)_objs)): CFLAGS += -DBUILDSTAMP="$$(buildstamp)"

endef
$(foreach p,$(PROGRAMS),$(eval $(call program_rules,$(p))))
