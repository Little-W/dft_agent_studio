# DFT Case Study: Synchronous Reset Misclassified in Scan DRC

## Summary

An isolated `secworks_blake2s` workspace repeatedly reported 1,876 post-DFT clock violations (one C4 and 1,875 C26). The design RTL was not the cause. The generated Design Compiler Tcl declared `reset_n` as a DFT `Reset`, although every sequential block used `posedge clk` and tested `reset_n` synchronously inside the block. This constraint made the DFT flow treat the signal inconsistently with the implemented clocked data path.

The correction was to model `reset_n` as test data for the synchronous reset behavior instead of declaring it an asynchronous DFT reset. No RTL was changed. In a separate copy of the generated workspace, both pre-DFT and post-DFT DRC then reported zero violations. TestMAX traced both scan chains (938 and 937 cells), reported no scan DRC violations, and measured 99.62% stuck-fault test coverage using 293 internal patterns.

## Evidence and causal reasoning

- The failing fresh report at `20261005T165220Z_secworks_blake2s_0ad84cbe/flow/reports/post_dft_drc.rpt` identified `reset_n` as a clock: C4 said it could not capture data with other clocks off; C26 said it was used as data against capture clock `clk`. The report counted 1,875 stable DFFs affected.
- `flow/reports/dft_signal.rpt` listed `clk` as ScanMasterClock/MasterClock and `reset_n` as `Reset`.
- All sequential event controls in `flow/rtl/blake2s*.v` were `posedge clk`; reset was tested in the process body (`if (!reset_n)`). Thus it was synchronous reset data, not an asynchronous reset pin.
- The generated Tcl contained `set_dft_signal -view existing_dft -type Reset -active_state 0 -port {reset_n}`. A successful `mor1kx_smic40` flow in the same environment provided a useful contrast: it did not classify its synchronous reset as an existing DFT Reset and instead exposed reset as `TestData` in the specification view.
- A separate copy at `/media/6/Projects/DFT_agent_project_workspaces/manual_secworks_blake2s_sync_reset_test` changed only this DFT modeling. Fresh DC reports showed pre-DFT DRC = 0 and post-DFT DRC = 0, with all 1,875 sequential cells valid scan cells.
- TestMAX output in `flow/logs/agent_atpg.log` showed successful protocol simulation and scan-chain tracing, no DRC violations, 94,324 total faults, and 99.62% stuck-fault test coverage (293 patterns). This exceeded the 99.00% target.

The repair was intentionally performed in an isolated flow copy. The original RTL, project checkout, and the agent's existing workspace were not modified. The experiment removed the `existing_dft` `Reset` declaration, constrained the synchronous reset input from timing analysis, and declared it as `TestData` in the `spec` view so ATPG can control the synchronous reset input during test. The exact electrical polarity and test value must always be taken from the particular design; do not copy these values blindly to another project.

## Why the agent stalled

1. It recognized the repeated C4/C26 count but did not connect it to the DFT-signal report and the RTL event controls. It treated the count as a large set of design-level violations instead of testing whether all instances shared one constraint-classification cause.
2. It over-focused on repeated flow/optimization retries and did not form a falsifiable prediction such as “if `reset_n` is synchronous data rather than a DFT reset, the DRC should stop naming it as a clock and the repeated C26 count should collapse.”
3. The goal's protection of original RTL was misread as a ban on all RTL in the isolated workspace. This case needed no RTL edit, but the identity distinction still matters for future real RTL defects: protect the original tree, inspect and edit the authorized isolated copy when warranted.
4. Invalid `run_dft_iteration` parameter values were tool-validation failures, not evidence about the design. The agent needed to consult accepted tool values and change the request, not interpret the failed call as another DRC result.
5. A Responses API HTTP 502 interrupted a long turn. This is a service failure, not a DFT outcome. The correct recovery is to resume from the durable workspace and fresh reports, not assume the work completed or replay the same unchanged run.

## Reusable diagnostic method

For a repeated clock/reset DRC, inspect one representative violation, the DFT-signal report, the relevant cell pins/nets, and the RTL sensitivity list/reset coding. Decide whether the signal is a real clock, asynchronous set/reset, synchronous control data, or a configuration/tool artifact. Test the smallest appropriate constraint change in an isolated copy and predict the report change in advance. Preserve original source hashes and verify the effective run inputs. Then run fresh pre-DFT DRC, scan insertion/post-DFT DRC, TestMAX scan DRC, and ATPG; read the generated reports and compare them with the acceptance target. If the observation does not match the prediction, reject the hypothesis and investigate a different layer. Never turn a successful case into a hard-coded rule that every signal named `reset` should be `TestData`.

## Reproduction artifacts

- Original evidence: `/media/6/Projects/DFT_agent_project_workspaces/20261005T165220Z_secworks_blake2s_0ad84cbe/flow/reports/`
- Isolated successful experiment: `/media/6/Projects/DFT_agent_project_workspaces/manual_secworks_blake2s_sync_reset_test/flow/`
- Successful reports: `reports/pre_dft_drc.rpt`, `reports/post_dft_drc.rpt`, `reports/dft_signal.rpt`, and `logs/agent_atpg.log`
- RTL source: unchanged by this experiment
