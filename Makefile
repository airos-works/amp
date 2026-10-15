# Native Haiku build; needs the development tools bundled with Haiku plus
# curl_devel, sqlite_devel and taglib2_devel (pkgman install ...).
.DEFAULT_GOAL := all
CXX ?= g++
BUILD ?= build-haiku
CPPFLAGS += -Isrc -Isrc/player -Ivendor
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-multichar -Wno-unused-parameter -Wno-sign-compare
CORE = $(wildcard src/core/*.cpp)
PLAYER = $(wildcard src/player/*.cpp)
UI = $(wildcard src/ui/*.cpp) src/main.cpp
CORE_OBJ = $(CORE:%.cpp=$(BUILD)/%.o)
PLAYER_OBJ = $(PLAYER:%.cpp=$(BUILD)/%.o)
UI_OBJ = $(UI:%.cpp=$(BUILD)/%.o)
CORE_LIBS = -lcurl -lsqlite3 -ltag -lnetwork -lpthread
LIBS = -lbe -lmedia -ldevice -ltranslation -ltracker -llocalestub $(CORE_LIBS)
.PHONY: all core player package clean icon check mdtool scantool
# The Quick Look add-on (src/quicklook): the local decoder and the output built again as
# position independent code for air/OS's quick_look_server. Needs the Quick Look headers and
# libquicklook; an SDK without them leaves it out. QUICKLOOK=1 forces it, with
# QUICKLOOK_CPPFLAGS and QUICKLOOK_LDFLAGS pointing at them.
HAIKU_HEADERS ?= /boot/system/develop/headers
QUICKLOOK_CPPFLAGS ?=
QUICKLOOK_LDFLAGS ?=
QUICKLOOK ?= $(if $(wildcard $(HAIKU_HEADERS)/os/add-ons/quick_look/QuickLook.h),1,)
QUICKLOOK_SRC = src/player/LocalDecoder.cpp src/player/AudioOutput.cpp src/ui/Theme.cpp src/ui/Icons.cpp src/quicklook/QuickLookAddOn.cpp
QUICKLOOK_OBJ = $(QUICKLOOK_SRC:%.cpp=$(BUILD)/quicklook/%.o)
QUICKLOOK_TARGET = $(if $(QUICKLOOK),$(BUILD)/Amp_quicklook,)

all: $(BUILD)/Amp $(QUICKLOOK_TARGET)
core: $(CORE_OBJ)

.PHONY: quicklook
quicklook: $(BUILD)/Amp_quicklook

$(BUILD)/Amp_quicklook: $(QUICKLOOK_OBJ)
	$(CXX) -shared -Wl,-soname=Amp -o $@.new $(QUICKLOOK_OBJ) $(QUICKLOOK_LDFLAGS) -lquicklook -lbe -lmedia -ltranslation -ltag
	mv $@.new $@

$(BUILD)/quicklook/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(QUICKLOOK_CPPFLAGS) $(CXXFLAGS) -fPIC -MMD -MP -c $< -o $@

-include $(QUICKLOOK_OBJ:.o=.d)
IMAGES = $(wildcard resources/images/*.png)
$(BUILD)/Amp: $(CORE_OBJ) $(PLAYER_OBJ) $(UI_OBJ) resources/Amp.rdef resources/branding/amp-icon.hvif $(IMAGES)
	$(CXX) -o $@.new $(CORE_OBJ) $(PLAYER_OBJ) $(UI_OBJ) $(LIBS)
	rc -o $(BUILD)/Amp.rsrc resources/Amp.rdef
	xres -o $@.new $(BUILD)/Amp.rsrc
	mimeset -f $@.new
	mv $@.new $@
$(BUILD)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/amp_core_tests: $(CORE_OBJ) $(BUILD)/tests/CoreTests.o
	$(CXX) -o $@ $^ $(CORE_LIBS)
check: $(BUILD)/amp_core_tests
	$(BUILD)/amp_core_tests
# command-line NetMD tool for testing against a real recorder
mdtool: $(BUILD)/mdtool
$(BUILD)/mdtool: $(CORE_OBJ) $(BUILD)/src/player/NetMDUsb.o $(BUILD)/src/player/MiniDiscPcm.o $(BUILD)/tools/mdtool.o
	$(CXX) -o $@ $^ -lbe -lmedia -ldevice $(CORE_LIBS)
# times the library scanner on real folders, with a scratch library
scantool: $(BUILD)/scantool
$(BUILD)/scantool: $(CORE_OBJ) $(BUILD)/tools/scantool.o
	$(CXX) -o $@ $^ $(CORE_LIBS)
icon:
	python3 tools/make-icon.py resources/branding/amp-icon.hvif resources/branding/amp-icon-preview.png
	python3 tools/make-images.py
package: all
	bash tools/package-haiku.sh
clean:
	rm -rf $(BUILD)
-include $(CORE_OBJ:.o=.d) $(PLAYER_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(BUILD)/tests/CoreTests.d $(BUILD)/tools/mdtool.d $(BUILD)/tools/scantool.d
player: $(PLAYER_OBJ)
