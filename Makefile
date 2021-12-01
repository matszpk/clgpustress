#####
# Makefile
# Mateusz Szpakowski
#####

.PHONY: all clean

OPENCLDIR = .

LDFLAGS = -Wall
CXXFLAGS = -Wall -std=gnu++11 -Os -fexpensive-optimizations
# CXXFLAGS = -Wall -g -std=gnu++11
CXX = g++
LIBDIRS = -L$(OPENCLDIR)/lib
INCDIRS = -I$(OPENCLDIR)/include -I`fltk-config --includedir`
LIBS = -lm -pthread -lpopt -lOpenCL
GUILIBS = `fltk-config --ldstaticflags` -lXpm

all: gpustress-cli gpustress-gui

gpustress-cli: gpustress-cli.o gpustress-core.o clkernels.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCDIRS) -o $@ $^ $(LIBDIRS) $(LIBS)

gpustress-gui: gpustress-gui.o gpustress-core.o clkernels.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCDIRS) -o $@ $^ $(LIBDIRS) $(LIBS) $(GUILIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCDIRS) -c -o $@ $<

gpustress-cli.o: gpustress-cli.cpp gpustress-core.h
gpustress-core.o: gpustress-core.cpp gpustress-core.h
gpustress-gui.o: gpustress-gui.cpp gpustress-core.h icon.xpm

clean:
	rm -f *.o gpustress-cli gpustress-gui
