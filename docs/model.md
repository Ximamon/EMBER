# Simplified propagation model

EMBER deliberately uses an educational local model. It is suitable for correctness, reproducibility and performance experiments, but it is not scientifically validated and must not be used for operational decisions.

## State and storage

Each cell is `Unburned`, `Burning`, `Burned` or `NonCombustible`. State and fuel use current/next buffers; moisture, vegetation and elevation remain constant. Every step reads only current data and writes every next cell before swapping the buffers.

The model inspects the eight-cell Moore neighborhood. Out-of-bounds neighbors are ignored. Newly ignited cells cannot influence another cell until the following step.

## Ignition probability

For every burning neighbor $j \in \mathcal{N}_i$, an ignition probability contribution $p_{j \to i}$ is computed from the target cell $i$'s fuel ($M_{\text{fuel}}$), moisture ($M_{\text{moisture}}$), and vegetation ($M_{\text{veg}}$); wind alignment; elevation difference; and neighbor distance:

$$
f_{\text{moisture}} = 1 - 0.8 \cdot \text{clamp}(M_{\text{moisture}}, 0, 1)
$$

$$
f_{\text{wind}} = \text{clamp}\left(1 + U_{\text{wind}} \cdot \mathbf{a}, \, 0.25, \, 2.0\right)
$$

$$
S = \text{clamp}\left(\frac{E_i - E_j}{S_{\text{scale}}}, \, -1.0, \, 1.0\right)
$$

$$
f_{\text{slope}} = \text{clamp}\left(1 + 0.5 \cdot S, \, 0.5, \, 1.5\right)
$$

$$
f_{\text{distance}} = \begin{cases}
1.0 & \text{for cardinal neighbors } (\Delta x \cdot \Delta y = 0) \\
\frac{1}{\sqrt{2}} \approx 0.7071 & \text{for diagonal neighbors } (\Delta x \cdot \Delta y \neq 0)
\end{cases}
$$

The single-neighbor ignition probability $p_{j \to i}$ is given by:

$$
p_{j \to i} = \text{clamp}\Big(P_{\text{base}} \cdot M_{\text{fuel}} \cdot M_{\text{veg}} \cdot f_{\text{moisture}} \cdot f_{\text{wind}} \cdot f_{\text{slope}} \cdot f_{\text{distance}}, \, 0, \, 1\Big)
$$

Where the wind alignment $\mathbf{a} = \hat{\mathbf{d}} \cdot \hat{\mathbf{w}}$ is the dot product between the propagation direction unit vector $\hat{\mathbf{d}}$ (from burning neighbor $j$ to target $i$) and the global wind vector $\hat{\mathbf{w}} = (\cos \theta, \sin \theta)$.

Contributions from all burning neighbors $j \in \mathcal{N}_{\text{burning}}$ are combined as independent events:

$$
P_{\text{total}}(i) = 1 - \prod_{j \in \mathcal{N}_{\text{burning}}} \left(1 - p_{j \to i}\right)
$$

One stateless random draw $U \sim \text{Uniform}(0, 1)$ keyed by `(scenario_seed, step_index, cell_index)` decides ignition ($U < P_{\text{total}}(i)$). A burning cell loses $R_{\text{burn}}$ fuel per step and becomes `Burned` when fuel reaches $0$. A newly burning cell starts consuming fuel on its next step.

## Synthetic terrain

Fuel, moisture, vegetation, elevation and non-combustibility use independent hash tags. A scenario seed is derived from the global seed and scenario identifier. This avoids mutable RNG state and makes later parallel traversal deterministic.

Default terrain samples are independent per cell. Spatially correlated terrain, real fuel classes, dynamic weather, physical spread rates and validated slope functions are intentionally outside the MVP. Those features can replace terrain initialization and the per-neighbor probability calculation without changing the runner or buffer protocol.
