# V9+ 1.8956 Lossless Compression Benchmark

This folder contains **V9+**, a portable C++17/C++20 lossless compression experiment targeting [Hutter Prize](https://prize.hutter1.net/)–style English/Wikipedia data. It combines 16 online bit-level prediction experts with a stretched-logit mixer, range arithmetic coding, and specialized XML, MediaWiki, word, stem, phonotactic, and match-context models.

The implementation is cold-start and online: it builds its context and learned weights while processing the input, with no external dictionaries or pretrained data.

## Contents

| File | Description |
| --- | --- |
| `main.cpp` | Benchmark harness, 32-bit range coder/decoder, expert models, logistic mixer, APM stages, and command-line interface. |
| `V9_LangMaskUpgrade.hpp` | Parser state and language-aware context features for XML, MediaWiki markup, entities, words, punctuation, morphology, and phonotactics. |
| `enwik9_750kb.txt` | 750,000-byte Wikipedia source used by the benchmark. |
| `output.txt` | Captured benchmark result for this folder. |

## Latest Benchmark Result

The recorded run in `output.txt` reports:

- **Source:** `enwik9_750kb.txt` (750,000 bytes)
- **Compressed size:** 177,725 bytes including the 8-byte original-length header
- **Model bpb:** 1.8956 bits per byte
- **Actual bpb:** 1.8957 bits per byte
- **Previous V8 reference:** 2.1638 bpb
- **Gain over V8:** 0.2681 bpb
- **Lossless verification:** PASS — exact source/decoded match
- **Runtime:** 57.64 seconds

> These figures describe one observed run on the provided data. Performance and compression can vary with compiler flags, CPU architecture, and input size.

## How It Works

### 1. Bit-level range coding

`main.cpp` implements 32-bit range arithmetic encoding and decoding. Each source byte is processed most-significant bit first, while the engine supplies a 12-bit probability for every bit.

### 2. Independent context experts

The engine contains 16 prediction experts, including:

- Order-0 through order-9 byte contexts
- A sparse 13-byte context
- Indirect context prediction
- Word-chain and word-skip models
- Shape-morphology and stem/suffix models
- XML/MediaWiki FSM context
- Phonotactic CV context
- Multi-match and fuzzy-gap match prediction

### 3. Stretched-logit logistic mixer

Each expert probability is converted to a stretched logit and combined with online SGD-style weight tables. Multiple context hashes select separate mixer weight rows.

### 4. Cascaded APM refinement

Three adaptive probability map (APM) stages refine the mixed probability using local byte, word, and match contexts.

### 5. Language-aware parser state

`V9_LangMaskUpgrade.hpp` tracks structural and linguistic context such as:

- XML tags, attributes, comments, entities, and closing-tag state
- MediaWiki links, templates, bold/italic markup, and extlinks
- Word boundaries, punctuation, capitalization, and recent words
- Prefixes and suffixes
- Morphology and consonant/vowel runs
- UTF-8 continuation state

## Build

Use a C++17 or C++20 compiler:

```bash
g++ -O3 -std=c++17 -march=native main.cpp -o v9_hutter
```

A portable build without architecture-specific tuning is also possible:

```bash
g++ -O3 -std=c++17 main.cpp -o v9_hutter
```

## Run

Run the benchmark on the included 750 KB input:

```bash
./v9_hutter --file enwik9_750kb.txt
```

Specify a different input file:

```bash
./v9_hutter --file path/to/input.txt
```

Limit an input to the first N bytes:

```bash
./v9_hutter --file enwik9_750kb.txt 100000
```

The harness accepts a numeric argument for a byte limit and `--encode-only` to skip the decode-and-compare verification step:

```bash
./v9_hutter --encode-only --file enwik9_750kb.txt
```

`--cold` is accepted for compatibility and does not alter the engine, because V9+ is always initialized in pure cold-start online mode.

## Output

A successful run reports:

- Source name and source byte count
- Encoded byte count, including the 8-byte length header
- Model and actual bits per byte
- Reference gain or loss relative to V8
- Encoding time and throughput
- Lossless decode verification status
- Standalone bpb for each expert
- Per-bucket telemetry for:
  - XML tags
  - MediaWiki markup
  - Word-start text
  - Mid-word text

## Requirements

- C++17 or newer
- A standard C++ compiler such as GCC or Clang
- A binary-safe input file

No external libraries or runtime dependencies are required.

## Notes

- Encoding and decoding use the same deterministic parser and online model state.
- The 8-byte header stores the original input length as a little-endian unsigned integer.
- The range decoder reads beyond the compressed stream by returning 0 when the input buffer is exhausted.
- The benchmark verifies decoded bytes against the original input unless `--encode-only` is selected.

## License

See the repository-level `LICENSE` file for licensing terms.