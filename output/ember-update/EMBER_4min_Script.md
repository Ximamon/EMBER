# EMBER — Four-minute speaker script

Target: 4:00 at approximately 120 words per minute. Timings are rehearsal targets; pause briefly at paragraph breaks.

## Slide 1 — Overnight progress (0:00–0:20)

Good morning. Here is a quick update on our overnight progress with EMBER. We worked on three areas: adding real terrain and wind data, improving GPU execution efficiency, and developing the fire spread model. I’ll briefly explain where each area stands.

## Slide 2 — Real terrain and wind (0:20–1:25)

My contribution was improving the real data inputs for our Collserola case. We now include elevation and a wind snapshot from ERA5 reanalysis, alongside the existing fuel map.

The elevation data is aligned with the simulation grid, so differences in height can influence the slope between neighbouring cells. The wind input gives us a direction and speed for a selected date and hour, instead of relying only on manually chosen values.

For now, that wind is constant across the area and throughout the run. This workflow runs on the scalar CPU version; it is not yet integrated with the GPU version.

The main advance is richer, reproducible environmental inputs. These inputs make our test cases more realistic, but they do not yet make the simulation a calibrated wildfire forecast.

## Slide 3 — GPU execution profiling (1:25–2:30)

Julián continued working on the efficiency of the GPU simulation. These screenshots show execution profiles in NVIDIA Nsight Systems for four MPI processes, labelled zero to three.

Each row shows a sequence of scenarios assigned to one process. The repeated blocks let us inspect how the work is organised, including scenario execution and CUDA synchronisation calls.

The value of these profiles is that they make the execution pattern visible. They help guide optimisation and show where we should measure overhead more carefully.

We do not have a matched before and after benchmark in these screenshots, so I am not claiming a specific speedup. The next measurement should compare the same workload and configuration, checking both execution time and correctness. That will let us quantify the benefit of the changes.

## Slide 4 — Fire model development (2:30–3:25)

Jesús focused on the fire model. He investigated the two approaches described in issue number four and created two branches from dev: rothermel-fire-model and normalized-fuel-model.

The Rothermel model is already implemented in the first branch. However, it still needs to be compiled and tested, so we should describe it as implementation progress, not as a validated result.

The second branch provides a separate place to explore the normalised fuel approach. We are not claiming that this alternative has already been implemented.

This gives us a clear starting point for the next stage: check that the Rothermel implementation builds, test its behaviour, and then assess how it can fit with the rest of EMBER.

## Slide 5 — Next steps (3:25–4:00)

Our proposed next steps are to compile and test the Rothermel model, measure the GPU changes with comparable benchmarks, and move towards integrating the environmental inputs with the other developments.

The three workstreams are progressing, but they are not yet one fully integrated, validated system.

That is our overnight update: richer real data inputs, continued GPU optimisation, and an implemented Rothermel model awaiting validation. Thank you.

