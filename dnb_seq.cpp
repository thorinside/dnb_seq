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

#include <new>
#include <cstdlib> // For rand()
#include <ctime>   // For time()
#include <cstring> // For memcpy, memset
#include <distingnt/api.h>
#include <distingnt/serialisation.h>

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
struct _DnbSeqAlgorithm_DTC
{
    DrumPattern currentPattern;
    DrumPattern basePattern; // The original, unmodified pattern
    int currentStep;
    int pulsesPerStep; // Pulses per 16th note = 6 for 24ppqn
    int pulseCount;

    bool clockHigh;
    bool resetHigh;

    // Counters for gate duration and groove delay
    int kickTriggerSamples;
    int snareTriggerSamples;
    int hihatTriggerSamples;
    int ghostTriggerSamples;

    int kickPendingDelay;
    int snarePendingDelay;
};

// The main algorithm class, stored in SRAM.
struct _DnbSeqAlgorithm : public _NT_algorithm
{
    _DnbSeqAlgorithm(_DnbSeqAlgorithm_DTC* dtc_ptr) : dtc(dtc_ptr) {}
    _DnbSeqAlgorithm_DTC* dtc;

    // Helper functions to manage patterns
    void generatePattern(int patternId);
    void generateVariation();
    void resetToDefault();
};


// --- Parameter Definitions ---
enum
{
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
    kParamGroove,
};

// Enum strings for the pattern selection
static char const * const enumStringsPatterns[] = {
    "Two-Step",
    "Delayed Two-Step",
    "Steppa",
    "Stompa",
    "Dance Hall",
    "Dimension UK",
    "Halftime",
    "Triplet Two-Step",
    "Amen Break",
    "Neurofunk",
    NULL
};

// All parameters for the plugin
static const _NT_parameter parameters[] = {
    NT_PARAMETER_CV_INPUT( "Clock In", 1, 1 )
    NT_PARAMETER_CV_INPUT( "Reset In", 0, 0 )
    
    NT_PARAMETER_CV_OUTPUT( "Kick Out", 1, 1 )
    NT_PARAMETER_CV_OUTPUT( "Snare Out", 1, 2 )
    NT_PARAMETER_CV_OUTPUT( "Hi-hat Out", 1, 3 )
    NT_PARAMETER_CV_OUTPUT( "Ghost Snare Out", 1, 4 ),

    { .name = "Pattern", .min = 0, .max = 9, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = enumStringsPatterns },
    { .name = "Vary Pattern", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = (char const *const[]){"Off", "Trigger", NULL} },
    { .name = "Reset Pattern", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = (char const *const[]){"Off", "Trigger", NULL} },
    { .name = "Groove", .min = 0, .max = 100, .def = 0, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL},
};

// Parameter Pages for the UI
static const uint8_t page1[] = { kParamPatternSelect, kParamGroove };
static const uint8_t page2[] = { kParamGenerateVariation, kParamResetToDefault };
static const uint8_t page3[] = { kParamClockInput, kParamResetInput, kParamKickOutput, kParamSnareOutput, kParamHihatOutput, kParamGhostSnareOutput };

