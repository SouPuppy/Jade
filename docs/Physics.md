# Physics runtime

`Jade::Physics::World` is the CPU simulation owner. It can simulate without a
window, renderer or live ECS instance. `Jade::World` owns ECS authoring
data; `Physics::System` bridges that data to the simulation and publishes the
result. There is one collision/integration/solver core, not two selectable engines.

## Runtime state and commands

Body IDs contain a world identity, slot and generation. Slots may be reused, but
an expired or foreign ID cannot address a new body. Runtime translation, velocity,
angular momentum and orientation use double precision. ECS Transform and RigidBody
values are authoring/presentation mirrors, converted with checked float limits.
Use World commands for runtime edits:

```cpp
Jade::JobSystem jobs({12});
Jade::Physics::World physics(jobs);
Jade::Physics::BodyDescription body;
body.shape = Jade::Collider{Jade::Sphere{0.5f}};
body.position = {0, 5, 0};
auto id = physics.create(body);
physics.addForce(id, {10, 0, 0});
physics.step(1.0 / 120.0);
auto state = physics.state(id);
```

Create static, dynamic or kinematic bodies explicitly. Dynamic mass must be
positive. Updating shape/mass recomputes inertia. Teleport preserves velocities
unless `clearVelocity` is selected. Impulses act at world-space points. One-step
force/torque is consumed only after a successful step; persistent loads remain
until replaced. Kinematic targets have an explicit duration and stop on arrival.
Empty shapes and disabled bodies preserve their state without participating.

The World and JobSystem are single-owner objects. The scheduler must outlive an
attached standalone World. Do not change World state from field/job callbacks or
while a step is executing. Runtime detaches scheduler pointers when releasing
worlds. `WorldSnapshot` owns an immutable copy for concurrent query consumers;
it retains original BodyIDs even after the live bodies are deleted.

## Fields and integration

Fields are immutable, composable physics inputs. Their stage sample returns force
in N and torque in N*m. A stage context includes simulation time, precise body
position/velocity, rotation, angular velocity/momentum, mass, BodyID and the complete read-only stage snapshot. The
legacy Vec3 sampling hook remains useful for simple user-defined fields; native
gravity kernels use the precise values.

Custom fields may override `sampleStage` or `prepare` directly to supply torque;
they do not need to implement the legacy `sample` hook. `World::fieldSnapshot`
returns a read-only value copy in BodyID slot order, including tombstones, and
`World::fields` returns the current immutable field handles. Solved force
inspection reuses the committed result without preparing another stage. After
edits, the bridge previews fields against a private snapshot containing both
authoring bodies and native-only sources, and marks the result unsolved.

`prepare` executes once before parallel sampling. BodyGravity resolves sources
once per stage and samples in O(targets * sources), with no per-target ECS access
or source search. BodyID sources reject foreign/stale/disabled bindings; ECS
sources retain their authoring validation and deletion behavior. Uniform, point,
body-source and composite gravity use the same field pipeline. Replacing a field
invalidates force diagnostics and wakes affected simulation state.

Semi-implicit Euler and Velocity Verlet are supported. Verlet captures complete
snapshots at both stages and reports the average force actually applied. Free
asymmetric rotation advances orientation with an implicit midpoint solve while
preserving world angular momentum. Position correction does not apply velocity
impulses and preserves angular momentum.

Force diagnostics report sampled fields plus explicit loads, and contact impulses
divided by the step duration. `totalForce` sums these two contributions; positional
correction and optional implicit damping are excluded. Second-order convergence
is tested for position-dependent gravity with Verlet, rather than asserted for
arbitrary velocity-dependent custom fields.

Input errors, field exceptions, worker errors, numerical overflow and failed CCD
leave the runtime state as it stood before the step, with no partial publication
of contacts, time or statistics. Accepted commands before the step remain applied.
Pending one-step loads remain available for retry. Derived spatial indexes are
invalidated on failure and rebuilt before their next use.

## Collision, solver and queries

Static and moving bodies have separate persistent four-way BVHs. Bulk construction
uses binned SAH, leaf bounds have motion/contact padding, changed leaves refit,
and quality degradation triggers rebuilding. Queries start after tree writes
finish. Sleeping bodies remain indexed, so active bodies and changed support can
wake them. Category/mask filters default to allowing every pair.

Sphere contacts use analytical geometry; boxes use all SAT axes and retain stable
surface witnesses near parallel faces. Clipping and contact construction use
double precision and bounded stack storage, including world-space witnesses.
Contact caches validate generational IDs, shape changes,
normals and local anchors. Each solved manifold keeps at most four points.

