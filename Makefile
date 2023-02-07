ifneq ($(wildcard local.mk),)
  include local.mk
endif

ifeq ($(wildcard $(SHDC)),)
  $(error SHDC must point to sokol-shdc (SHDC = $(SHDC)))
endif

include modes.mk

LDFLAGS_web-debug += --shell-file shell.html
LDFLAGS_web-release += --shell-file shell.html

PROGRAMS = chip8run

chip8run_SRC_DIR = src/
chip8run_SRCS = src/main.c src/sokol.c

include c.mk

ifneq ($(MODE),$(MODE:web-%=%))
  ifeq ($(wildcard $(EMSDK_SH)),)
    $(error EMSDK_SH must point to emsdk_env.sh (EMSDK_SH = $(EMSDK_SH)))
  endif
  shader_lang = glsl300es:glsl100
  $(chip8run_bin): shell.html
else
  shader_lang = glsl330
endif

shaders_h = $(chip8run_gen_dir)shaders.glsl.h

$(shaders_h): src/shaders.glsl | $(chip8run_gen_dir)
	$(SHDC) -i $< -o $@ --slang $(shader_lang)

$(chip8run_src/main.c_dep): $(shaders_h)
$(chip8run_src/main.c_dep): CPPFLAGS += -DSHADERS_H="../$(shaders_h)"

$(chip8run_src/main.c_obj): $(shaders_h)
$(chip8run_src/main.c_obj): CPPFLAGS += -DSHADERS_H="../$(shaders_h)"
