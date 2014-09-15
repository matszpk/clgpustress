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

gpustress: gpustress.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCDIRS) -o $@ $^ $(LIBDIRS) $(LIBS)

clean:
	rm -f *.o gpustress
