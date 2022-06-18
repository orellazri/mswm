CXXFLAGS ?= -Wall -g
CXXFLAGS += -std=c++14
CXXFLAGS += `pkg-config --cflags x11 libglog`
LDFLAGS += `pkg-config --libs x11 libglog`

all: mswm

HEADERS = \
    window_manager.hpp
SOURCES = \
    window_manager.cpp \
    main.cpp
OBJECTS = $(SOURCES:.cpp=.o)

mswm: $(HEADERS) $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f mswm $(OBJECTS)