static const _NT_parameterPage pages[] = {
    { .name = "Pattern", .numParams = ARRAY_SIZE(page1), .params = page1 },
    { .name = "Modify", .numParams = ARRAY_SIZE(page2), .params = page2 },
    { .name = "Routing", .numParams = ARRAY_SIZE(page3), .params = page3 },
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
        case 0: { // Two-Step (Corrected Hi-Hats)
            const bool pat_k[] = {1,0,0,0, 0,0,0,1, 0,0,0,0, 0,0,0,0};
            const bool pat_s[] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0};
            const bool pat_h[] = {1,0,1,0, 1,1,1,0, 1,0,1,0, 1,1,1,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 1: { // Delayed Two-Step
            const bool pat_k[] = {1,0,0,0, 0,0,0,1, 0,0,0,0, 0,0,0,0};
            const bool pat_s[] = {0,0,0,0, 0,1,0,0, 0,0,0,0, 1,0,0,0};
            const bool pat_h[] = {1,0,1,0, 1,1,1,0, 1,0,1,0, 1,1,1,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 2: { // Steppa (Corrected Ghost Snares)
            const bool pat_k[] = {1,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
            const bool pat_s[] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0};
            const bool pat_h[] = {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};
            const bool pat_g[] = {0,0,0,1, 0,1,0,0, 0,0,0,0, 0,1,0,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            memcpy(p.ghostSnare, pat_g, sizeof(pat_g));
            break;
        }
        case 3: { // Stompa
            const bool pat_k[] = {1,0,0,0, 0,1,0,0, 0,1,0,0, 0,1,0,0};
            const bool pat_s[] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0};
            const bool pat_h[] = {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 4: { // Dance Hall
            const bool pat_k[] = {1,0,0,0, 0,0,1,0, 0,0,0,0, 0,0,1,0};
            const bool pat_s[] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0};
            const bool pat_h[] = {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 5: { // Dimension UK (double length)
            p.steps = 32;
            const bool pat_k[] = {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0};
            const bool pat_s[] = {0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0, 0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0};
            const bool pat_h[] = {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0, 1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 6: { // Halftime (Corrected Ghost Snares)
            const bool pat_k[] = {1,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0};
            const bool pat_s[] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 0,0,0,0};
            const bool pat_h[] = {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};
            const bool pat_g[] = {0,0,1,0, 0,0,1,0, 0,0,0,0, 1,0,1,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            memcpy(p.ghostSnare, pat_g, sizeof(pat_g));
            break;
        }
        case 7: { // Triplet Two-Step
            p.steps = 24;
            const bool pat_k[] = {1,0,0,0,0,0, 1,0,0,0,0,0, 1,0,0,0,0,0, 1,0,0,0,0,0};
            const bool pat_s[] = {0,0,0,0,0,0, 1,0,0,0,0,0, 0,0,0,0,0,0, 1,0,0,0,0,0};
            const bool pat_h[] = {1,0,1,0,1,0, 1,0,1,0,1,0, 1,0,1,0,1,0, 1,0,1,0,1,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            break;
        }
        case 8: { // Amen Break
            const bool pat_k[] = {1,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0};
            const bool pat_s[] = {0,0,0,0, 1,0,0,1, 0,1,0,0, 1,0,0,0};
            const bool pat_h[] = {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};
            const bool pat_g[] = {0,0,0,0, 0,0,1,0, 0,0,0,0, 0,0,0,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            memcpy(p.ghostSnare, pat_g, sizeof(pat_g));
            break;
        }
        case 9: { // Neurofunk (Corrected Ghost Snares)
            const bool pat_k[] = {1,0,0,0, 0,1,0,0, 1,0,0,0, 0,1,0,0};
            const bool pat_s[] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0};
            const bool pat_h[] = {1,0,1,1, 1,0,1,0, 1,0,1,1, 1,0,1,0};
            const bool pat_g[] = {0,0,0,1, 0,0,0,0, 0,0,0,1, 0,0,0,0};
            memcpy(p.kick, pat_k, sizeof(pat_k));
            memcpy(p.snare, pat_s, sizeof(pat_s));
            memcpy(p.hihat, pat_h, sizeof(pat_h));
            memcpy(p.ghostSnare, pat_g, sizeof(pat_g));
            break;
        }
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
    bool isMainSnare = (position == 4 || position == 12 || position == 20 || position == 28) && track == 1;

    if (!isMainSnare) {
        switch (track) {
            case 0: variation.kick[position] = !variation.kick[position]; break;
            case 1: variation.snare[position] = !variation.snare[position]; break;
            case 2: variation.hihat[position] = !variation.hihat[position]; break;
            case 3: variation.ghostSnare[position] = !variation.ghostSnare[position]; break;
        }
    }
    dtc->currentPattern = variation;
}

// Resets the pattern to its original state
void _DnbSeqAlgorithm::resetToDefault() {
    dtc->currentPattern = dtc->basePattern;
}


// --- Plugin API Functions ---

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_DnbSeqAlgorithm);
    req.dram = 0;
    req.dtc = sizeof(_DnbSeqAlgorithm_DTC);
    req.itc = 0;
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications) {
    // Use placement new to construct the algorithm in the pre-allocated SRAM
    _DnbSeqAlgorithm* alg = new (ptrs.sram) _DnbSeqAlgorithm((_DnbSeqAlgorithm_DTC*)ptrs.dtc);
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;

    // Initialize state
    srand(NT_getCpuCycleCount()); // Seed RNG
    alg->dtc->currentStep = 0;
    alg->dtc->pulseCount = 0;
    alg->dtc->pulsesPerStep = 6; // 24ppqn / 4 (16th notes per beat) = 6
    alg->dtc->clockHigh = false;
    alg->dtc->resetHigh = false;

    // Initialize trigger counters
    alg->dtc->kickTriggerSamples = 0;
    alg->dtc->snareTriggerSamples = 0;
    alg->dtc->hihatTriggerSamples = 0;
    alg->dtc->ghostTriggerSamples = 0;
    alg->dtc->kickPendingDelay = 0;
    alg->dtc->snarePendingDelay = 0;

    // Generate initial pattern based on default parameter value
    alg->generatePattern(alg->v[kParamPatternSelect]);
    
    return alg;
}

void parameterChanged(_NT_algorithm* self, int p) {
    _DnbSeqAlgorithm* pThis = (_DnbSeqAlgorithm*)self;

    if (p == kParamPatternSelect) {
        pThis->generatePattern(pThis->v[kParamPatternSelect]);
        pThis->dtc->currentStep = 0; // Reset step on pattern change
    } else if (p == kParamGenerateVariation) {
        if (pThis->v[kParamGenerateVariation] == 1) {
            pThis->generateVariation();
            // Reset trigger parameter
            NT_setParameterFromUi(NT_algorithmIndex(self), kParamGenerateVariation + NT_parameterOffset(), 0);
        }
    } else if (p == kParamResetToDefault) {
        if (pThis->v[kParamResetToDefault] == 1) {
            pThis->resetToDefault();
            // Reset trigger parameter
            NT_setParameterFromUi(NT_algorithmIndex(self), kParamResetToDefault + NT_parameterOffset(), 0);
        }
    }
}

// Helper to detect rising edge
static inline bool isRisingEdge(float sample, bool& state) {
    bool high = sample > 1.0f;
    bool rising = high && !state;
    state = high;
    return rising;
}


void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    _DnbSeqAlgorithm* pThis = (_DnbSeqAlgorithm*)self;
    _DnbSeqAlgorithm_DTC* dtc = pThis->dtc;
    int numFrames = numFramesBy4 * 4;

    // Get input and output busses
    float* clockIn = busFrames + (pThis->v[kParamClockInput] - 1) * numFrames;
    float* resetIn = pThis->v[kParamResetInput] > 0 ? busFrames + (pThis->v[kParamResetInput] - 1) * numFrames : nullptr;
    
    float* kickOut = busFrames + (pThis->v[kParamKickOutput] - 1) * numFrames;
    float* snareOut = busFrames + (pThis->v[kParamSnareOutput] - 1) * numFrames;
    float* hihatOut = busFrames + (pThis->v[kParamHihatOutput] - 1) * numFrames;
    float* ghostSnareOut = busFrames + (pThis->v[kParamGhostSnareOutput] - 1) * numFrames;

    // Get groove amount and calculate delay
    float grooveAmount = pThis->v[kParamGroove] / 100.0f;
    int grooveDelaySamples = (int)(grooveAmount * (NT_globals.sampleRate / 1000.0f) * 10.0f); // up to 10ms delay

    // Fixed 10ms gate length
    const int gateLengthSamples = (int)((10.0f / 1000.0f) * NT_globals.sampleRate);

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
                
                // --- 2. Schedule triggers on a new step ---
                if (dtc->currentPattern.hihat[dtc->currentStep]) {
                    dtc->hihatTriggerSamples = gateLengthSamples;
                }
                if (dtc->currentPattern.ghostSnare[dtc->currentStep]) {
                    dtc->ghostTriggerSamples = gateLengthSamples;
                }

                // Schedule grooved tracks
                if (dtc->currentPattern.kick[dtc->currentStep]) {
                    if (grooveDelaySamples == 0) dtc->kickTriggerSamples = gateLengthSamples;
                    else dtc->kickPendingDelay = grooveDelaySamples;
                }
                if (dtc->currentPattern.snare[dtc->currentStep]) {
                    if (grooveDelaySamples == 0) dtc->snareTriggerSamples = gateLengthSamples;
                    else dtc->snarePendingDelay = grooveDelaySamples;
                }
            }
        }

        // --- 3. Process pending delays ---
         if (dtc->kickPendingDelay > 0) {
            dtc->kickPendingDelay--;
            if (dtc->kickPendingDelay == 0) {
                dtc->kickTriggerSamples = gateLengthSamples;
            }
        }
        if (dtc->snarePendingDelay > 0) {
            dtc->snarePendingDelay--;
            if (dtc->snarePendingDelay == 0) {
                dtc->snareTriggerSamples = gateLengthSamples;
            }
        }

        // --- 4. Process active gates and set output voltages ---
        kickOut[i] = (dtc->kickTriggerSamples > 0) ? 5.0f : 0.0f;
        if (dtc->kickTriggerSamples > 0) dtc->kickTriggerSamples--;

        snareOut[i] = (dtc->snareTriggerSamples > 0) ? 5.0f : 0.0f;
        if (dtc->snareTriggerSamples > 0) dtc->snareTriggerSamples--;

        hihatOut[i] = (dtc->hihatTriggerSamples > 0) ? 5.0f : 0.0f;
        if (dtc->hihatTriggerSamples > 0) dtc->hihatTriggerSamples--;

        ghostSnareOut[i] = (dtc->ghostTriggerSamples > 0) ? 5.0f : 0.0f;
        if (dtc->ghostTriggerSamples > 0) dtc->ghostTriggerSamples--;
    }
}


bool draw(_NT_algorithm* self) {
    _DnbSeqAlgorithm* pThis = (_DnbSeqAlgorithm*)self;
    _DnbSeqAlgorithm_DTC* dtc = pThis->dtc;

    // Draw the current pattern state
    if (dtc->currentPattern.steps == 0) return true; // Avoid division by zero
    int stepWidth = 256 / dtc->currentPattern.steps;
    int trackHeight = 64 / 4;

    for (int track = 0; track < 4; ++track) {
        const bool* patternTrack = nullptr;
        switch (track) {
            case 0: NT_drawText(2, track * trackHeight + 2, "K", 15); patternTrack = dtc->currentPattern.kick; break;
            case 1: NT_drawText(2, track * trackHeight + 2, "S", 15); patternTrack = dtc->currentPattern.snare; break;
            case 2: NT_drawText(2, track * trackHeight + 2, "H", 15); patternTrack = dtc->currentPattern.hihat; break;
            case 3: NT_drawText(2, track * trackHeight + 2, "G", 15); patternTrack = dtc->currentPattern.ghostSnare; break;
        }
        
        if (patternTrack) {
            for (int step = 0; step < dtc->currentPattern.steps; ++step) {
                int x = 12 + step * stepWidth;
                int y = track * trackHeight;
                if (patternTrack[step]) {
                    NT_drawShapeI(kNT_rectangle, x, y, x + stepWidth - 2, y + trackHeight - 2, 10);
                }
                if (step == dtc->currentStep) {
                    NT_drawShapeI(kNT_box, x, y, x + stepWidth - 2, y + trackHeight - 2, 15);
                }
            }
        }
    }

    return true; // Hide default parameter line
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
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:
            return kNT_apiVersionCurrent;
        case kNT_selector_numFactories:
            return 1;
        case kNT_selector_factoryInfo:
            return (uintptr_t)((data == 0) ? &factory : NULL);
    }
    return 0;
}