The solver precomputes effective masses and angular impulse responses. Coulomb
friction uses a coupled 2x2 tangential mass and circular impulse limit. Defaults
remain 20 velocity iterations, 4 position iterations, 0.005 m contact distance
and 0.002 m penetration slop. No fast-math or artificial default damping is used.
The simulation contact distance must be positive; overlap and cast queries may
request zero margin.

Dynamic contact graphs retain connectivity between steps. New edges merge islands;
removed edges trigger splitting. Static ground does not merge independent islands.
Large islands are colored by whole manifolds: bodies written by one manifold
cannot be written by another manifold of the same color. Iterations and colors
have dependency barriers. SIMD packets span independent manifolds; points within
a manifold remain ordered. Scalar, SSE2 and AVX2 execute the same physical model.

Sleep is an island decision, using surface motion, contact residuals and support
balance. Defaults are 0.03 m/s for 0.5 s. Forces, impulses, teleports, support loss,
kinematic contact and changes to fields/shape/mass wake bodies. Unknown custom
fields and moving gravity sources cannot certify sleep. Use `Config::sleeping =
false` to validate the solver independently of sleep.
Static edits wake nearby affected islands through the spatial index, so editing
distant support does not wake the whole world. A fully sleeping unchanged world
with sleep-compatible fields advances time without scanning bodies or solving
contacts.

Discrete collision is the default. Continuous bodies use swept candidates,
support-mapped distance bounds and conservative advancement, including relative
translation and angular sweep. The earliest TOI divides the world step into
segments. Existing overlaps are solved separately. Budgets default to 32 events
and 32 advancement iterations; exhaustion throws and rolls back, rather than
silently tunneling. AABB, overlap, ray and shape-cast queries share the index.
Query filters can exclude a body, restrict motion types and reject hit normals.
Cast hits report the obstacle surface witness and initial overlap penetration.
Immutable snapshots support AABB, overlap, ray and shape-cast queries as well.
The planar character controller uses shape casts and sliding, with its existing
ground-height behavior; stairs, slopes and jumping are outside this implementation.

## Scheduling and measurement

Physics shares the engine JobSystem. Worker-local queues have independent locks,
work stealing balances queues, and range tasks claim several atomic leases per
queue visit. Global control state is used for graph submission/lifecycle, not
every range element. Worker waits help execute jobs. External helpers only execute
the graph they are waiting for and its descendants. Priority inheritance, weighted
fairness, failure propagation and shutdown draining remain part of the contract.

Field sampling, integration, bounds, candidate queries, narrow phase, constraint
preparation, colored solving and output conversion expose parallel work. Solver
iterations are submitted as a dependency graph. Small workloads use a serial path.
Increasing workers does not eliminate serial publication, memory bandwidth limits
or physics dependencies; CPU utilization is not an acceptance target by itself.

`JobStatistics` exposes per-worker busy/idle time, queue/lock waits, task counts,
stealing and category execution time. `Physics::Statistics` reports phase timings,
active/sleeping bodies, pairs, contacts, islands, colors, cache hits and TOI events.
Timing units are milliseconds; scheduler counters use nanoseconds. Nested help
waits are excluded from their parent's execution time to avoid double counting.
Reported lock waits cover queue locks. Worker category busy time measures executed
job wall time, not operating-system CPU utilization.

Build `JadePhysicsBenchmark` for headless measurement:

```text
JadePhysicsBenchmark --scene scene5 --count 100000 --workers 12 --steps 24 --output scene5.csv
JadePhysicsBenchmark --scene dense --count 10000 --workers 12 --steps 120
JadePhysicsBenchmark --scene rest --count 10000 --workers 12 --warmup 240 --steps 120
JadePhysicsBenchmark --scene fields --count 10000 --workers 12 --steps 32
```

Other workloads are sparse, mixed and ccd. `--workers 0` selects a serial World;
positive values create that many workers, with the waiting thread able to help.
Scene 5 CCD can be requested with `--continuous 1`; the default matches discrete
simulation. Compare p50/p95 and phase times under identical inputs, warmup, step
size and iteration counts. Rendering is excluded. `--bridge 1` includes ECS
synchronization and publication and reports their cost separately; the default
measures the native simulation. `--allocations 1` counts benchmark-process C++
allocations after warmup, including scheduler allocations, and adds instrumentation
overhead. `core_capacity_bytes` sums owned physics container capacities, excluding
ECS, renderer, scheduler storage and allocations owned by external field objects.
Dynamic payloads inside prepared kernel closures are also outside this capacity sum.
The ten-wall scene receives no special activation, filtering or solver shortcuts.

`benchmarks/run-physics.ps1 -Suite all` runs the worker scaling matrix, 1k/10k/100k
body workload matrix, bridge, CCD, allocation and sleep-disabled checks. CSV and
worker timing logs are retained under `build/physics-release/validation`.

