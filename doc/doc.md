### Callgrind

File `callgrind_bench_a1131032f.out` is the callgrind output for [this version](https://github.com/hyrise-mp/hyrise/tree/a1131032f776553bd588c37c9c1aa1a28395ddf7).

Based on this output, these are the most time consuming functions:
- opossum::AbstractTableScanImpl::\_scan_with_iterators<..., ColumnVsValueTableScanImpl::\_scan_dictionary_segment(...), ...> (28.81 + 27.75 %)
- opossum::probe<...>(...) (6.03 %)
- opossum::Validate::\_on_execute(...) (5.25 %)
- opossum::ReferenceSegmentIterable<int, EraseReferencedSegmentType>::\_on_with_iterators<>(...) (3.86 %)
- opossum::AbstractTableScanImpl::\_scan_with_iterators<false, opossum::ColumnBetweenTableScanImpl::\_scan_dictionary_segment(...), ...> (1.24 &)
