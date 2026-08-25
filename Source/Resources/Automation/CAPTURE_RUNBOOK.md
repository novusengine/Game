# Novus AI capture runbook

This document describes how Codex or Claude Code should collect visual animation
evidence and NVIDIA Nsight Graphics GPU Trace reports through deterministic
Game automation.

## General rules

- Start and stop Game and capture utilities through the connected Novus MCP
  server.
- Use a fresh process for each capture mode.
- Never collect render-target screenshots during a performance trace. GPU
  readback and PNG encoding change the workload being measured.
- Use unique request IDs and artifact directories for every run.
- Wait for `GameEvent.Loaded`, the camera save's `GameEvent.MapLoaded`, and the
  workload-specific settle period before capturing.
- Discover user camera saves at runtime. `Default` is the only camera-save name
  that may be treated as universal.

## Animation screenshot sequence

Copy or rewrite `Scripts/Game/animation_capture_sequence.luau` for the workload.
The committed harness intentionally leaves `PrepareWorkload` and `StartWorkload`
empty so the animation system can supply its own deterministic setup and trigger.
Preparation settles before recording; the start hook runs immediately before the
first frame is queued so the beginning of the animation is not discarded.

The default overview sequence captures `SceneColor` 50 times at 0.1-second
intervals. For a short temporal defect, prefer a one- or two-second sequence at
a higher sample rate. Keep PNG as the canonical evidence format.

GPU readback remains on the render thread, but color conversion, PNG encoding,
and file publication run on a dedicated worker. This preserves the requested
capture cadence while artifacts finish in the background. Do not stop Game until
the terminal marker confirms that every queued artifact is ready.

Run:

```text
automation_run <request-id> Scripts/Game/<rewritten-capture-script>.luau
```

Require:

1. `NOVUS_ANIMATION_CAPTURE_BEGIN`
2. The expected number of `NOVUS_ANIMATION_CAPTURE event=frame-queued` markers
3. One `NOVUS_ARTIFACT` `artifact_ready` marker for every frame
4. `NOVUS_ANIMATION_CAPTURE_COMPLETE status=succeeded`

Artifacts are written below:

```text
Source/Resources/Automation/Artifacts/animation-captures/<request-id>/
```

Review regularly spaced frames first, then inspect every adjacent frame around
any visible discontinuity. Look for pose popping, frozen or lagging parts,
incorrect pivots, attachment lag, deformation, foot sliding, silhouette jumps,
and inconsistent motion vectors or shadows. Generate a higher-frequency follow-up
sequence only around the suspicious interval.

## Nsight GPU Trace preparation

**Performance captures must use the Release Game configuration.** Do not use
Debug, RelDebug, or Tracy builds for Nsight timing, occupancy, throughput, stall,
or bottleneck conclusions. A non-Release trace may be used only to investigate
capture integration or rendering correctness, and its performance data must be
discarded.

GPU Trace requires an NVIDIA Turing-or-newer GPU, profiling permissions, a local
Nsight Graphics installation, and an Nsight host process attached to Game. The
Game integration is disabled unless launched with `-nsight-gpu-trace`; normal
launches do not create the integration object or load NVIDIA runtime libraries.

In MCPTools configuration:

- The Game `nsight_gpu_trace` launch option supplies the command-line flag and
  `NOVUS_NSIGHT_GRAPHICS_PATH`.
- The `nsight` process points at the installed `ngfx.exe` and permits additional
  capture arguments.
- Local installation paths belong in `MCPTools.toml`; keep the example current
  but do not assume every developer installed the same version or location.

Do not combine `renderdoc` and `nsight_gpu_trace`. The two tools cannot inject
different graphics activities into the same process.

Before interpreting a trace as performance evidence, run the same settled scene
in an uninstrumented Release process and issue `performance_snapshot`. Record its
120-frame `gpu_ms` and named `GPU_PASS_AVG` values as the authoritative timing
baseline. Debug captures are useful for correctness and diagnostics, not for
representative frame-time measurements.

## Nsight launch sequence

The normal workflow is headless. Do not open or automate the Nsight Graphics UI
unless the user explicitly asks for UI inspection or the CLI path itself is being
diagnosed.

1. Start a fresh **Release** Game process with launch option
   `nsight_gpu_trace`. Stop if Release is unavailable; do not substitute Debug.
2. Read the Game PID returned by MCPTools.
3. Detect the GPU model with `nvidia-smi` and select the matching Nsight
   architecture and metric set from `ngfx.exe --help-all`.
4. Create a unique ignored output directory below
   `Source/Resources/Automation/Artifacts/nsight/<request-id>`.
5. Start the configured `nsight` process with arguments equivalent to:

   ```text
   --attach-pid=<game-pid>
   --output-dir=<artifact-directory>
   --architecture=<detected-architecture>
   --metric-set-name=<selected-metric-set>
   ```

6. Add MCP launch option `multi_pass` or `shader_profiler` when required.
7. Run `Scripts/Game/validation_nsight_gpu_trace.luau`, or a rewritten copy with
   the animation workload inserted into `PrepareWorkload` and `StartWorkload`.
