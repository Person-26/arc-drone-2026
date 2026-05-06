ifeq ($(origin CXX),default)
CXX=g++
endif

CXXFLAGS=-g -Wall $(shell pkg-config --cflags --libs opencv4)

all: circle

