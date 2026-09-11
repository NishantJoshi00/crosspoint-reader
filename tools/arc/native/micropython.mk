ARC_NATIVE_DIR := $(USERMOD_DIR)
SRC_USERMOD_CXX += $(ARC_NATIVE_DIR)/ArcNative.cpp
SRC_USERMOD_CXX += $(ARC_NATIVE_DIR)/../../../lib/ArcPlayer/ArcGraphics.cpp
CXXFLAGS_USERMOD += -std=c++20
CFLAGS_USERMOD += -I$(ARC_NATIVE_DIR)/../../../lib/ArcPlayer