This implementation covers sphere/box rigid bodies, fields, kinematics, queries,
CCD and the existing planar character controller. Joints, mesh collision, soft
bodies and GPU physics remain outside this engine version. Large active contact
graphs remain bounded by synchronization, memory traffic and iterative solving;
multi-core execution alone does not promise a 100k-body realtime frame rate.

## Measured behavior (2026-10-12)

Windows x64 Release, MSVC 19.51, AMD Ryzen 9 9900X (12 cores / 24 logical
processors). These are single-run measurements, not universal timing guarantees.
The default benchmark uses a 1/120 s step, 20 velocity / 4 position iterations,
automatic SIMD selection, 4 warmup steps and 24 measured steps. Sleeping is disabled
except in the rest workload, which warms up for 120 steps. No compiler, other test
suite or other Demo was active during the final matrix.

Scene 5 contains 100,000 dynamic cubes plus the dynamic projectile. All remain
active during this comparison. Increasing worker count changes scheduling only:
final contact count, maximum penetration, displaced-body count and maximum speeds
agree across all six runs at the CSV precision.

| Workers | p50 ms/step | p95 ms/step | Workers with physics execution | Average worker execution overlap |
| --- | ---: | ---: | ---: | ---: |
| 0 (serial) | 908.7 | 1063.4 | 0 | 0.00 |
| 1 | 669.1 | 743.9 | 1 | 0.95 |
| 3 | 373.6 | 411.0 | 3 | 2.70 |
| 6 | 238.2 | 260.5 | 6 | 5.08 |
| 12 | 183.7 | 202.5 | 12 | 9.47 |
| 23 | 171.3 | 189.6 | 23 | 16.68 |

The 23-worker run is 5.3 times faster than the serial run. Execution overlap is
summed worker physics job wall time divided by measurement wall time; it excludes
the waiting/helper thread and is neither an OS utilization percentage nor a count
of physical cores. Worker logs contain individual execution/task/queue-lock totals.

These workloads use 12 workers; values below are native physics p50 ms/step:

| Workload | 1,000 bodies | 10,000 bodies | 100,000 bodies | 100k p95 |
| --- | ---: | ---: | ---: | ---: |
| Sparse rotating/moving boxes | 1.95 | 3.07 | 27.06 | 29.11 |
| Dense touching floor grid, sleep disabled | 15.13 | 70.17 | 811.80 | 832.62 |
| Fully sleeping floor grid | <0.001 | <0.001 | <0.001 | <0.001 |
| Mixed box sizes and masses | 2.01 | 3.14 | 26.94 | 29.66 |
| 64 moving body-gravity sources | 1.95 | 3.43 | 29.74 | 30.39 |
| 1,200 m/s spheres bouncing between thin walls, CCD | 3.41 | 7.91 | 64.90 | 150.15 |

The sleeping result measures an unchanged-world time advance; it retains its
contacts/index/state and does no body scan. It is not the cost of putting a new
world to sleep. Small parallel workloads are sensitive to scheduling overhead;
`Config::parallelThreshold` is available to adjust the serial cutoff.

Scene 5 over 240 measured steps (after 4 warmup steps) gives p50 210.5 ms and p95
253.5 ms with 23 workers. All body states remain finite. At the final step, maximum
penetration is 0.0948 m and owned core container capacity is approximately 1.21 GB;
finite state does not imply penetration stays within contact distance during this
violent tall-wall impact. This covers about two seconds, not a traversal of all
ten walls or an unlimited-duration stability proof.

Including per-step ECS synchronization/publication gives Scene 5 p50 344.0 ms,
p95 359.9 ms; the mean bridge contribution is 150.0 ms. Runtime amortizes the
bridge across fixed substeps when there is no fixed gameplay callback. Scene 5
with projectile CCD and 12 workers gives p50 361.1 ms, p95 1766.9 ms, using the
default 32-event / 32-iteration budgets. CCD cost varies with impact segmentation.

Core container capacity is about 0.92 GB in the short Scene 5 run and 2.53 GB in
the 100k dense grid. The instrumented short Scene 5 run records about 651 C++
allocations and 53.2 MB cumulative allocation traffic per step, including growing
contact capacities and job submission. It is not allocation-free. The unchanged
10k resting grid records zero allocations; disabling sleep raises its p50 to
75.0 ms. Allocation traffic is not retained or peak process memory.

The remaining costs follow the workload: BVH maintenance/traversal depends on
body count and candidate overlap; contact generation/cache/storage depends on
contact count; solving depends on contact points, iterations and color barriers;
body gravity is O(targets * sources); CCD repeats work at impact boundaries. Dense
touching grids therefore cost much more than sparse motion with the same body
count. The solver and bridge still have substantial memory traffic and serial
work. These measurements do not establish realtime performance for Scene 5.

Full validation and actual Demo execution are recorded in [Validation.md](Validation.md).
