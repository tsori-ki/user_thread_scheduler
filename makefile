# Compiler and tools
CC = g++
CXX = g++
AR = ar
RANLIB = ranlib
RM = rm -f

# Project sources and objects
LIBSRC = uthreads.cpp
LIBOBJ = $(LIBSRC:.cpp=.o)

# Include paths and flags
INCS = -I.
CFLAGS = -Wall -std=c++11 -g $(INCS)
CXXFLAGS = -Wall -std=c++11 -g $(INCS)

# Static library
LIB = libuthreads.a
TARGETS = $(LIB)

# Tarball for submission
TAR = tar
TARFLAGS = -cvf
TARNAME = ex2.tar
TARSRCS = $(LIBSRC) Makefile README

.PHONY: all clean tar

all: $(TARGETS)

$(LIB): $(LIBOBJ)
	$(AR) rcs $@ $^
	$(RANLIB) $@

# Compile .cpp → .o
%.o: %.cpp uthreads.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(TARGETS) $(LIBOBJ) *~ core

tar: clean all
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
