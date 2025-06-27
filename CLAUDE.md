# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Drum & Bass sequencer plugin for the Disting NT platform, implemented as an embedded C++ audio plugin. The plugin generates drum patterns for kick, snare, hi-hat, and ghost snare tracks with various classic DnB rhythm patterns.

## Architecture

- **Single-file plugin**: `dnb_seq.cpp` contains the entire plugin implementation
- **Embedded target**: ARM Cortex-M7 microcontroller with strict memory constraints
- **Real-time audio processing**: Sample-accurate timing with per-sample processing loop
- **Memory layout**:
  - SRAM: Main algorithm class (`_DnbSeqAlgorithm`)
  - DTC: Persistent state data (`_DnbSeqAlgorithm_DTC`) that survives power cycles
- **Pattern system**: 10 predefined DnB patterns with algorithmic variation generation

## Build Commands

```bash
# Build the plugin object file
make all

# Clean build artifacts
make clean

# Build and validate the plugin (checks memory constraints and undefined symbols)
make check
```

## Key Constraints

- **Memory limits**: .bss section must not exceed 8 KiB (enforced by `make check`)
- **Cross-compilation**: Uses `arm-none-eabi-c++` for ARM Cortex-M7 target
- **No standard library**: Limited to basic C functions (memcpy, memset, rand)
- **Real-time constraints**: All processing must complete within audio buffer timeframe

## External Dependencies

- **NT_API_PATH**: Points to Disting NT API headers (defaults to `../../github/distingNT_API`)
- **distingnt/api.h**: Core plugin API definitions
- **distingnt/serialisation.h**: Parameter serialization support

## Pattern Implementation

The sequencer contains 10 hardcoded drum patterns:
- Two-Step, Delayed Two-Step, Steppa, Stompa
- Dance Hall, Dimension UK (32 steps), Halftime
- Triplet Two-Step (24 steps), Amen Break, Neurofunk

Each pattern defines boolean arrays for the four drum tracks, with algorithmic variation that randomly flips individual steps while preserving main backbeat elements.