# Novus AI validation runbook

This document records the verified workflow for Codex or Claude Code to drive
Novus validation through the connected Novus MCP server.

## Process-control rules

1. Use the connected Novus MCP tools for all Game process management.
2. Never launch `Game-App.exe` through PowerShell, Node, a helper process, or a
   separately spawned MCPTools host.
3. Call `list_processes` before a campaign and resolve unexpected running Game
   processes before starting another.
4. Start each validation mode in a fresh Debug process. Do not combine Vulkan
   validation launch options.
5. Stop each process through `stop_process` and verify `state=stopped` and
   `exit_code=0`.
6. Use `write_console` only with the process ID returned by `start_process`.

Required validation modes:

- `vulkan_validation`
- `vulkan_sync_validation`
- `vulkan_gpu_validation`

Best-practices validation is intentionally excluded.

If the Novus MCP tools are unavailable, stop and fix that connection. Do not
create an ad-hoc process-management fallback.

## Log handling

- Start at cursor `0` once per process.
- Retain and use every returned `next_cursor`.
- Give every `automation_run` command a unique request ID.
- A successful `write_console` proves only that stdin accepted the command.
- Require matching `NOVUS_AUTOMATION` started and succeeded markers.
- A succeeded automation marker does not complete an asynchronous sequence;
  require its workload-specific terminal marker.
- Game configures stdout and stderr as unbuffered streams. Progress markers
  should appear while Game remains running.
- Search validation output separately for:
  - `Validation Error`
  - `Validation Warning`
  - `VUID-`
  - `SYNC-HAZARD`
  - `GPU-AV`
  - `UNASSIGNED-`

After a host reboot, a retained process record may incorrectly say `running`.
Do not trust that state alone. A new run should consume commands and produce
fresh timestamps. Stop stale records through MCP before starting a replacement.

## Choosing a workload

### Fast representative map campaign

Use for ordinary pre-commit rendering validation:

```text
automation_run <request-id> Scripts/Game/validation_map_sequence.luau
```

The script waits for `GameEvent.Loaded` when necessary, discovers and stably
sorts every camera save returned by `Camera.GetSaveNames`, skips the universal
engine-provided `Default` save, loads each user-configured save, then loads and
unloads Azeroth and Kalimdor. Each transition waits for `GameEvent.MapLoaded`
and a five-second dwell.

Require:

```text
NOVUS_VALIDATION_SEQUENCE_COMPLETE steps=<count>
```

Treat `NOVUS_VALIDATION_SEQUENCE_FAILED` or the five-minute per-transition
watchdog as failure.

### Extensive all-map campaign

Use for broad content and renderer coverage:

```text
automation_run <request-id> Scripts/Game/validation_all_maps_sequence.luau
```

Require the discovery marker before waiting for completion:

```text
NOVUS_VALIDATION_SEQUENCE event=discovered camera-saves=<count> maps=<count>
```

The script loads and unloads every stably sorted record returned by
`Map.GetList()`. If native loading emits `GameEvent.MapLoadFailed`, the script
reports the failed load and paired unload as skipped, then continues. Preserve
the map name and reason. A nonzero skipped count is content availability
information, not by itself a harness failure.

Require:

```text
NOVUS_VALIDATION_SEQUENCE_COMPLETE steps=<count> skipped=<count>
```

### CVAR campaign

Use a separate fresh process:

```text
automation_run <request-id> Scripts/Game/validation_cvar_sweep.luau
```

The script discovers and stably sorts the available camera saves, skips
`Default`, and requires at least one user-configured save. It owns camera
loading, the baseline snapshot, ten-frame settling, restoration, and terminal
developer-mode handling. Do not replace it with individual MCP calls.

Require the following in order:

1. `NOVUS_CVAR_SEQUENCE event=camera-loaded`
2. `NOVUS_CVAR_SNAPSHOT_BEGIN`
3. `NOVUS_CVAR_SNAPSHOT_END`
4. `NOVUS_CVAR_SWEEP_BEGIN`
5. `NOVUS_CVAR_SEQUENCE_COMPLETE`

Success requires:

```text
restoreMismatches=0 ... status=restored
```

The expected exceptions are:

- `cl_r_joltEnabled` is skipped with `reason=blacklisted` and
  `detail=known_game_hang`.
- `cl_r_fogBlendBegin`, `cl_r_fogBlendEnd`, and `cl_r_fogColor` may report
  `NOVUS_CVAR_LIVE_DRIFT` because sky/time simulation updates them continuously.

Do not generalize the live-drift exception to other CVARs.

## CVAR crash or hang recovery

If Game crashes or stops consuming commands:

1. Retain the initial snapshot.
2. Find the last unmatched `NOVUS_CVAR_BEGIN`.
3. Record its CVAR name, last choice, validation output, and process exit code.
4. Restore its original snapshot value if it persisted.
5. Set `START_INDEX` in `validation_cvar_sweep.luau` to the following sorted
   index.
6. Start a fresh process with the same validation mode and execute the complete
   script again. It will discover and load the configured camera save before
   resuming.
7. Restore `START_INDEX` to `1` after the investigation.

Treat a confirmed hang the same as a crash. A process reported as running is
still hung if update callbacks and independent stdin commands are no longer
consumed.

## Environment-dependent results

Camera saves, map availability, asset errors, timings, and validation findings
belong to the current run and environment. Discover them at runtime and store
them under ignored `Source/Resources/Automation/Reports/`. Do not hard-code user-created camera
save names, historical map counts, skipped-map lists, machine timings, or past
validation messages into the scripts or this runbook.

## Reporting a run

For each process, report:

- Game configuration and launch option.
- Workload and request ID.
- Discovered map/CVAR count where applicable.
- Completed and skipped action counts.
- Any failed action with its stable reason.
- Distinct validation IDs or hazards and their first relevant context.
- Restoration mismatches and documented live drifts.
- Final process state and exit code.

Store generated logs and summaries under ignored
`Source/Resources/Automation/Reports/`, not as committed source files.
