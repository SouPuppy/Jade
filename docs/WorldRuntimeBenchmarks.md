# World Runtime ECS benchmark

Measured on 2026-10-12 with Windows x64, AMD Ryzen 9 9900X, MSVC 19.51 and Release optimization. Both binaries use the same three-component workload and allocation instrumentation. Baseline Registry sources were copied read-only from commit `5d9fb3b`; current uses the paged Registry. Values below are medians of three sequential runs, with process memory sampled during execution.

## Creation and allocation

| Entities | Baseline creation ms | Paged creation ms | Baseline allocations | Paged allocations |
| ---: | ---: | ---: | ---: | ---: |
| 400 | 0.317 | 0.092 | 4,850 | 185 |
| 4,096 | 2.795 | 0.781 | 49,217 | 272 |
| 100,000 | 315.645 | 17.135 | 1,200,088 | 1,458 |

Creation includes Registry construction, entity identities and Position/Velocity/Visual components. Signature transitions are cached after their first use; component storage grows in stable 256-slot pages. The baseline uses per-component shared allocations, hash pools and repeated exact-size group reservations.

## Traversal and destruction

| Entities | Baseline snapshot x100 ms | Paged snapshot x100 ms | Baseline update x100 ms | Paged batch update x100 ms | Baseline destroy ms | Paged destroy ms |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 400 | 1.700 | 0.389 | 1.667 | 0.209 | 0.137 | 0.122 |
| 4,096 | 22.185 | 5.886 | 21.402 | 2.098 | 2.195 | 0.924 |
| 100,000 | 1,328.911 | 195.444 | 1,456.924 | 54.260 | 265.934 | 20.712 |

Snapshot traversal copies/sorts matching identities and reads two components. Update traversal changes Position using Velocity. The baseline's update uses its snapshot API; current uses direct group visitation. Thus the batch comparison includes removal of snapshot allocation/sorting as well as the storage change. Checksums agree for every workload. Destruction removes all created entities.

## Memory

Observed peak process working set across the complete benchmark has median 50,733,056 bytes for baseline and 23,511,040 bytes for current, approximately 48.4 MiB and 22.4 MiB. This includes the executable, allocator retention and runtime overhead; it is not an exact owned-ECS capacity measurement.

Creation's cumulative requested allocation bytes are:

| Entities | Baseline requested bytes | Paged requested bytes |
| ---: | ---: | ---: |
| 400 | 545,740 | 152,503 |
| 4,096 | 36,143,443 | 1,535,934 |
| 100,000 | 20,068,652,724 | 39,021,657 |

Requested bytes count all allocation requests, including repeatedly replaced buffers. The baseline's 20 GB figure is cumulative allocation traffic, not simultaneous resident memory. Machine load, allocator behavior and build options affect timing; these are measured samples, not a realtime guarantee or a whole-engine frame benchmark.

Current reproduction: build Release with `JADE_BUILD_BENCHMARKS=ON`, then run `JadeEcsBenchmark.exe`. The optional benchmark has no window/GPU dependency. Raw three-run CSV files and `world-ecs-measurements.json` remain ignored under `build/`; the isolated baseline build is under `build/ecs-baseline/`.
