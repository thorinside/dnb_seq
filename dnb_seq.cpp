/*
MIT License

Copyright (c) 2025 Thorinside

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <cstdlib> // For rand()
#include <cstring> // For memcpy, memset
#include <ctime>   // For time()
#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <new>

// --- Data Structures ---

const int MAX_STEPS = 32; // The longest pattern has 32 steps

// Holds the sequence for each of the four drum tracks using fixed-size arrays.
struct DrumPattern {
    bool kick[MAX_STEPS];
    bool snare[MAX_STEPS];
    bool hihat[MAX_STEPS];
    bool ghostSnare[MAX_STEPS];
    int steps; // Number of steps in the pattern
};

// Main algorithm state, stored in DTC memory for persistence.
struct _DnbSeqAlgorithm_DTC {
    DrumPattern currentPattern;
    DrumPattern basePattern; // The original, unmodified pattern
    int currentStep;
    int pulsesPerStep; // Pulses per 16th note = 6 for 24ppqn
    int pulseCount;

    bool clockHigh;
    bool resetHigh;

    // Pattern queue state
    int queuedPatternId; // -1 = no pattern queued
    bool patternChangeQueued;

    // Counters for gate duration
    int kickTriggerSamples;
    int snareTriggerSamples;
    int hihatTriggerSamples;
    int ghostTriggerSamples;

    // Custom UI state
    int currentSeed;
    float bdProbability; // 0.0-1.0
    float snareProbability; // 0.0-1.0
    float ghostProbability; // 0.0-1.0
    // Note: HH probability stays at 1.0 (backbone)
};

// The main algorithm class, stored in SRAM.
struct _DnbSeqAlgorithm : public _NT_algorithm {
    _DnbSeqAlgorithm(_DnbSeqAlgorithm_DTC *dtc_ptr) : dtc(dtc_ptr) {
    }

    _DnbSeqAlgorithm_DTC *dtc;

    // Helper functions to manage patterns
    void generatePattern(int patternId);

    void generateVariation();

    void generateVariationWithSeed(int seed);

    void resetToDefault();
};

// --- Parameter Definitions ---
enum {
    // Inputs
    kParamClockInput,
    kParamResetInput,

    // Outputs
    kParamKickOutput,
    kParamSnareOutput,
    kParamHihatOutput,
    kParamGhostSnareOutput,

    // Pattern Controls
    kParamPatternSelect,
    kParamGenerateVariation,
    kParamResetToDefault,
};

// Enum strings for the pattern selection
static char const *const enumStringsPatterns[] = {
    "Two-Step", "Delayed Two-Step", "Steppa", "Stompa",
    "Dance Hall", "Dimension UK", "Halftime", "Triplet Two-Step",
    "Amen Break", "Neurofunk", nullptr
};

// Pattern names for display (without NULL terminator)
static const char *const patternNames[] = {
    "Two-Step", "Delayed Two-Step", "Steppa", "Stompa",
    "Dance Hall", "Dimension UK", "Halftime", "Triplet Two-Step",
    "Amen Break", "Neurofunk"
};

// All parameters for the plugin
static const _NT_parameter parameters[] = {
    NT_PARAMETER_CV_INPUT("Clock In", 1, 1)
    NT_PARAMETER_CV_INPUT("Reset In", 0, 0)

    NT_PARAMETER_CV_OUTPUT("Kick Out", 1, 1)
    NT_PARAMETER_CV_OUTPUT("Snare Out", 1, 2)
    NT_PARAMETER_CV_OUTPUT("Hi-hat Out", 1, 3)
    NT_PARAMETER_CV_OUTPUT("Ghost Snare Out", 1, 4)

    {
        .name = "Pattern",
        .min = 0,
        .max = 9,
        .def = 0,
        .unit = kNT_unitEnum,
        .scaling = 0,
        .enumStrings = enumStringsPatterns
    },
    {
        .name = "Vary Pattern",
        .min = 0,
        .max = 1,
        .def = 0,
        .unit = kNT_unitEnum,
        .scaling = 0,
        .enumStrings = (char const *const[]){"Off", "Trigger", nullptr}
    },
    {
        .name = "Reset Pattern",
        .min = 0,
        .max = 1,
        .def = 0,
        .unit = kNT_unitEnum,
        .scaling = 0,
        .enumStrings = (char const *const[]){"Off", "Trigger", nullptr}
    },
};

// Parameter Pages for the UI
static const uint8_t page1[] = {kParamPatternSelect};
static const uint8_t page2[] = {kParamGenerateVariation, kParamResetToDefault};
static const uint8_t page3[] = {
    kParamClockInput, kParamResetInput,
    kParamKickOutput, kParamSnareOutput,
    kParamHihatOutput, kParamGhostSnareOutput
};

static const _NT_parameterPage pages[] = {
    {.name = "Pattern", .numParams = ARRAY_SIZE(page1), .params = page1},
    {.name = "Modify", .numParams = ARRAY_SIZE(page2), .params = page2},
    {.name = "Routing", .numParams = ARRAY_SIZE(page3), .params = page3},
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// --- Pattern Generation Functions ---

// Creates a pattern based on an ID
void _DnbSeqAlgorithm::generatePattern(int patternId) {
    DrumPattern p;
    memset(&p, 0, sizeof(p)); // Clear the entire struct, setting arrays to false
    p.steps = 16; // Default for most patterns

    switch (patternId) {
        case 0: {
            // Two-Step
            const bool pat_k[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
            const bool pat_s[] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0};
            const bool pat_h[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 1: {
            // Delayed Two-Step
            const bool pat_k[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
            const bool pat_s[] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
            const bool pat_h[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 2: {
            // Steppa
            const bool pat_k[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            const bool pat_s[] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
            const bool pat_h[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
            const bool pat_g[] = {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            memcpy(p.ghostSnare, pat_g, sizeof(pat_g));
            break;
        }
        case 3: {
            // Stompa
            const bool pat_k[] = {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
            const bool pat_s[] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
            const bool pat_h[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
            const bool pat_g[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            memcpy(p.ghostSnare, pat_g, sizeof(pat_g));
            break;
        }
        case 4: {
            // Dance Hall
            const bool pat_k[] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            const bool pat_s[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0};
            const bool pat_h[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 5: {
            // Dimension UK (double length)
            p.steps = 32;
            const bool pat_k[] = {
                1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0
            };
            const bool pat_s[] = {
                0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
            };
            const bool pat_h[] = {
                1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
                1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0
            };
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 6: {
            // Halftime
            const bool pat_k[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            const bool pat_s[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
            const bool pat_h[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 7: {
            // Triplet Two-Step
            p.steps = 24;
            const bool pat_k[] = {
                1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
                1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0
            };
            const bool pat_s[] = {
                0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0
            };
            const bool pat_h[] = {
                1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
                1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0
            };
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 8: {
            // Amen Break
            const bool pat_k[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
            const bool pat_s[] = {0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0};
            const bool pat_h[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
            const bool pat_g[] = {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            memcpy(p.ghostSnare, pat_g, sizeof(pat_g));
            break;
        }
        case 9: {
            // Neurofunk
            const bool pat_k[] = {1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0};
            const bool pat_s[] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0};
            const bool pat_h[] = {1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0};
            const bool pat_g[] = {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            memcpy(p.ghostSnare, pat_g, sizeof(pat_g));
            break;
        }
        default: break;
    }

    dtc->basePattern = p;
    dtc->currentPattern = p;
}

// Generates a random variation of the current pattern
void _DnbSeqAlgorithm::generateVariation() {
    DrumPattern variation = dtc->basePattern; // Start from the clean base pattern
    int track = rand() % 4;
    int position = rand() % variation.steps;

    // Don't change main snare hits on beats 2 and 4 to keep the backbeat
    bool isMainSnare =
            (position == 4 || position == 12 || position == 20 || position == 28) &&
            track == 1;

    if (!isMainSnare) {
        switch (track) {
            case 0:
                variation.kick[position] = !variation.kick[position];
                break;
            case 1:
                variation.snare[position] = !variation.snare[position];
                break;
            case 2:
                variation.hihat[position] = !variation.hihat[position];
                break;
            case 3:
                variation.ghostSnare[position] = !variation.ghostSnare[position];
                break;
        }
    }
    dtc->currentPattern = variation;
}

// Generates a variation using a specific seed and probability controls
void _DnbSeqAlgorithm::generateVariationWithSeed(int seed) {
    DrumPattern variation = dtc->basePattern; // Start from the clean base pattern

    // Set seed for deterministic variations
    srand(seed);

    // Apply multiple random changes based on probabilities
    for (int i = 0; i < 4; i++) {
        // Apply a few random changes
        int track = rand() % 4;
        int position = rand() % variation.steps;

        // Don't change main snare hits on beats 2 and 4 to keep the backbeat
        bool isMainSnare =
                (position == 4 || position == 12 || position == 20 || position == 28) &&
                track == 1;

        if (!isMainSnare) {
            float probability = 1.0f;
            bool shouldChange = false;

            switch (track) {
                case 0: // Kick
                    probability = dtc->bdProbability;
                    shouldChange = (rand() / (float) RAND_MAX) < probability;
                    if (shouldChange) {
                        variation.kick[position] = !variation.kick[position];
                    }
                    break;
                case 1: // Snare
                    probability = dtc->snareProbability;
                    shouldChange = (rand() / (float) RAND_MAX) < probability;
                    if (shouldChange) {
                        variation.snare[position] = !variation.snare[position];
                    }
                    break;
                case 2: // Hi-hat - always full probability (backbone)
                    variation.hihat[position] = !variation.hihat[position];
                    break;
                case 3: // Ghost snare
                    probability = dtc->ghostProbability;
                    shouldChange = (rand() / (float) RAND_MAX) < probability;
                    if (shouldChange) {
                        variation.ghostSnare[position] = !variation.ghostSnare[position];
                    }
                    break;
            }
        }
    }
    dtc->currentPattern = variation;
}

// Resets the pattern to its original state
void _DnbSeqAlgorithm::resetToDefault() {
    dtc->currentPattern = dtc->basePattern;
}

// --- Plugin API Functions ---

void calculateRequirements(_NT_algorithmRequirements &req,
                           const int32_t *specifications) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_DnbSeqAlgorithm);
    req.dram = 0;
    req.dtc = sizeof(_DnbSeqAlgorithm_DTC);
    req.itc = 0;
}

_NT_algorithm *construct(const _NT_algorithmMemoryPtrs &ptrs,
                         const _NT_algorithmRequirements &req,
                         const int32_t *specifications) {
    // Use placement new to construct the algorithm in the pre-allocated SRAM
    _DnbSeqAlgorithm *alg =
            new(ptrs.sram) _DnbSeqAlgorithm((_DnbSeqAlgorithm_DTC *) ptrs.dtc);
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;

    // Initialize state
    srand(NT_getCpuCycleCount()); // Seed RNG
    alg->dtc->currentStep = 0;
    alg->dtc->pulseCount = 0;
    alg->dtc->pulsesPerStep = 6;
    alg->dtc->clockHigh = false;
    alg->dtc->resetHigh = false;

    // Initialize pattern queue state
    alg->dtc->queuedPatternId = -1;
    alg->dtc->patternChangeQueued = false;

    // Initialize trigger counters
    alg->dtc->kickTriggerSamples = 0;
    alg->dtc->snareTriggerSamples = 0;
    alg->dtc->hihatTriggerSamples = 0;
    alg->dtc->ghostTriggerSamples = 0;

    // Initialize custom UI state
    alg->dtc->currentSeed = 0;
    alg->dtc->bdProbability = 1.0f;
    alg->dtc->snareProbability = 1.0f;
    alg->dtc->ghostProbability = 1.0f;

    // Generate initial pattern based on default parameter value
    alg->generatePattern(alg->v[kParamPatternSelect]);

    return alg;
}

void parameterChanged(_NT_algorithm *self, int p) {
    _DnbSeqAlgorithm *pThis = (_DnbSeqAlgorithm *) self;

    if (p == kParamPatternSelect) {
        // Queue the pattern change instead of applying immediately
        pThis->dtc->queuedPatternId = pThis->v[kParamPatternSelect];
        pThis->dtc->patternChangeQueued = true;
    } else if (p == kParamGenerateVariation) {
        if (pThis->v[kParamGenerateVariation] == 1) {
            pThis->generateVariation();
            // Reset trigger parameter
            NT_setParameterFromUi(NT_algorithmIndex(self),
                                  kParamGenerateVariation + NT_parameterOffset(), 0);
        }
    } else if (p == kParamResetToDefault) {
        if (pThis->v[kParamResetToDefault] == 1) {
            pThis->resetToDefault();
            // Reset trigger parameter
            NT_setParameterFromUi(NT_algorithmIndex(self),
                                  kParamResetToDefault + NT_parameterOffset(), 0);
        }
    }
}

// Helper to detect rising edge
static inline bool isRisingEdge(float sample, bool &state) {
    bool high = sample > 1.0f;
    bool rising = high && !state;
    state = high;
    return rising;
}

void step(_NT_algorithm *self, float *busFrames, int numFramesBy4) {
    _DnbSeqAlgorithm *pThis = (_DnbSeqAlgorithm *) self;
    _DnbSeqAlgorithm_DTC *dtc = pThis->dtc;
    int numFrames = numFramesBy4 * 4;

    // Get input and output busses
    float *clockIn = busFrames + (pThis->v[kParamClockInput] - 1) * numFrames;
    float *resetIn =
            pThis->v[kParamResetInput] > 0
                ? busFrames + (pThis->v[kParamResetInput] - 1) * numFrames
                : nullptr;

    float *kickOut = busFrames + (pThis->v[kParamKickOutput] - 1) * numFrames;
    float *snareOut = busFrames + (pThis->v[kParamSnareOutput] - 1) * numFrames;
    float *hihatOut = busFrames + (pThis->v[kParamHihatOutput] - 1) * numFrames;
    float *ghostSnareOut =
            busFrames + (pThis->v[kParamGhostSnareOutput] - 1) * numFrames;

    // Fixed 10ms gate length
    const int gateLengthSamples =
            (int) ((10.0f / 1000.0f) * NT_globals.sampleRate);

    // Per-sample processing
    for (int i = 0; i < numFrames; ++i) {
        // --- 1. Handle advancing the sequencer based on clock/reset ---
        if (resetIn && isRisingEdge(resetIn[i], dtc->resetHigh)) {
            dtc->currentStep = 0;
            dtc->pulseCount = 0;
        }

        if (isRisingEdge(clockIn[i], dtc->clockHigh)) {
            dtc->pulseCount++;
            if (dtc->pulseCount >= dtc->pulsesPerStep) {
                dtc->pulseCount = 0;
                dtc->currentStep = (dtc->currentStep + 1) % dtc->currentPattern.steps;

                // Check for queued pattern change at the start of a new pattern cycle
                if (dtc->currentStep == 0 && dtc->patternChangeQueued) {
                    pThis->generatePattern(dtc->queuedPatternId);
                    dtc->patternChangeQueued = false;
                    dtc->queuedPatternId = -1;
                }

                // --- 2. Reset all trigger counters, then set them if there's a trigger
                // on this step ---
                dtc->kickTriggerSamples = 0;
                dtc->snareTriggerSamples = 0;
                dtc->hihatTriggerSamples = 0;
                dtc->ghostTriggerSamples = 0;

                if (dtc->currentPattern.kick[dtc->currentStep]) {
                    dtc->kickTriggerSamples = gateLengthSamples;
                }
                if (dtc->currentPattern.snare[dtc->currentStep]) {
                    dtc->snareTriggerSamples = gateLengthSamples;
                }
                if (dtc->currentPattern.hihat[dtc->currentStep]) {
                    dtc->hihatTriggerSamples = gateLengthSamples;
                }
                if (dtc->currentPattern.ghostSnare[dtc->currentStep]) {
                    dtc->ghostTriggerSamples = gateLengthSamples;
                }
            }
        }

        // --- 3. Process active gates and set output voltages ---
        if (dtc->kickTriggerSamples > 0) {
            kickOut[i] = 5.0f;
            dtc->kickTriggerSamples--;
        } else {
            kickOut[i] = 0.0f;
        }

        if (dtc->snareTriggerSamples > 0) {
            snareOut[i] = 5.0f;
            dtc->snareTriggerSamples--;
        } else {
            snareOut[i] = 0.0f;
        }

        if (dtc->hihatTriggerSamples > 0) {
            hihatOut[i] = 5.0f;
            dtc->hihatTriggerSamples--;
        } else {
            hihatOut[i] = 0.0f;
        }

        if (dtc->ghostTriggerSamples > 0) {
            ghostSnareOut[i] = 5.0f;
            dtc->ghostTriggerSamples--;
        } else {
            ghostSnareOut[i] = 0.0f;
        }
    }
}

bool draw(_NT_algorithm *self) {
    _DnbSeqAlgorithm *pThis = (_DnbSeqAlgorithm *) self;
    _DnbSeqAlgorithm_DTC *dtc = pThis->dtc;

    // Draw the current pattern state
    if (dtc->currentPattern.steps == 0)
        return true; // Avoid division by zero

    // Draw plugin title (avoiding dead zone above y=15)
    NT_drawText(2, 20, "DnB Seq", 15, kNT_textLeft, kNT_textTiny);

    // Draw current pattern name on second line
    int patternId = pThis->v[kParamPatternSelect];
    if (patternId >= 0 && patternId < 10) {
        NT_drawText(2, 26, patternNames[patternId], 15, kNT_textLeft, kNT_textTiny);
    }

    // Define margins and calculate adjusted dimensions (two-line header)
    const int margin = 6;
    const int titleHeight = 12; // Space for two-line header
    const int displayWidth = 256;
    const int displayHeight = 64;
    const int usableWidth = displayWidth - (2 * margin); // 236px
    const int usableHeight = displayHeight - (2 * margin) - titleHeight; // Space for titles
    const int trackHeight = usableHeight / 4; // Height per track
    const int labelWidth = 35; // Increased space for full track names
    const int gridWidth = usableWidth - labelWidth; // Width available for step grid
    int stepWidth = gridWidth / dtc->currentPattern.steps;

    for (int track = 0; track < 4; ++track) {
        const bool *patternTrack = nullptr;
        const char *trackName = nullptr;
        int trackColor = 15; // Default color

        // Text positioning: margin + small offset, track area + better vertical centering
        int textX = margin + 2;
        int textY = margin + titleHeight + track * trackHeight + trackHeight - 2; // Align with track area

        switch (track) {
            case 0:
                trackName = "KICK";
                trackColor = 3; // Darker color for kick
                patternTrack = dtc->currentPattern.kick;
                break;
            case 1:
                trackName = "SNARE";
                trackColor = 5; // Darker color for snare
                patternTrack = dtc->currentPattern.snare;
                break;
            case 2:
                trackName = "HIHAT";
                trackColor = 7; // Darker color for hihat
                patternTrack = dtc->currentPattern.hihat;
                break;
            case 3:
                trackName = "GHOST";
                trackColor = 9; // Darker color for ghost snare
                patternTrack = dtc->currentPattern.ghostSnare;
                break;
        }

        NT_drawText(textX, textY, trackName, trackColor);

        // Draw subtle horizontal separator line between tracks (except last one)
        if (track < 3) {
            int separatorY = margin + titleHeight + (track + 1) * trackHeight - 1;
            NT_drawShapeI(kNT_line, margin, separatorY,
                          margin + usableWidth, separatorY, 7);
        }

        if (patternTrack) {
            for (int step = 0; step < dtc->currentPattern.steps; ++step) {
                // Step grid positioning: margin + label space + step offset, margin + title + track offset
                int x = margin + labelWidth + step * stepWidth;
                int y = margin + titleHeight + track * trackHeight;

                // Draw background grid for all steps (darker outline)
                NT_drawShapeI(kNT_box, x, y, x + stepWidth - 2, y + trackHeight - 2, 1);

                // Draw active steps with track-specific colors
                if (patternTrack[step]) {
                    NT_drawShapeI(kNT_rectangle, x + 1, y + 1, x + stepWidth - 3,
                                  y + trackHeight - 3, trackColor);
                }

                // Draw current step indicator with bright highlight
                if (step == dtc->currentStep) {
                    NT_drawShapeI(kNT_box, x, y, x + stepWidth - 2, y + trackHeight - 2,
                                  15);
                }
            }
        }
    }

    return true; // Hide default parameter line
}

// --- Custom UI Functions ---

uint32_t hasCustomUi(_NT_algorithm *self) {
    return kNT_encoderL | kNT_encoderR | kNT_encoderButtonL | kNT_encoderButtonR |
           kNT_potButtonC | kNT_potL | kNT_potR;
}

void customUi(_NT_algorithm *self, const _NT_uiData &data) {
    _DnbSeqAlgorithm *pThis = (_DnbSeqAlgorithm *) self;

    // Right encoder: Change seed and generate variation
    if (data.encoders[1] != 0) {
        pThis->dtc->currentSeed += data.encoders[1];
        pThis->generateVariationWithSeed(pThis->dtc->currentSeed);
    }

    // Right encoder button: Reset pattern to default
    if ((data.controls & kNT_encoderButtonR) && !(data.lastButtons & kNT_encoderButtonR)) {
        pThis->resetToDefault();
    }

    // Left encoder: Change pattern
    if (data.encoders[0] != 0) {
        int currentPattern = pThis->v[kParamPatternSelect];
        currentPattern += data.encoders[0];
        if (currentPattern < 0) currentPattern = 9;
        if (currentPattern > 9) currentPattern = 0;
        NT_setParameterFromUi(NT_algorithmIndex(self), kParamPatternSelect + NT_parameterOffset(), currentPattern);
    }

    // Left encoder button: Reset pattern to default
    if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL)) {
        pThis->resetToDefault();
    }

    // Center pot button: Exit
    if ((data.controls & kNT_potButtonC) && !(data.lastButtons & kNT_potButtonC)) {
    }

    // Left pot: BD probability
    if (data.controls & kNT_potL) {
        pThis->dtc->bdProbability = data.pots[0];
    }

    // Right pot: Snare and Ghost probability (split the pot range)
    if (data.controls & kNT_potR) {
        float potValue = data.pots[2];
        if (potValue < 0.5f) {
            // First half controls snare probability
            pThis->dtc->snareProbability = potValue * 2.0f;
            pThis->dtc->ghostProbability = 1.0f;
        } else {
            // Second half controls ghost probability
            pThis->dtc->snareProbability = 1.0f;
            pThis->dtc->ghostProbability = (potValue - 0.5f) * 2.0f;
        }
    }
}

void setupUi(_NT_algorithm *self, _NT_float3 &pots) {
    _DnbSeqAlgorithm *pThis = (_DnbSeqAlgorithm *) self;
    pots[0] = pThis->dtc->bdProbability;
    // For right pot, we need to determine which half based on current values
    if (pThis->dtc->snareProbability < 1.0f) {
        pots[2] = pThis->dtc->snareProbability * 0.5f;
    } else {
        pots[2] = 0.5f + (pThis->dtc->ghostProbability * 0.5f);
    }
    pots[1] = 0.5f; // Center pot has no stored value
}

// --- Factory and Plugin Entry ---

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('T', 'h', 'D', 'B'),
    .name = "DnB Seq",
    .description = "Drum & Bass Sequencer",
    .numSpecifications = 0,
    .specifications = NULL,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
    .hasCustomUi = hasCustomUi,
    .customUi = customUi,
    .setupUi = setupUi,
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:
            return kNT_apiVersionCurrent;
        case kNT_selector_numFactories:
            return 1;
        case kNT_selector_factoryInfo:
            return (uintptr_t) ((data == 0) ? &factory : NULL);
    }
    return 0;
}
