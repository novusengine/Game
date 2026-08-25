---
name: novus-nsight-gpu-trace
description: Capture and analyze NVIDIA Nsight Graphics GPU Trace reports for Novus Game through the headless MCPTools and Luau automation workflow. Use for GPU occupancy, throughput, stalls, shader profiling, register pressure, or Nsight expert findings; do not use for RenderDoc frame debugging.
---

# Novus Nsight GPU Trace

Locate the active Novus Game repository and read
`Source/Resources/Automation/CAPTURE_RUNBOOK.md`, then follow its Nsight workflow
and completion criteria.

Use MCPTools to manage both Game and Nsight. The normal workflow is headless:
start Game in **Release** with `nsight_gpu_trace`, attach the configured `nsight`
process to the returned PID, and let one deterministic Luau automation script
control the trace boundary through `NsightGPUTrace.Start()` and
`NsightGPUTrace.Stop()`.

Release is mandatory for every performance, occupancy, throughput, stall, and
bottleneck capture. Never substitute Debug, RelDebug, or Tracy. Non-Release
captures are allowed only for integration or correctness diagnosis, and their
performance measurements must not be reported.

For performance work, first run the identical settled workload in an
uninstrumented Release process and collect `performance_snapshot`. Treat its
`gpu_ms` as the timing baseline. Nsight's exported frame interval can include GPU
idle time; compare named RenderGraph/pass ranges with the baseline and use the
trace primarily for counters, stalls, occupancy, and workload attribution.

Do not launch Game through a shell, drive the Nsight UI, trigger captures with a
hotkey, or infer completion from elapsed time. Keep scene setup outside the trace,
use a unique ignored output directory, and require both the Game marker sequence
and Nsight's saved-report, export-success, and `Running result: 0` evidence.

Select the installed GPU architecture and metric set from `ngfx.exe --help-all`.
Use triage first and keep Nsight's base-clock lock for repeatability. Add
`shader_profiler` for shader-level questions and
`multi_pass` only when the workload is deterministic across passes and the extra
metrics are needed.

The CLI auto-export contains metrics, not the Analysis View's NVIDIA-authored
recommendations or severities. When the user asks what Nsight suggests, collect
a Release `multi_pass` trace with a deterministic three-frame workload, verify
the native report, and stop Game. Then use the general computer-control skill to
open the `.ngfx-gputrace` offline, inspect the Analysis View in Aggregate mode,
and transcribe each finding's severity, title, recommendation, and supporting
metric or range. Preserve screenshots when practical.

Explicitly distinguish NVIDIA Trace Analysis findings from your own conclusions.
Never present inferences from BASE exports as Nsight suggestions. If the UI or
Analysis View cannot be inspected, say that the recommendations were not
retrieved.

If the host fails to attach or select metrics, apply the runbook's clean-session
recovery once. Do not weaken diagnostics, modify Game integration, or switch to
UI automation until the verbose CLI evidence shows that the clean headless route
cannot proceed.
