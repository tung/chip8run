ALL_MODES = debug release web-debug web-release

CPPFLAGS_debug = -DSOKOL_GLCORE33 -pthread
LDFLAGS_debug = -pthread
LDLIBS_debug = -ldl -lm -lpthread -lGL -lX11 -lXi -lXcursor

CPPFLAGS_release = -DSOKOL_GLCORE33 -pthread
LDFLAGS_release = -pthread
LDLIBS_release = -ldl -lm -lpthread -lGL -lX11 -lXi -lXcursor

EMSDK_ENV_SH_web-debug = $(EMSDK_SH)
CPPFLAGS_web-debug = -DSOKOL_GLES3
LDFLAGS_web-debug = -sMAX_WEBGL_VERSION=2

EMSDK_ENV_SH_web-release = $(EMSDK_SH)
CFLAGS_web-release = -O3 -flto
CPPFLAGS_web-release = -DSOKOL_GLES3
LDFLAGS_web-release = -O3 -flto -sMAX_WEBGL_VERSION=2
