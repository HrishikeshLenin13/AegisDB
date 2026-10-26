# Bloom filter observability and composition

This guide covers implemented behavior. It does not change deployment or account configuration.

## Usage

```cpp
#include "aegisdb/index/bloom_filter.hpp"
#include <string>
#include <vector>
aegisdb::BloomFilter left(100), right(100);
std::vector<std::string> keys{"alpha", "beta"};
left.AddRange(keys.begin(), keys.end());
right.Add("gamma");
left.MergeFrom(right); // same bit capacity and hash count required
```

## Contracts and limits

- Empty, BitCount, HashCount, and FillRatio expose occupancy without exporting internal bit storage.
- EstimatedFalsePositiveRate is an insertion-count model, not a measured false-positive rate. Duplicate inserts and merges can make the estimate conservative.
- AddRange is equivalent to repeated Add calls and counts insertions, not distinct keys.
- MergeFrom checks compatibility and insertion-count overflow before changing bits. An uninitialized filter conservatively answers maybe; it is not an empty initialized filter.

## Source map

- [`include/aegisdb/index/bloom_filter.hpp`](../include/aegisdb/index/bloom_filter.hpp)
<!-- source: include/aegisdb/index/bloom_filter.hpp -->
- [`tests/test_bloom_round2.cpp`](../tests/test_bloom_round2.cpp)
<!-- source: tests/test_bloom_round2.cpp -->
- [`Makefile`](../Makefile)
<!-- source: Makefile -->

## Regression command

Run from the repository root:

```sh
make test && python3 -m unittest discover -s tests -v
```

