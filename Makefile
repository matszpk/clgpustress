#####
# Makefile
# Mateusz Szpakowski
#####

.PHONY: all clean

AMDAPP = /opt/AMDAPP

LDFLAGS = -Wall
CXXFLAGS = -Wall -std=gnu++11 -O2 -fexpensive-optimizations
# CXXFLAGS = -Wall -g -std=gnu++11
CXX = g++
LIBDIRS = -L$(AMDAPP)/lib
INCDIRS = -I$(AMDAPP)/include
LIBS = -lm -pthread -lpopt -lOpenCL

all: gpustress

gpustress: gpustress.o clkernels.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCDIRS) -o $@ $^ $(LIBDIRS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCDIRS) -c -o $@ $^

clean:
	rm -f *.o gpustress
