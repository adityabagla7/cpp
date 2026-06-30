CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O3 -march=native -Wall -Wextra -Iinclude
BUILD    := build

.PHONY: all demo test bench clean
all: demo test bench

$(BUILD):
	@mkdir -p $(BUILD)

demo: $(BUILD)
	$(CXX) $(CXXFLAGS) src/main.cpp -o $(BUILD)/demo

test: $(BUILD)
	$(CXX) $(CXXFLAGS) tests/test_orderbook.cpp -o $(BUILD)/tests
	./$(BUILD)/tests

bench: $(BUILD)
	$(CXX) $(CXXFLAGS) bench/benchmark.cpp -o $(BUILD)/bench
	./$(BUILD)/bench

clean:
	rm -rf $(BUILD)
