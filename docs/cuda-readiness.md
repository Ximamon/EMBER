# CUDA readiness

The CPU baseline separates contiguous data ownership (`GridBuffers`) from trivially copyable pointer views (`GridView` and `ConstGridView`). A future CUDA backend can preserve the public configuration and statistics while replacing initialization and update loops.

## Candidate kernels

1. Terrain initialization: one thread per cell, using the existing integer hash.
2. Time-step update: one thread per cell, reading current arrays and writing next state/fuel.
3. Reductions: count burning and ever-burned cells and detect extinction.
4. Scenario batching: map scenarios to an additional grid dimension when device memory permits.

State, both fuel/state buffers and static terrain arrays should remain resident on the device across steps. Swapping current and next pointers avoids full-grid copies. Only counters and final requested grids need host transfers.

## Expected issues

- Boundary and state branches may cause warp divergence.
- Reducing the burning count every step can introduce synchronization and host latency.
- The kernel is likely memory-bandwidth-bound because it reads multiple SoA arrays and neighboring state.
- CPU and GPU floating-point decisions may differ near probability thresholds; reproducibility should be defined per backend rather than bitwise across backends.
- Scenario-level and cell-level parallelism must be benchmarked separately.

The sequential backend remains the correctness oracle. Any optimized backend should first compare terrain hashes, state transitions on deterministic fixtures and final non-timing statistics before performance measurements.
