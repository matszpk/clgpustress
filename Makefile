#####
# Makefile
# Mateusz Szpakowski
#####

.PHONY: all clean

OPENCLDIR = /home/mat/docs/dev/opencl/OpenCL1.1

LDFLAGS = -Wall
CXXFLAGS = -Wall -std=gnu++11 -O2 -fexpensive-optimizations
# CXXFLAGS = -Wall -g -std=gnu++11
CXX = g++
LIBDIRS = -L$(OPENCLDIR)/lib
INCDIRS = -I$(OPENCLDIR)/include
LIBS = -lm -pthread -lpopt -lOpenCL
GUILIBS = `fltk-config --ldstaticflags` 

all: gpustress gpustress-gui

gpustress: gpustress.o gpustress-core.o clkernels.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCDIRS) -o $@ $^ $(LIBDIRS) $(LIBS)

gpustress-gui: gpustress-gui.o gpustress-core.o clkernels.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCDIRS) -o $@ $^ $(LIBDIRS) $(LIBS) $(GUILIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCDIRS) -c -o $@ $<

gpustress.o: gpustress.cpp gpustress-core.h
gpustress-core.o: gpustress-core.cpp gpustress-core.h
gpustress-gui.o: gpustress-gui.cpp gpustress-core.h

clean:
	rm -f *.o gpustress gpustress-gui
