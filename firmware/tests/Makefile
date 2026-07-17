# katibOS unit tests — fast, hermetic. Run:  make   (from this dir)
CXX    := clang++
ROOT   := ..
INC    := -I. -I$(ROOT)/emulator/shims -I$(ROOT)/src
DEF    := -DARDUINO=10812 -DHOST_EMU
FLAGS  := -std=gnu++17 -g -O0 -Wall -Wno-format -Wno-varargs

# test runners
TESTS  := test_main.cpp test_textutil.cpp test_pagination.cpp test_editorwindow.cpp test_wordnav.cpp test_editorviewport.cpp test_ar_win.cpp test_massstorage_descriptor.cpp
# production units under test (dependency-free)
UNITS  := $(ROOT)/src/service/Tools/TextUtil.cpp $(ROOT)/src/keyboard/Locale/ar_win/ar_win.cpp

runtests: $(TESTS) $(UNITS) microtest.h $(ROOT)/src/service/Tools/TextUtil.h
	$(CXX) $(FLAGS) $(INC) $(DEF) $(TESTS) $(UNITS) -o runtests
	@echo "--- running ---"
	@./runtests

clean:
	rm -f runtests

.PHONY: runtests clean
