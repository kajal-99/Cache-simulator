CXX      = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra
TARGET   = cachesim
SRCS     = cachesim.cpp main.cpp

all: $(TARGET)

$(TARGET): $(SRCS) cachesim.hpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
