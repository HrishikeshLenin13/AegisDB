CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude
LDFLAGS_SQLITE ?= -lsqlite3
HEADERS := $(shell find include -type f \( -name '*.hpp' -o -name '*.h' \))

.PHONY: all test bench clean

all: aegisql aegis_test bench_tpch

aegisql: tools/aegisql.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $<

aegis_test: tests/test_aegis.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $<

bench_tpch: bench/bench_tpch.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_SQLITE)

test: aegis_test
	./aegis_test
	$(CXX) $(CXXFLAGS) tests/test_bloom_clear.cpp -o /tmp/aegis-bloom-clear-test
	/tmp/aegis-bloom-clear-test
	$(CXX) $(CXXFLAGS) tests/test_bloom_capacity.cpp -o /tmp/aegis-bloom-capacity-test
	/tmp/aegis-bloom-capacity-test

bench: bench_tpch
	./bench_tpch 1

clean:
	rm -f aegisql aegis_test bench_tpch
	rm -rf build
