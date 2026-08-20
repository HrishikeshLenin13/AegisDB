# AegisDB architecture

This is the map of the engine: what each structure is for, and which paper/textbook it corresponds to. Implementation lives under `include/aegisdb/`.

## Storage

**Page (4 KiB).** Every page starts with a 48-byte header: magic, page id, **LSN**, type, slot count, free-space interval, sibling links. The LSN is the ARIES pageLSN used at redo time (`if pageLSN >= logLSN: skip`).

**Slotted page.** Slot directory grows forward from the header; tuples grow backward from the end of the page. Delete marks a slot empty and compacts when fragmentation exceeds a threshold. This is the standard heap-page layout (Ramakrishnan/Gehrke, Hellerstein/Stonebraker).

**Tuple.** `xmin`, `xmax`, previous-version RID, null bitmap, then INTEGER/DOUBLE/BOOLEAN/VARCHAR payloads. `xmin`/`xmax` are transaction ids for **snapshot isolation**; `prev_*` is the version chain.

**Buffer pool.** Clock (second-chance) over frames, pin counts, dirty bits. If a full sweep finds only pinned frames, fall back to unpinned LRU. Eviction of a dirty page **flushes the WAL up to pageLSN** first (WAL protocol). Replacement is steal: dirty pages may go to disk before commit. Commit is no-force: only the log is forced.

**Heap file.** Singly linked heap pages. Insert asks the **free-space map** (one byte per page, `free_bytes/16`) for a page that fits, otherwise appends.

## Indexing

**B+ tree.** Variable-length memcomparable keys (`Value::EncodeKey`). Leaves hold `(key, RID)` and are linked for range scans. Internal nodes hold separator keys and child page ids. Insert splits at midpoint and pushes a separator; delete borrows from a sibling or merges and pulls the separator. Bloom filter sits in front of point lookups.

**Extendible hash.** Directory of `2^global_depth` bucket pointers (single directory page, max depth 9). Overflow splits a bucket and doubles the directory when `local_depth == global_depth`; beyond max depth, buckets chain.

## Query engine

**Parser.** Recursive descent. Select-list expressions, inner joins, `BETWEEN` (desugared to `>= AND <=`), aggregates, `ORDER BY`, `LIMIT`.

**Optimizer (Selinger / System R).** Left-deep dynamic programming over the join graph. Per base table, compare sequential scan vs B+ index scan using sargable predicates (`col {=, <, >, <=, >=} literal`). Join method: hash vs sort-merge vs nested-loop from a simple I/O+CPU cost model. Cardinality: table `ANALYZE` stats (row count, per-column NDV, min/max); equality selectivity `1/NDV`; range from `[min,max]`; join `|R||S|/max(NDV)`.

`EXPLAIN` prints the physical tree with `cost=` and `rows=`.

**Executor.** Volcano: every operator is `Init / Next / Close`. Hash join builds an in-memory hash table on the left. Sort-merge sorts both inputs with **external merge sort** (spills runs to `/tmp` when the in-memory run exceeds a tuple cap, then k-way merges). Hash aggregate spills only in RAM for this version; the operator boundary is the same if a run file is added later.

## Transactions and recovery

**MVCC snapshot isolation.** `BEGIN` freezes `xmax = next_xid` and the set of in-flight xids. A tuple is visible to a reader if its creator committed before the snapshot and its deleter did not. Writers take exclusive **2PL** locks on RIDs (wait-die) so first-writer-wins.

**ARIES.** Log records are LSN-tagged with `prevLSN`, txn id, page/slot, before-image, after-image. Types: BEGIN, INSERT, UPDATE, DELETE, COMMIT, ABORT, CLR, CHECKPOINT, NEW_PAGE.

Restart:

1. **Analysis** from the last checkpoint: rebuild the transaction table (losers) and dirty-page table (`recLSN` per page).
2. **Redo** repeating history from `min(recLSN)`.
3. **Undo** losers in reverse LSN order, writing **CLRs** so a crash during undo does not undo twice.

Commit forces the log, not the data pages.

## What this is not

It is not a clustered B-tree engine, not a vectorized execution engine, and not SQLite. Those gaps are measured in `docs/BENCHMARKS.md`.
