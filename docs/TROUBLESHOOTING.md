# Troubleshooting AegisDB

Use the smallest reproducible fixture before changing production data or configuration. Run commands from the repository root.

## A key not inserted returns maybe

Bloom filters allow false positives. Confirm against the authoritative data structure before treating a maybe as presence.

## MergeFrom throws

Construct both filters with compatible capacity/hash settings. Rebuild from original keys when their shapes differ; do not truncate bit vectors.

## The model estimate rises after a repeated merge

Count measures insertion operations. Use FillRatio to observe actual occupied bits; neither value is exact distinct cardinality.

## Reproduce and report

1. Record the exact command or UI action, runtime version, expected result, and actual result.
2. Run the regression command below on the same checkout and keep its first failure, not only the final summary.
3. Reduce the issue to a deterministic fixture without personal data or credentials.
4. Include the affected source path from the [feature guide](RECENT_IMPROVEMENTS.md) and whether it reproduces on a clean checkout.

```sh
make test && python3 -m unittest discover -s tests -v
```

## Recovery

Keep a copy of input data before experimenting. Revert the individual source change with Git and rerun the same fixture; a source revert does not automatically reverse browser storage, downloaded files, or external service state. Do not delete a lockfile or weaken a validation gate to make a failure disappear.
