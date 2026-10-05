# DFT Agent Studio

DFT Agent Studio is a native C++/Qt engineering workspace for configuring projects, running and supervising DFT workflows, and reviewing execution evidence. The repository contains the desktop application, agent runtime, storage layer, and interactive terminal client.

## Repository Layout

- `src/` contains the native application, agent, workflow, and storage services.
- `qml/` contains the interface and reusable components in `qml/components/`.
- `assets/` contains application fonts and icons.
- `packaging/` and `.github/workflows/` contain Linux packaging and CI definitions.

## Build

Requirements: CMake 3.21+, a C++20 compiler, Qt 6.5+, SQLite3, and zlib.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./run-dft-agent-studio.sh
```

The application data directory is `~/.dft_agent_studio`. On first launch, missing files from checkout-local `studio_data` are imported without overwriting existing user files. Empty bootstrap project settings are restored from the old configuration, and non-conflicting workspace database rows are merged. The original directory is preserved.

## Training Data

`dft-training-data` validates and combines reviewed supervised JSONL sources:

```sh
dft-training-data \
  --input feedback.jsonl \
  --input tool-trajectories.jsonl \
  --output build/training-mix.jsonl \
  --manifest build/training-mix.manifest.json
```

Rows are checked for the supported conversation roles and training policies, deduplicated by canonical JSON, and recorded with source and dataset SHA-256 digests. Invalid input aborts without committing a partial dataset.

`dft-training-shards` classifies and safely segments long tool trajectories using the exact loaded llama.cpp chat template and tokenizer. Start the configured `llama-server` first:

```sh
dft-training-shards \
  --dataset build/training-mix.jsonl \
  --output-dir build/training-stages \
  --llama-server http://127.0.0.1:8080 \
  --limits 768,2048,3000
```

Tool calls stay grouped with their results, prior assistant turns are retained as context, and the report records integrity checks, length distributions, output hashes, and anomalies. Each populated stage also gets a `.full_sequence.txt` corpus rendered with the same model template; this can feed the native llama.cpp trainer. `--report-only` skips stage and corpus files while preserving the analysis report.

`dft-training-preflight` replaces the former Transformers-based length audit. It reads the same reviewed JSONL format, measures every assistant decision with the active llama.cpp template/tokenizer, verifies that each full sequence starts with its prompt tokens, reports interpolated p50/p95/p99 lengths and the recommended power-of-two maximum, and never truncates samples:

```sh
dft-training-preflight \
  --dataset build/training-mix.jsonl \
  --output build/training-preflight.json \
  --llama-server http://127.0.0.1:8080 \
  --check-max-length 4096
```

The check exits with status 2 when any decision exceeds the requested maximum; omit `--check-max-length` to measure only.

Linux packages also include `dft-model-finetune`, the pinned llama.cpp C++ `llama-finetune` executable. It provides a native, no-Python full-parameter GGUF fine-tuning path for compatible F32 models. The upstream trainer is experimental and is not a replacement for the former PEFT/LoRA SFT pipeline: it does not currently preserve assistant-only loss masks, quantized LoRA training, or the former checkpoint semantics. Safetensors/LoRA conversion remains an external workflow; do not use this command on a quantized inference model or treat a successful process exit as model-quality validation.

It accepts a plain-text corpus, not JSONL directly. Use one populated corpus emitted by `dft-training-shards`:

```sh
dft-model-finetune \
  --model base-f32.gguf \
  --file build/training-stages/stage_01_1_768.full_sequence.txt \
  --output-file tuned-f32.gguf \
  --epochs 2
```

Evaluation sets can be checked separately, including case-ID and project isolation from training data:

```sh
dft-evaluation-data \
  --holdout holdout.jsonl \
  --training build/training-mix.jsonl \
  --training-project secworks_aes \
  --dataset-kind holdout \
  --manifest build/holdout.manifest.json
```

Use `--dataset-kind development` for tuning sets; their manifest explicitly marks them as not suitable for final acceptance.

Reviewed preference pairs can be validated and fingerprinted before any preference-training experiment:

```sh
dft-preference-data \
  --dataset reviewed-preferences.jsonl \
  --manifest build/preferences.manifest.json
```

Each row must contain non-empty `prompt`, `chosen`, and `rejected` message arrays; prompts end with a user message, both answers end with an assistant message, and `metadata.source` plus unique `metadata.case` values are required.

DFT runtime event histories can be audited without exposing raw prompts, source text, or credentials:

```sh
dft-session-audit \
  ~/.dft_agent_studio/agent_runtime/threads/<session-id>/<session-id>.events.bin \
  --output build/session-audit.json
```

The report separates actual tool calls from guard events, lists recorded patch outcomes, and extracts DRC observations from trusted DFT tool results. It reports recorded evidence only and does not infer filesystem changes that are absent from the event log. JSONL and JSON-array event files are also supported.

The repository is licensed under the MIT License. See [LICENSE](LICENSE).

Copyright (c) 2026 Little-W
