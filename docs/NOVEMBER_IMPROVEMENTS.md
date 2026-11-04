# Heap compaction

Slotted-page compaction now packs payloads through one fixed 4 KiB stack buffer instead of allocating nested vectors per tuple. Slot IDs, page links and live payload bytes are preserved. The allocation-counting regression observes zero C++ heap allocations during compaction; this is an allocation claim, not an end-to-end throughput benchmark.

`SlottedPage::CanInsert` performs a nonmutating fit check that includes reclaimable payload bytes and directory-slot reuse. It neither compacts nor dirties the page; actual insertion retains the existing allocation/compaction behavior.
