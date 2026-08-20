CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude
LDFLAGS_SQLITE ?= -lsqlite3

.PHONY: all test bench clean

all: aegisql aegis_test bench_tpch

aegisql: tools/aegisql.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

aegis_test: tests/test_aegis.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

bench_tpch: bench/bench_tpch.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_SQLITE)

test: aegis_test
	./aegis_test

bench: bench_tpch
	./bench_tpch 1

clean:
	rm -f aegisql aegis_test bench_tpch
	rm -rf build