8. Require `NOVUS_NSIGHT_GPU_TRACE event=host-ready`, `event=started`, and
   `event=stop-requested` in order.
9. Wait for the Nsight host to report the saved native trace, successful export,
   and `Running result: 0` before stopping Game.

The Nsight host is configured with `--start-with-ngfx-sdk` and
`--stop-with-ngfx-sdk`. This keeps the trace boundary under Luau control instead
of guessing from launch time, presentation count, or a hotkey.

### Clean-session recovery

An existing Nsight UI or CLI host may retain or own profiling state. If attach,
metric selection, or host readiness fails, stop the managed Game and Nsight
processes, close any Nsight Graphics UI, and retry once from a clean Game process.
Do not repeatedly wait on an unchanged loading screen or `IsReady()` state.

For `No single-pass metric set selected`, first verify the architecture and metric
set against the installed `ngfx.exe --help-all` output. If they were parsed
correctly, perform the clean-session retry before changing code or configuration.
Escalate with the complete verbose Nsight log if the clean retry also fails.

Do not use a successful standalone NVIDIA sample capture as evidence that the
Novus workflow passed. It is only a diagnostic that can distinguish a general
Nsight installation problem from attach or SDK-trigger problems.

## Profiling modes

Use separate captures for different questions.

### Triage

Start with the architecture's top-level or throughput metric set. Use
`multi_pass` when richer expert-system advice is needed and the workload repeats
identically across frames. Inspect GPU-bound state, unit throughput, occupancy,
queue gaps, synchronization, memory pressure, and the Trace Analysis findings.

Do not treat the exported `FRAME.xls` "GPU frame time" as the game's GPU work
duration without calibrating it against the native Release baseline. It may span
idle time between submitted frames and can move independently of the named GPU
ranges. Prefer the `RenderGraph` and individual pass ranges for trace-local cost,
then report their overhead relative to `performance_snapshot`.

On Blackwell GB20x in Nsight Graphics 2026.1, the only available Top-Level
Triage metric set reports Real-Time Shader Profiler enabled even without the
explicit `shader_profiler` launch option. The CLI does not accept a false value
for that switch. Keep Nsight's default base-clock lock for stable, repeatable
comparisons, and compare only captures made with the same clock policy. These
properties make the trace excellent diagnostic evidence but prevent its absolute
timing from replacing the native baseline.

### Shader profiler

Use `shader_profiler` after triage identifies an SM- or shader-limited range.
Inspect registers per thread, theoretical warp occupancy and its limiting
resource, shared-memory use, warp latency, stall reasons, expensive functions,
and source-line correlation.

Real-Time Shader Profiler and the broadest SM/L1TEX counter collection are
different collection choices. Do not expect one trace to answer every question.

## AI-readable reports

The configured Nsight host uses `--auto-export`. Preserve both the native trace
and every exported metrics file. Record:

- Nsight version, GPU, and driver
- Game configuration and revision
- Map, camera, and workload description
- Metric set and launch modifiers
- Clock policy and V-Sync policy
- Trace warnings or incomplete-data notices
- Trace Analysis findings and their reported severity
- Highest-cost ranges and shader identifiers
- Occupancy, register, shared-memory, and stall evidence supporting each finding

Nsight's native report contains richer interactive analysis than its documented
CLI contract guarantees for exported metrics. If expert recommendations are not
present in the export, inspect the native report in Nsight Graphics rather than
inventing a replacement recommendation engine. The 2026.1 CLI `--auto-export`
produces BASE metrics but does not export the Analysis View's recommendation
text or severities.

When the user asks for NVIDIA's suggestions, collect a Release trace with
`multi_pass`, base clocks, and a deterministic workload. NVIDIA recommends three
frames so the Analysis View can use Aggregate mode. Complete and verify the
headless capture first, then stop Game before opening the report. Use general
computer control only for this offline review:

1. Open the native `.ngfx-gputrace` in Nsight Graphics.
2. Select the Analysis View and Aggregate mode when available.
3. Record every visible finding's severity, title, recommendation, and cited
   metric or range. Capture screenshots as evidence when practical.
4. Clearly label those entries as NVIDIA Trace Analysis findings. Keep any
   additional AI interpretation in a separate section.

Do not claim that raw exported counters are Nsight suggestions. If computer
control or the Analysis View is unavailable, report that NVIDIA's recommendations
could not be retrieved; do not silently replace them with inferred advice.

## Marker audit

The model renderer already gives its render-graph passes stable descriptive
names such as model work/culling, visibility, geometry, velocity, transparency,
and resolve operations. GPU resources also have debug names. Verify those names
appear in the first real trace before adding more instrumentation. Add NVTX or
additional Vulkan labels only where the report cannot identify a material
animation workload.

## Completion criteria

A visual run is complete only when every expected PNG is ready and readable. An
Nsight run is complete only when the host has finalized the native report and
any automatic exports. Require the ordered Game markers (`host-ready`, `started`,
and `stop-requested`), a non-empty `.ngfx-gputrace`, successful automatic export,
and Nsight `Running result: 0`. A Luau success or stop-request marker alone does
not prove that the Nsight artifact is ready.
