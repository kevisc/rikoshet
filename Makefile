RACK_DIR ?= ../..

FLAGS += -std=c++17
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += plugin.json

include $(RACK_DIR)/plugin.mk
