# Novus Game automation

This directory contains deterministic Luau workloads for AI-driven Game
verification. MCPTools configures it as `NOVUS_AUTOMATION_ROOT`; Game accepts
only canonical regular `.luau` files below `Scripts`.

Execute a workload through the redirected Game console:

```text
automation_run <request-id> Scripts/<script>.luau
```

The request ID is selected by the caller. Game emits `NOVUS_AUTOMATION`
started, succeeded, or failed markers containing that ID. A succeeded marker
means the script was loaded and registered successfully; asynchronous workloads
must also emit their own terminal marker before the run is complete.

## Retained workloads

### Fast map validation

```text
automation_run validation-fast-maps Scripts/Game/validation_map_sequence.luau
```

Discovers and stably sorts every camera save returned by `Camera.GetSaveNames`,
skips the universal engine-provided `Default` save, then loads each
user-configured save before loading and unloading `Azeroth` and `Kalimdor`.
Each action waits for the native `GameEvent.MapLoaded` event and then dwells
for five seconds through `Scheduler.AfterSeconds`.

Success:

```text
NOVUS_VALIDATION_SEQUENCE_COMPLETE steps=<count>
```

Failure:

```text
NOVUS_VALIDATION_SEQUENCE_FAILED reason=<reason>
```

### All-map validation

```text
automation_run validation-all-maps Scripts/Game/validation_all_maps_sequence.luau
```

Discovers and stably sorts every camera save returned by `Camera.GetSaveNames`,
skips `Default`, then loads each user-configured save. It subsequently
discovers and stably sorts every map returned by `Map.GetList()` and loads and
unloads each map. Native
`GameEvent.MapLoadFailed` events are reported as skipped actions so one
unavailable map does not abort the rest of the campaign.

Discovery and success markers:

```text
NOVUS_VALIDATION_SEQUENCE event=discovered camera-saves=<count> maps=<count>
NOVUS_VALIDATION_SEQUENCE_COMPLETE steps=<count> skipped=<count>
```

The skipped count is an action count. A failed map load skips both that load
and its now-inapplicable unload, contributing two skipped actions.

### CVAR validation

```text
automation_run validation-cvars Scripts/Game/validation_cvar_sweep.luau
```

Waits for Game readiness, discovers and stably sorts the available camera
saves, skips `Default`, loads the first user-configured save, and dwells for
five seconds after its map completion event. At least one user-configured
camera save is required. The script then snapshots the complete CVAR registry
and exercises every finite value it can derive:

- Boolean: `false` and `true`.
- Integer: small documented ranges and documented enum values.
- Float: documented range boundaries.
- String/vector: current and default values.
- Read-only: reported and skipped.

Every applied choice remains active for ten complete application frames through
`Scheduler.AfterFrames`, after which the original value is restored.

`cl_r_joltEnabled` is explicitly blacklisted because enabling it is known to
hang Game. `cl_developerMode` is exercised last and restored synchronously
because changing it schedules a Luau-state reload.

Success:

```text
NOVUS_CVAR_SEQUENCE_COMPLETE count=<count> restoreMismatches=0 liveDrifts=<count> status=restored
```

`cl_r_fogBlendBegin`, `cl_r_fogBlendEnd`, and `cl_r_fogColor` are continuously
updated by sky/time simulation. Differences in those values are reported as
live drifts rather than restoration failures.

## Automation APIs

The retained sequences are stable shared validation workloads. For a
feature-specific investigation, an AI may create or rewrite a temporary Luau
script under `Scripts`, execute it, inspect its output or artifacts, and remove
it afterward.

### Render-target capture

```luau
local accepted, captureError = RenderTarget.Dump(
    "SceneColor",
    "feature-184/scene-color.png")
assert(accepted, captureError)
```

Paths are relative to the ignored `Artifacts` directory and must use `.png`.
The call queues the request. Wait for the corresponding `NOVUS_ARTIFACT`
`artifact_ready` or `artifact_failed` marker before reading the file.

### RenderDoc

Place `renderdoc.dll` next to `Game-App.exe` and start Game with the MCPTools
`renderdoc` launch option. Then a feature-specific script may queue a frame:

```luau
assert(RenderDoc.IsAvailable(), "RenderDoc is unavailable")
local accepted, captureError =
    RenderDoc.CaptureNextFrame("feature-184/frame.rdc")
assert(accepted, captureError)
```

Wait for the `NOVUS_ARTIFACT` marker before opening the capture.

### Tracy

Set `Enable Tracy` to `true` in `Premake/BuildSettings.lua`, regenerate the
Visual Studio projects, and rebuild the desired Game configuration. Use that
build with the version-matched `tracy-capture` process. Start Game before the
capture process, wait until `Tracy.IsConnected()` is true, execute the workload,
and explicitly stop the capture when finished. The ten-minute watchdog is
failure recovery, not the intended capture duration.

Feature-specific scripts can insert diagnostic messages with:

```luau
Tracy.Message("feature-184 shadows-off")
```

## Vulkan validation modes

MCPTools exposes three process-scoped launch options:

- `vulkan_validation`
- `vulkan_sync_validation`
- `vulkan_gpu_validation`

Each uses `VK_LAYER_KHRONOS_validation` through the launched process
environment. Game and Engine contain no validation-layer activation code.
Run modes separately in fresh Debug processes and inspect redirected output.
Best-practices validation is intentionally excluded.

See `VALIDATION_RUNBOOK.md` for the required process workflow, log checks,
known findings, and recovery rules.
