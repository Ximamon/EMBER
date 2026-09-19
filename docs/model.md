# Simplified propagation model

EMBER deliberately uses an educational local model. It is suitable for correctness, reproducibility and performance experiments, but it is not scientifically validated and must not be used for operational decisions.

## State and storage

The simulation grid is structured as a 2D rectangular grid of dimensions $\text{width} \times \text{height}$ flattened into contiguous 1D memory buffers using a **Structure-of-Arrays (SoA)** layout managed by `GridBuffers`. Each cell is accessed via its 1D index:

$$
\text{index} = \text{row} \times \text{width} + \text{column}
$$

Each cell is in one of four states represented by the `CellState` enum:
- `Unburned`: Cell has not ignited and can catch fire.
- `Burning`: Cell is actively on fire and spreading heat to neighbors.
- `Burned`: Cell fuel has been completely consumed and cannot ignite again.
- `NonCombustible`: Cell contains non-combustible material (e.g., river, road) and cannot ignite.

### Double buffering

To prevent data races and order dependencies during cell traversal, dynamic properties (`state` and `fuel`) maintain two internal vectors (`states_[2]` and `fuels_[2]`):
- **Current view (`current_view`)**: Read-only buffer for step $t$.
- **Next view (`next_view`)**: Write-only target buffer for step $t+1$.

Static terrain properties (`moisture`, `vegetation`, and `elevation`) do not change during simulation and use single contiguous vectors (`moisture_`, `vegetation_`, `elevation_`).

At the end of each time step, `swap_buffers()` alternates the active index (`current_index_ = 1U - current_index_`) in $O(1)$ time without copying memory.

The model inspects the eight-cell Moore neighborhood ($\mathcal{N}_i$). Out-of-bounds neighbors are ignored. Newly ignited cells in step $t$ do not influence neighbor ignition until step $t+1$.

## Ignition probability

For every burning neighbor $j \in \mathcal{N}_i$, an ignition probability contribution $p_{j \to i}$ is computed from target cell $i$'s fuel ($M_{\text{fuel}}$), moisture ($M_{\text{moisture}}$), and vegetation ($M_{\text{veg}}$); wind alignment; elevation difference; and neighbor distance.

### The `clamp` function

Throughout the model equations, $\text{clamp}(v, a, b)$ (implemented via `std::clamp` / `clamp01`) restricts a scalar value $v$ to the closed interval $[a, b]$:

$$
\text{clamp}(v, a, b) = \max\left(a, \min(v, b)\right) = \begin{cases} a & \text{if } v < a \\\\ v & \text{if } a \le v \le b \\\\ b & \text{if } v > b \end{cases}
$$

When no upper bound is specified (e.g., $\text{clamp}(v)$ or `clamp01`), the default interval is $[0, 1]$. This prevents numerical overflow and bounds modifiers within valid physical limits.

### Environmental factors

1. **Moisture modifier**:
   $$
   f_{\text{moisture}} = 1 - 0.8 \cdot \text{clamp}(M_{\text{moisture}}, 0, 1)
   $$

2. **Wind alignment modifier**:
   $$
   f_{\text{wind}} = \text{clamp}\left(1 + U_{\text{wind}} \cdot \mathbf{a}, \, 0.25, \, 2.0\right)
   $$
   where $\mathbf{a} = \hat{\mathbf{d}} \cdot \hat{\mathbf{w}}$ is the dot product between the propagation direction unit vector $\hat{\mathbf{d}}$ (from burning neighbor $j$ to target $i$) and the global wind vector $\hat{\mathbf{w}} = (\cos \theta, \sin \theta)$. In grid space where row index grows downwards, the direction vector is $\hat{\mathbf{d}} = ( \Delta x \cdot f_{\text{distance}}, \, -\Delta y \cdot f_{\text{distance}} )$ to align $90^\circ$ with North (upwards).

3. **Slope modifier**:
   $$
   S = \text{clamp}\left(\frac{E_i - E_j}{S_{\text{scale}}}, \, -1.0, \, 1.0\right)
   $$
   $$
   f_{\text{slope}} = \text{clamp}\left(1 + 0.5 \cdot S, \, 0.5, \, 1.5\right)
   $$
   Fire spreads faster uphill ($E_i > E_j$) and slower downhill ($E_i < E_j$).

4. **Distance factor**:
   $$
   f_{\text{distance}} = \begin{cases}
   1.0 & \text{for cardinal neighbors } (\Delta x \cdot \Delta y = 0) \\
   \frac{1}{\sqrt{2}} \approx 0.70710678 & \text{for diagonal neighbors } (\Delta x \cdot \Delta y \neq 0)
   \end{cases}
   $$

### Single-neighbor probability

The probability of target cell $i$ igniting due to burning neighbor $j$ is:

$$
p_{j \to i} = \text{clamp}\Big(P_{\text{base}} \cdot \text{clamp}(M_{\text{fuel}}, 0, 1) \cdot M_{\text{veg}} \cdot f_{\text{moisture}} \cdot f_{\text{wind}} \cdot f_{\text{slope}} \cdot f_{\text{distance}}, \, 0, \, 1\Big)
$$

### Multi-neighbor independent combination

Contributions from all burning neighbors $j \in \mathcal{N}_{\text{burning}}$ are combined assuming independent ignition events:

$$
P_{\text{total}}(i) = 1 - \prod_{j \in \mathcal{N}_{\text{burning}}} \left(1 - p_{j \to i}\right)
$$

A target cell ignites if a stateless random draw $U \sim \text{Uniform}(0, 1)$ satisfies $U < P_{\text{total}}(i)$.

The random value $U$ is derived deterministically for cell $i$ at step $t$ using the C++ function:

```cpp
const double U = uniform01(keyed_hash(
    scenario_seed, random_tag::spread, step_index, cell_index));
```

## Fuel consumption and cell lifecycle

- **Burning cells**: Lose fuel at rate $R_{\text{burn}}$ per step:
  $$
  M_{\text{fuel}}(t+1) = \max\left(0, \, M_{\text{fuel}}(t) - R_{\text{burn}}\right)
  $$
  When fuel reaches $0$, the cell transitions to `Burned`.
- **Newly ignited cells**: Transition to `Burning` in `next_view` during step $t$ without consuming fuel until step $t+1$.
- **Initial ignition points**: Configured via `--ignition X,Y` or defaulted to the center cell $(\lfloor \text{width}/2 \rfloor, \lfloor \text{height}/2 \rfloor)$. Ignition cells have their fuel initialized to $\max(M_{\text{fuel}}, R_{\text{burn}})$ in both buffers to guarantee they burn for at least one step.

## Synthetic terrain and stateless PRNG

All initial cell parameters are procedurally generated using stateless 64-bit hashing (`keyed_hash`) with dedicated 64-bit tags:
- `random_tag::fuel` (`0x4655454c`)
- `random_tag::moisture` (`0x4d4f4953`)
- `random_tag::vegetation` (`0x56454745`)
- `random_tag::elevation` (`0x454c4556`)
- `random_tag::non_combustible` (`0x4e4f4e43`)
- `random_tag::spread` (`0x53505244`)

The scenario seed is derived deterministically from global seed and scenario ID (`scenario_seed = hash_combine(global_seed, scenario_id)`). This eliminates mutable PRNG state and ensures bitwise reproducibility across thread or cell traversal variations.

Default terrain samples are independent per cell. Spatially correlated terrain, real fuel classes, dynamic weather, physical spread rates and validated slope functions are intentionally outside the MVP. Those features can replace terrain initialization and the per-neighbor probability calculation without changing the runner or buffer protocol.

