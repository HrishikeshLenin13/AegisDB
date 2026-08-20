# AegisDB vs SQLite — TPC-H subset

Workload: synthetic TPC-H-shaped tables at scale **1** (500 customers, 5000 orders, 20000 lineitem rows). Dates are `YYYY-MM-DD` varchars; quantities/discounts follow TPC-H ranges. Each query number is the **best of 3** runs after load + `ANALYZE`.

| Metric | AegisDB | SQLite 3 | Aegis / SQLite |
|---|---:|---:|---:|
| Load | 885.931 ms | 19.4544 ms | 45.5388x |
| Q1 group-by aggregate | 24.1399 ms | 9.57125 ms | 2.52213x |
| Q6 filtered sum | 3.73396 ms | 1.58854 ms | 2.35056x |
| Q3 3-way join + agg | 9.62683 ms | 9.06133 ms | 1.06241x |
| Crash recovery (reopen + count) | 25.0535 ms | 1.70108 ms | 14.7279x |

## Plans (AegisDB)

### Q6

```
HashAggregate  cost=12.0425 rows=1
  Filter  cost=12.0225 rows=1
    IndexScan  cost=12.02 rows=1 table=lineitem index=li_ship
```

### Q3

```
Limit  cost=361.239 rows=10
  ExternalSort  cost=361.239 rows=20
    HashAggregate  cost=360.375 rows=20
      HashJoin  cost=356.375 rows=200 join=(0=0)
        HashJoin  cost=334.875 rows=100 join=(1=0)
          Filter  cost=301.125 rows=50
            SeqScan  cost=301 rows=50 table=orders
          Filter  cost=31.25 rows=100
            SeqScan  cost=31 rows=100 table=customer
        Filter  cost=16.5 rows=200
          IndexScan  cost=16 rows=200 table=lineitem index=li_ship
```

## Where and why we lose

SQLite wins on this workload, and it should. The gap is not a mystery — it is the
difference between a teaching/research engine with explicit layers and a 20-year
production bytecode VM sitting on a clustered B-tree.

Query latency is the honest headline: **Q3 is within ~6% of SQLite**; Q1/Q6 are
about 2.3–2.5×. Load and recovery are where the textbook design tax shows up.

1. **Load path (the 45×).** The bench inserts Aegis rows by parsing SQL
   `INSERT ... VALUES (...), (...)` batches. SQLite uses prepared statements and
   bind. On top of that, every Aegis insert writes a physiological WAL record
   (before/after image) and maintains secondary indexes. Replacing the SQL
   insert path with a bulk-load API would close most of this gap without
   touching the query engine.
2. **Storage model.** SQLite tables *are* B-trees (rowid clustered). AegisDB is
   heap + secondary B+ tree, so a filtered scan is heap I/O plus index lookups
   rather than a single tree walk. The slotted-page heap and page-level FSM are
   the right textbook design; they are also extra indirection.
3. **Buffer pool vs OS cache.** We copy 4 KiB pages through a userspace clock/LRU
   pool (steal/no-force). SQLite lets the kernel page cache do most of the work
   and uses a small pager. On a dataset that fits in RAM, the extra memcpy and
   pin/unpin traffic show up immediately.
4. **Volcano iterator vs VDBE.** Row-at-a-time `Init/Next/Close` with
   `std::vector<Value>` tuples (heap-allocated varchars) is honest and inspectable.
   SQLite's VDBE is a tight register machine with a single allocation arena.
   That shows up on Q1/Q6 (CPU-bound aggregates).
5. **Cardinality + join order.** Selinger DP with NDV/min/max is correct System R
   structure, but histograms are coarse and we do not have SQLite's
   `sqlite_stat1` / skip-scan / automatic indexes. Q3 still picked a left-deep
   hash join + index scan on `lineitem`; SQLite's planner is simply more
   practiced at the same idea.
6. **WAL shape (the 15× recovery).** ARIES physiological logging (before/after
   images, CLR, analysis/redo/undo) is more general than SQLite WAL. Recovery
   does repeating history plus loser undo. SQLite recovery is "replay the WAL
   frames into the db file". We lose on restart time because we do more work
   per log record; we win on the *story* (steal/no-force, recLSN, dirty-page
   table).
7. **No vectorization, no covering indexes, no statistics-driven scan-vs-index
   hysteresis.** Those are documented follow-ups, not hidden defects.

The engine is built so those losses are *local*: swap the tuple representation,
keep the same Volcano operators; replace clock with LRU-K, keep the same WAL
protocol. The benchmark is the contract — re-run `./bench_tpch` after each
change.
