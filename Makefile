CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -Wpedantic
LDFLAGS  += -pthread

SRCS := $(wildcard src/*.cpp)
OBJS := $(SRCS:src/%.cpp=build/%.o)
DEPS := $(OBJS:.o=.d)

build/prism: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: clean
-include $(DEPS)
