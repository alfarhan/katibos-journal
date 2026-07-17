# MicroJournal rev_8 native (macOS/SDL2) emulator.
# Run from this directory:  make && ./microjournal-emu

CXX      := clang++
CXXSTD   := -std=gnu++17
CXXFLAGS := $(CXXSTD) -g -O0 -Wno-format -Wno-format-security -Wno-varargs -Wno-non-pod-varargs -Wno-narrowing -Wno-c++11-narrowing

ROOT   := ..
SRC    := $(ROOT)/src
LIB    := $(ROOT)/lib/ST7305_MonoTFT_Library/src
AJSON  := $(ROOT)/.pio/libdeps/rev_8/ArduinoJson/src

INCLUDES := -Ishims -I. -I$(SRC) -I$(LIB) -I$(AJSON) $(shell sdl2-config --cflags)

DEFINES  := -DARDUINO=10812 -DREV8 -DKEYPAD_68 \
            -DPIN_SCLK=12 -DPIN_MOSI=11 -DPIN_CS=10 -DPIN_DC=46 -DPIN_RST=3 \
            -DHOST_EMU -DBUFFER_SIZE=8000 -DVERSION_AVAILABLE=0 \
            -DFIRMWARE='"/firmware_rev_8.bin"'

LDFLAGS  := $(shell sdl2-config --libs) $(shell curl-config --libs)

# ---- host adaptation layer ----
HOST_CPP := host_main.cpp host_runtime.cpp host_log.cpp host_st7305.cpp \
            host_fs.cpp host_app.cpp host_stubs.cpp host_screen.cpp \
            host_real_sync.cpp host_sync_http.cpp

# ---- real firmware sources we compile against ----
FW_CPP := \
  $(SRC)/service/Sync/SyncCore.cpp \
  $(SRC)/display/display.cpp \
  $(SRC)/display/RLCD/display_RLCD.cpp \
  $(SRC)/display/RLCD/WordProcessor/WordProcessor.cpp \
  $(SRC)/display/RLCD/Menu/Menu.cpp \
  $(SRC)/display/RLCD/Menu/Home/Home.cpp \
  $(SRC)/display/RLCD/Menu/Layout/Layout.cpp \
  $(SRC)/display/RLCD/Menu/Clear/Clear.cpp \
  $(SRC)/display/RLCD/Menu/Wifi/Wifi.cpp \
  $(SRC)/display/RLCD/Menu/Sync/Sync.cpp \
  $(SRC)/display/RLCD/Menu/Storage/Storage.cpp \
  $(SRC)/display/RLCD/Menu/Rename/Rename.cpp \
  $(SRC)/display/RLCD/Menu/Settings/Settings.cpp \
  $(SRC)/display/RLCD/Menu/Help/Help.cpp \
  $(SRC)/display/RLCD/Menu/Stats/Stats.cpp \
  $(SRC)/display/RLCD/Menu/About/About.cpp \
  $(SRC)/display/RLCD/Menu/Ota/Ota.cpp \
  $(SRC)/display/RLCD/Menu/Timezone/Timezone.cpp \
  $(SRC)/display/RLCD/Menu/SyncProvider/SyncProvider.cpp \
  $(SRC)/display/RLCD/Menu/Preferences/Preferences.cpp \
  $(SRC)/display/RLCD/Menu/DeviceName/DeviceName.cpp \
  $(SRC)/display/RLCD/Menu/FactoryReset/FactoryReset.cpp \
  $(SRC)/service/Updater/Ota.cpp \
  $(SRC)/service/Clock/Clock.cpp \
  $(SRC)/display/RLCD/KeyboardScreen/KeyboardScreen.cpp \
  $(SRC)/service/WifiEntry/WifiEntry.cpp \
  $(SRC)/service/Send/Send.cpp \
  $(SRC)/display/RLCD/ErrorScreen/ErrorScreen.cpp \
  $(SRC)/display/RLCD/Update/Update.cpp \
  $(SRC)/keyboard/keyboard.cpp \
  $(SRC)/keyboard/Keypad/Keypad.cpp \
  $(SRC)/keyboard/Locale/locale.cpp \
  $(SRC)/keyboard/Locale/us/us.cpp \
  $(SRC)/keyboard/Locale/uk/uk.cpp \
  $(SRC)/keyboard/Locale/be/be.cpp \
  $(SRC)/keyboard/Locale/ca/ca.cpp \
  $(SRC)/keyboard/Locale/dv/dv.cpp \
  $(SRC)/keyboard/Locale/finnish/finnish.cpp \
  $(SRC)/keyboard/Locale/norweigian/norweigian.cpp \
  $(SRC)/keyboard/Locale/fr/fr.cpp \
  $(SRC)/keyboard/Locale/ge/ge.cpp \
  $(SRC)/keyboard/Locale/it/it.cpp \
  $(SRC)/keyboard/Locale/latin/latin.cpp \
  $(SRC)/keyboard/Locale/swedish/swedish.cpp \
  $(SRC)/keyboard/Locale/ar/ar.cpp \
  $(SRC)/keyboard/Locale/ar_win/ar_win.cpp \
  $(SRC)/app/Config/Config.cpp \
  $(SRC)/service/Editor/Editor.cpp \
  $(SRC)/service/Bidi/Bidi.cpp \
  $(SRC)/service/Tools/Tools.cpp \
  $(SRC)/service/Tools/TextUtil.cpp \
  $(SRC)/service/WordCounter/WordCounter.cpp \
  $(SRC)/service/Buffer/BufferService.cpp \
  $(LIB)/ST73XX_UI.cpp \
  $(LIB)/U8g2_for_ST73XX.cpp

# C source (u8g2 fonts)
FW_C := $(LIB)/u8g2_fonts.c

OBJDIR := build
OBJS := $(addprefix $(OBJDIR)/,$(notdir $(HOST_CPP:.cpp=.o))) \
        $(patsubst $(ROOT)/%.cpp,$(OBJDIR)/%.o,$(FW_CPP)) \
        $(patsubst $(ROOT)/%.c,$(OBJDIR)/%.o,$(FW_C))

TARGET := microjournal-emu

all: sdcard $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# host .cpp (this dir)
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

# firmware .cpp (under ..)
$(OBJDIR)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@

# firmware .c (u8g2 fonts) - MUST compile as C: the font arrays are defined as
# `const uint8_t foo[] = ...` which is external linkage in C but *internal*
# linkage in C++, so building as C++ drops every font symbol at link time.
$(OBJDIR)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	clang -std=gnu11 -g -O0 $(INCLUDES) $(DEFINES) -c $< -o $@

sdcard:
	@mkdir -p sdcard

clean:
	rm -rf $(OBJDIR) $(TARGET) frame.pgm

.PHONY: all clean sdcard
