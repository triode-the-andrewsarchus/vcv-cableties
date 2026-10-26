# Flags for the C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Source files
SOURCES += $(wildcard src/*.cpp)

# Assets and resources to include in distribution package
DISTRIBUTABLES += res

# Include the Rack SDK plugin Makefile
include $(RACK_DIR)/plugin.mk
