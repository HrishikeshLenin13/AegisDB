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
	$(CXX) $(CXXFLAGS) tests/test_bloom_round2.cpp -o /tmp/aegis-bloom-round2-test
	/tmp/aegis-bloom-round2-test
	$(CXX) $(CXXFLAGS) tests/test_slotted_round3.cpp -o /tmp/aegis-slotted-round3
	/tmp/aegis-slotted-round3
	$(CXX) $(CXXFLAGS) tests/test_completion.cpp -o /tmp/aegis-completion-contracts
	/tmp/aegis-completion-contracts
	$(CXX) $(CXXFLAGS) tests/test_quality.cpp -o /tmp/aegis-quality-contracts
	/tmp/aegis-quality-contracts

bench: bench_tpch
	./bench_tpch 1

clean:
	rm -f aegisql aegis_test bench_tpch
	rm -rf build
