/*
 * Mutated Sequences - MIDI Rhythm Sequencer
 * 8-step sequencer with pitch, velocity, and gate per step
 * 
 * Copyright (C) 2025 ZynMI
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define MUTATED_SEQ_URI "https://github.com/PatttF/zynMI/plugins/mutated_sequences"

#define NUM_STEPS 8

// Port indices
typedef enum {
    PORT_MIDI_IN = 0,
    PORT_MIDI_OUT,
    PORT_CLOCK_SOURCE,     // 0=Internal, 1=MIDI Clock, 2=Host Tempo
    PORT_BPM,              // Internal clock BPM
    PORT_CLOCK_DIV,        // Clock division: 1/4, 1/8, 1/16, 1/32
    PORT_SWING,            // 0-100% swing amount
    PORT_GATE_LENGTH,      // Gate length as % of step
    PORT_NUM_STEPS,        // Active steps (1-8)
    PORT_TRANSPOSE,        // Global transpose (-24 to +24 semitones)
    PORT_RUNNING,          // Run/Stop
    // New creative controls
    PORT_PATTERN,          // Rhythm pattern/formula (0-9)
    PORT_PATTERN_PARAM,    // Pattern parameter/tweaker (0-100)
    PORT_VELOCITY_MODE,    // Velocity mode: Manual, Accent, Ramp Up, Ramp Down, Random
    PORT_VELOCITY_AMOUNT,  // Amount for velocity modes (0-100%)
    PORT_PITCH_MODE,       // Pitch mode: Manual, Euclidean, Pentatonic, Chromatic
    PORT_PITCH_SPREAD,     // Pitch spread/range for generative modes
    PORT_PROBABILITY,      // Gate probability per step (0-100%)
    PORT_HUMANIZE,         // Timing humanization (0-100%)
    PORT_MUTATE,           // Mutate amount (0-100%) - randomly changes parameters on sequence loop
    // Per-step parameters (8 steps × 4 params = 32 ports)
    PORT_STEP1_PITCH,         // MIDI note (0-127)
    PORT_STEP1_VELOCITY,      // Velocity (0-127)
    PORT_STEP1_PROBABILITY,   // Step probability (0-100%)
    PORT_STEP1_RATCHET,       // Ratchet count (1-8)
    PORT_STEP2_PITCH,
    PORT_STEP2_VELOCITY,
    PORT_STEP2_PROBABILITY,
    PORT_STEP2_RATCHET,
    PORT_STEP3_PITCH,
    PORT_STEP3_VELOCITY,
    PORT_STEP3_PROBABILITY,
    PORT_STEP3_RATCHET,
    PORT_STEP4_PITCH,
    PORT_STEP4_VELOCITY,
    PORT_STEP4_PROBABILITY,
    PORT_STEP4_RATCHET,
    PORT_STEP5_PITCH,
    PORT_STEP5_VELOCITY,
    PORT_STEP5_PROBABILITY,
    PORT_STEP5_RATCHET,
    PORT_STEP6_PITCH,
    PORT_STEP6_VELOCITY,
    PORT_STEP6_PROBABILITY,
    PORT_STEP6_RATCHET,
    PORT_STEP7_PITCH,
    PORT_STEP7_VELOCITY,
    PORT_STEP7_PROBABILITY,
    PORT_STEP7_RATCHET,
    PORT_STEP8_PITCH,
    PORT_STEP8_VELOCITY,
    PORT_STEP8_PROBABILITY,
    PORT_STEP8_RATCHET,
    PORT_COUNT
} PortIndex;

typedef struct {
    LV2_URID atom_Blank;
    LV2_URID atom_Float;
    LV2_URID atom_Object;
    LV2_URID atom_Path;
    LV2_URID atom_Resource;
    LV2_URID atom_Sequence;
    LV2_URID midi_Event;
    LV2_URID time_Position;
    LV2_URID time_barBeat;
    LV2_URID time_beatsPerMinute;
    LV2_URID time_speed;
} URIDs;

typedef struct {
    // Ports
    const LV2_Atom_Sequence* midi_in;
    LV2_Atom_Sequence* midi_out;
    
    const float* clock_source;
    const float* bpm;
    const float* clock_div;
    const float* swing;
    const float* gate_length;
    const float* num_steps;
    const float* transpose;
    const float* running;
    
    // New creative controls
    const float* pattern;
    const float* pattern_param;
    const float* velocity_mode;
    const float* velocity_amount;
    const float* pitch_mode;
    const float* pitch_spread;
    const float* probability;
    const float* humanize;
    const float* mutate;
    
    // Step parameters (arrays for easier indexing)
    const float* step_pitch[NUM_STEPS];
    const float* step_velocity[NUM_STEPS];
    const float* step_probability[NUM_STEPS];
    const float* step_ratchet[NUM_STEPS];
    
    // Mutated parameter storage (offset from original values)
    int mutated_pitch[NUM_STEPS];
    int mutated_velocity[NUM_STEPS];
    
    // URIDs
    LV2_URID_Map* map;
    URIDs uris;
    
    // Atom forge for writing MIDI
    LV2_Atom_Forge forge;
    LV2_Atom_Forge_Frame frame;
    
    // Sequencer state
    double sample_rate;
    uint32_t current_step;
    uint64_t samples_since_step;
    uint64_t samples_per_step;
    bool gate_active;
    uint8_t current_note;
    bool note_is_on;
    
    // Ratchet state
    int current_ratchet;
    int ratchet_count;
    uint64_t samples_per_ratchet;
    uint64_t samples_since_ratchet;
    bool step_is_active;  // Track if current step passed gate/pattern/probability checks
    
    // Host tempo tracking
    double host_bpm;
    bool host_playing;
    
    // MIDI clock tracking
    uint32_t midi_clock_count;      // Count of MIDI clock messages (24 per quarter note)
    uint64_t last_clock_time;       // Sample time of last clock message
    uint64_t clock_interval;        // Samples between clock messages
    bool midi_clock_running;        // MIDI clock start/stop state
    uint64_t sample_position;       // Track sample position for MIDI clock timing
    
    // Random state for generative features
    uint32_t rng_state;
} MutatedSequencer;

static void
map_uris(LV2_URID_Map* map, URIDs* uris)
{
    uris->atom_Blank          = map->map(map->handle, LV2_ATOM__Blank);
    uris->atom_Float          = map->map(map->handle, LV2_ATOM__Float);
    uris->atom_Object         = map->map(map->handle, LV2_ATOM__Object);
    uris->atom_Path           = map->map(map->handle, LV2_ATOM__Path);
    uris->atom_Resource       = map->map(map->handle, LV2_ATOM__Resource);
    uris->atom_Sequence       = map->map(map->handle, LV2_ATOM__Sequence);
    uris->midi_Event          = map->map(map->handle, LV2_MIDI__MidiEvent);
    uris->time_Position       = map->map(map->handle, LV2_TIME__Position);
    uris->time_barBeat        = map->map(map->handle, LV2_TIME__barBeat);
    uris->time_beatsPerMinute = map->map(map->handle, LV2_TIME__beatsPerMinute);
    uris->time_speed          = map->map(map->handle, LV2_TIME__speed);
}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
    MutatedSequencer* self = (MutatedSequencer*)calloc(1, sizeof(MutatedSequencer));
    if (!self) return NULL;
    
    self->sample_rate = rate;
    self->current_step = 0;
    self->samples_since_step = 0;
    self->gate_active = false;
    self->note_is_on = false;
    self->current_ratchet = 0;
    self->ratchet_count = 1;
    self->samples_per_ratchet = 0;
    self->samples_since_ratchet = 0;
    self->step_is_active = false;
    self->host_bpm = 120.0;
    self->host_playing = false;
    self->midi_clock_count = 0;
    self->last_clock_time = 0;
    self->clock_interval = 0;
    self->midi_clock_running = false;
    self->sample_position = 0;
    self->rng_state = 12345;  // Simple RNG seed
    
    // Initialize mutated parameters to 0 (no mutation)
    for (int i = 0; i < NUM_STEPS; i++) {
        self->mutated_pitch[i] = 0;
        self->mutated_velocity[i] = 0;
    }
    
    // Get URID map feature
    for (int i = 0; features[i]; i++) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            self->map = (LV2_URID_Map*)features[i]->data;
        }
    }
    
    if (!self->map) {
        free(self);
        return NULL;
    }
    
    map_uris(self->map, &self->uris);
    lv2_atom_forge_init(&self->forge, self->map);
    
    return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    MutatedSequencer* self = (MutatedSequencer*)instance;
    
    switch (port) {
        case PORT_MIDI_IN:
            self->midi_in = (const LV2_Atom_Sequence*)data;
            break;
        case PORT_MIDI_OUT:
            self->midi_out = (LV2_Atom_Sequence*)data;
            break;
        case PORT_CLOCK_SOURCE:
            self->clock_source = (const float*)data;
            break;
        case PORT_BPM:
            self->bpm = (const float*)data;
            break;
        case PORT_CLOCK_DIV:
            self->clock_div = (const float*)data;
            break;
        case PORT_SWING:
            self->swing = (const float*)data;
            break;
        case PORT_GATE_LENGTH:
            self->gate_length = (const float*)data;
            break;
        case PORT_NUM_STEPS:
            self->num_steps = (const float*)data;
            break;
        case PORT_TRANSPOSE:
            self->transpose = (const float*)data;
            break;
        case PORT_RUNNING:
            self->running = (const float*)data;
            break;
        case PORT_PATTERN:
            self->pattern = (const float*)data;
            break;
        case PORT_PATTERN_PARAM:
            self->pattern_param = (const float*)data;
            break;
        case PORT_VELOCITY_MODE:
            self->velocity_mode = (const float*)data;
            break;
        case PORT_VELOCITY_AMOUNT:
            self->velocity_amount = (const float*)data;
            break;
        case PORT_PITCH_MODE:
            self->pitch_mode = (const float*)data;
            break;
        case PORT_PITCH_SPREAD:
            self->pitch_spread = (const float*)data;
            break;
        case PORT_PROBABILITY:
            self->probability = (const float*)data;
            break;
        case PORT_HUMANIZE:
            self->humanize = (const float*)data;
            break;
        case PORT_MUTATE:
            self->mutate = (const float*)data;
            break;
        // Step parameters
        default:
            if (port >= PORT_STEP1_PITCH && port < PORT_COUNT) {
                int offset = port - PORT_STEP1_PITCH;
                int step = offset / 4;
                int param = offset % 4;
                
                if (step < NUM_STEPS) {
                    switch (param) {
                        case 0: self->step_pitch[step] = (const float*)data; break;
                        case 1: self->step_velocity[step] = (const float*)data; break;
                        case 2: self->step_probability[step] = (const float*)data; break;
                        case 3: self->step_ratchet[step] = (const float*)data; break;
                    }
                }
            }
            break;
    }
}

// Simple LCG random number generator
static uint32_t
next_random(uint32_t* state)
{
    *state = (*state * 1103515245 + 12345) & 0x7fffffff;
    return *state;
}

static float
random_float(uint32_t* state)
{
    return (float)next_random(state) / (float)0x7fffffff;
}

// Euclidean rhythm generator
static bool
euclidean_gate(int step, int num_steps, int pulses)
{
    if (pulses <= 0 || pulses > num_steps) return false;
    int bucket = step * pulses;
    return (bucket % num_steps) < pulses;
}

// Pattern generator - returns true if gate should be active
static bool
apply_pattern(int step, int num_steps, int pattern, float param)
{
    // param is 0-100, normalize to useful ranges
    switch (pattern) {
        case 0: // All steps (straight)
            return true;
            
        case 1: // Every other (8th notes)
            return (step % 2) == 0;
            
        case 2: // Quarter notes
            return (step % 4) == 0;
            
        case 3: // Euclidean (param controls density)
            {
                int pulses = (int)((param / 100.0f) * num_steps + 0.5f);
                if (pulses < 0) pulses = 0;
                if (pulses > num_steps) pulses = num_steps;
                if (pulses == 0) return false;  // No pulses = silence
                return euclidean_gate(step, num_steps, pulses);
            }
            
        case 4: // Syncopated (offbeat emphasis)
            return (step % 2) == 1;
            
        case 5: // Triplet feel (every 3rd)
            return (step % 3) == 0;
            
        case 6: // Gallop (param controls which beats)
            {
                int skip = (int)(param / 25.0f);  // 0-3
                return (step % 4) != skip;
            }
            
        case 7: // Fibonacci-inspired
            {
                int fib_mask[] = {1, 1, 0, 1, 0, 1, 1, 0};  // Fibonacci rhythm
                return fib_mask[step % 8];
            }
            
        case 8: // Random (param controls probability)
            // Creates a deterministic but seemingly random pattern based on step
            {
                // Use safe arithmetic to avoid overflow
                uint32_t step_seed = (12345u + (uint32_t)step * 7919u) & 0x7fffffffu;
                step_seed = (step_seed * 1103515245u + 12345u) & 0x7fffffffu;
                float roll = (float)step_seed / (float)0x7fffffff;
                return roll < (param / 100.0f);
            }
            
        case 9: // Clave pattern
            {
                // Son clave: X..X..X...X.X...
                int clave[] = {1, 0, 0, 1, 0, 0, 1, 0};
                return clave[step % 8];
            }
            
        case 10: // Four-on-floor (classic techno/house kick)
            return (step % 4) == 0;
            
        case 11: // Backbeat (snare on 2 and 4)
            return (step == 2) || (step == 6);
            
        case 12: // Hi-hat 16ths (param controls density)
            {
                // Create hi-hat patterns with varying density
                float threshold = 1.0f - (param / 100.0f);
                int patterns[] = {1, 0, 1, 0, 1, 0, 1, 0};  // Every other
                if (threshold < 0.33f) {
                    // Dense: all steps
                    return true;
                } else if (threshold < 0.66f) {
                    // Medium: every other
                    return patterns[step % 8];
                } else {
                    // Sparse: quarter notes
                    return (step % 4) == 0;
                }
            }
            
        case 13: // Breakbeat (classic amen-style)
            {
                // X.XX.X.X (boom-bap variation)
                int breakbeat[] = {1, 0, 1, 1, 0, 1, 0, 1};
                return breakbeat[step % 8];
            }
            
        case 14: // Tom pattern (tribal feel)
            {
                // X..X.X.X (param shifts pattern)
                int shift = (int)(param / 12.5f);  // 0-7
                int tom[] = {1, 0, 0, 1, 0, 1, 0, 1};
                return tom[(step + shift) % 8];
            }
            
        case 15: // Shuffle/Swing (swung 16ths)
            {
                // XX.XX.XX (swung groove)
                int shuffle[] = {1, 1, 0, 1, 1, 0, 1, 1};
                return shuffle[step % 8];
            }
            
        case 16: // Cowbell/Clave 2 (Rumba clave)
            {
                // X..X.X..X.X. (rumba clave)
                int rumba[] = {1, 0, 0, 1, 0, 1, 0, 0};
                return rumba[step % 8];
            }
            
        case 17: // Ride cymbal (jazz feel)
            {
                // X.XX.XX. (param adds variations)
                int ride[] = {1, 0, 1, 1, 0, 1, 1, 0};
                float threshold = param / 100.0f;
                if (step % 4 == 3 && threshold > 0.5f) {
                    return true;  // Add extra hits
                }
                return ride[step % 8];
            }
            
        case 18: // Percussion fill (param controls complexity)
            {
                // Increasingly complex patterns based on param
                float density = param / 100.0f;
                if (density < 0.33f) {
                    // Simple: X...X.X.
                    int simple[] = {1, 0, 0, 0, 1, 0, 1, 0};
                    return simple[step % 8];
                } else if (density < 0.66f) {
                    // Medium: X.X.X.XX
                    int medium[] = {1, 0, 1, 0, 1, 0, 1, 1};
                    return medium[step % 8];
                } else {
                    // Complex: XX.XXX.X
                    int complex[] = {1, 1, 0, 1, 1, 1, 0, 1};
                    return complex[step % 8];
                }
            }
            
        case 19: // Paradiddle (drum rudiment)
            {
                // RLRRLRLL pattern (alternating emphasis)
                // Strong beats on 1, 4, 6
                int paradiddle[] = {1, 0, 1, 1, 0, 1, 1, 0};
                return paradiddle[step % 8];
            }
            
        // === MELODIC/SYNTH LEAD PATTERNS (20-29) ===
        
        case 20: // Arpeggio Up (param controls step skip)
            {
                // Play every Nth step based on param
                int skip = 1 + (int)(param / 33.3f);  // 1, 2, or 3
                return (step % skip) == 0;
            }
            
        case 21: // Arpeggio Down (reverse)
            {
                int skip = 1 + (int)(param / 33.3f);
                return ((num_steps - 1 - step) % skip) == 0;
            }
            
        case 22: // Octave Jump (param controls octave pattern)
            {
                // Alternate between low and high octaves
                // Use with pitch mode for best results
                if (param < 33.3f) {
                    // Every other: X.X.X.X.
                    return (step % 2) == 0;
                } else if (param < 66.6f) {
                    // 1 low, 2 high: X.XX....
                    int oct[] = {1, 0, 1, 1, 0, 0, 0, 0};
                    return oct[step % 8];
                } else {
                    // Complex: XX..X..X
                    int oct[] = {1, 1, 0, 0, 1, 0, 0, 1};
                    return oct[step % 8];
                }
            }
            
        case 23: // Staccato Melody (short rhythmic bursts)
            {
                // XX..XX....XX..XX (punchy articulation)
                int staccato[] = {1, 1, 0, 0, 1, 1, 0, 0};
                return staccato[step % 8];
            }
            
        case 24: // Legato Flow (sustained notes)
            {
                // XXX.XXX. (flowing melodic lines)
                int legato[] = {1, 1, 1, 0, 1, 1, 1, 0};
                return legato[step % 8];
            }
            
        case 25: // Call & Response (param shifts response)
            {
                int shift = (int)(param / 12.5f);  // 0-7
                // Call: XX.. Response: ..XX
                int call_resp[] = {1, 1, 0, 0, 0, 0, 1, 1};
                return call_resp[(step + shift) % 8];
            }
            
        case 26: // Dotted Rhythm (synth stabs)
            {
                // X..X..X. (dotted 8th feel)
                int dotted[] = {1, 0, 0, 1, 0, 0, 1, 0};
                return dotted[step % 8];
            }
            
        case 27: // Trance Gate (param controls gate pattern)
            {
                // Progressive trance-style gating
                if (param < 25.0f) {
                    // 16th: XXXXXXXX
                    return true;
                } else if (param < 50.0f) {
                    // 8th: X.X.X.X.
                    return (step % 2) == 0;
                } else if (param < 75.0f) {
                    // Triplet: X..X..X.
                    int trip[] = {1, 0, 0, 1, 0, 0, 1, 0};
                    return trip[step % 8];
                } else {
                    // Dotted: X...X...
                    return (step % 4) == 0;
                }
            }
            
        case 28: // Chord Stabs (rhythmic chord hits)
            {
                // X...X...X.X. (dramatic hits)
                int stabs[] = {1, 0, 0, 0, 1, 0, 0, 0};
                // Add extra hit if param > 50
                if (step == 5 && param > 50.0f) {
                    return true;
                }
                return stabs[step % 8];
            }
            
        case 29: // Melody Roll (param controls roll density)
            {
                // Increasing density for build-ups
                float density = param / 100.0f;
                if (density < 0.25f) {
                    // Quarter: X.......
                    return (step % 8) == 0;
                } else if (density < 0.5f) {
                    // Half: X...X...
                    return (step % 4) == 0;
                } else if (density < 0.75f) {
                    // 3/4: X.X.X...
                    int roll[] = {1, 0, 1, 0, 1, 0, 0, 0};
                    return roll[step % 8];
                } else {
                    // Full: X.X.X.X.
                    return (step % 2) == 0;
                }
            }
            
        default:
            return true;
    }
}

// Calculate velocity based on mode
static int
calculate_velocity(MutatedSequencer* self, int step, int base_velocity)
{
    int mode = (int)(*self->velocity_mode);
    if (mode < 0) mode = 0;
    if (mode > 5) mode = 0;
    float amount = *self->velocity_amount / 100.0f;  // 0-1
    
    switch (mode) {
        case 0:  // Manual (use base velocity)
            return base_velocity;
            
        case 1:  // Accent pattern (emphasize certain beats)
            {
                bool is_accent = (step % 4) == 0;  // Accent every 4th step
                if (is_accent) {
                    return base_velocity + (int)((127 - base_velocity) * amount);
                }
                return base_velocity;
            }
            
        case 2:  // Ramp up
            {
                int num_steps = (int)(*self->num_steps);
                if (num_steps <= 1) return base_velocity;
                float ramp = (float)step / (float)(num_steps - 1);
                int min_vel = base_velocity - (int)(base_velocity * amount * 0.5f);
                int max_vel = base_velocity + (int)((127 - base_velocity) * amount);
                return min_vel + (int)((max_vel - min_vel) * ramp);
            }
            
        case 3:  // Ramp down
            {
                int num_steps = (int)(*self->num_steps);
                if (num_steps <= 1) return base_velocity;
                float ramp = 1.0f - ((float)step / (float)(num_steps - 1));
                int min_vel = base_velocity - (int)(base_velocity * amount * 0.5f);
                int max_vel = base_velocity + (int)((127 - base_velocity) * amount);
                return min_vel + (int)((max_vel - min_vel) * ramp);
            }
            
        case 4:  // Random
            {
                float rand_val = random_float(&self->rng_state);
                int variation = (int)(amount * 60.0f);  // Up to ±60 velocity
                return base_velocity + (int)((rand_val - 0.5f) * 2.0f * variation);
            }
            
        case 5:  // Alternating (swing velocity)
            {
                bool is_odd = (step % 2) == 1;
                if (is_odd) {
                    return base_velocity - (int)(base_velocity * amount * 0.3f);
                }
                return base_velocity;
            }
            
        default:
            return base_velocity;
    }
}

// Calculate pitch based on mode
static int
calculate_pitch(MutatedSequencer* self, int step, int base_pitch)
{
    int mode = (int)(*self->pitch_mode);
    if (mode < 0) mode = 0;
    if (mode > 5) mode = 0;
    int spread = (int)(*self->pitch_spread);
    
    switch (mode) {
        case 0:  // Manual (use base pitch)
            return base_pitch;
            
        case 1:  // Ascending
            {
                int num_steps = (int)(*self->num_steps);
                int offset = (step * spread) / num_steps;
                return base_pitch + offset;
            }
            
        case 2:  // Descending
            {
                int num_steps = (int)(*self->num_steps);
                int offset = ((num_steps - 1 - step) * spread) / num_steps;
                return base_pitch + offset;
            }
            
        case 3:  // Pentatonic scale
            {
                // Major pentatonic intervals: 0, 2, 4, 7, 9
                int penta[] = {0, 2, 4, 7, 9};
                int scale_step = step % 5;
                int octave = step / 5;
                return base_pitch + penta[scale_step] + (octave * 12);
            }
            
        case 4:  // Random walk
            {
                float rand_val = random_float(&self->rng_state);
                int offset = (int)((rand_val - 0.5f) * 2.0f * spread);
                return base_pitch + offset;
            }
            
        case 5:  // Zigzag (up/down)
            {
                bool going_up = (step % 4) < 2;
                int local_step = step % 2;
                int offset = going_up ? (local_step * spread / 2) : (spread - local_step * spread / 2);
                return base_pitch + offset;
            }
            
        default:
            return base_pitch;
    }
}

static void
send_note_off(MutatedSequencer* self, uint64_t frame_offset)
{
    if (!self->note_is_on) return;
    
    uint8_t msg[3] = {
        LV2_MIDI_MSG_NOTE_OFF,
        self->current_note,
        0
    };
    
    lv2_atom_forge_frame_time(&self->forge, frame_offset);
    lv2_atom_forge_atom(&self->forge, 3, self->uris.midi_Event);
    lv2_atom_forge_raw(&self->forge, msg, 3);
    lv2_atom_forge_pad(&self->forge, 3);
    
    self->note_is_on = false;
}

static void
send_note_on(MutatedSequencer* self, uint8_t note, uint8_t velocity, uint64_t frame_offset)
{
    // Turn off previous note first
    if (self->note_is_on) {
        send_note_off(self, frame_offset);
    }
    
    uint8_t msg[3] = {
        LV2_MIDI_MSG_NOTE_ON,
        note,
        velocity
    };
    
    lv2_atom_forge_frame_time(&self->forge, frame_offset);
    lv2_atom_forge_atom(&self->forge, 3, self->uris.midi_Event);
    lv2_atom_forge_raw(&self->forge, msg, 3);
    lv2_atom_forge_pad(&self->forge, 3);
    
    self->current_note = note;
    self->note_is_on = true;
}

static void
mutate_sequence(MutatedSequencer* self)
{
    float mutate_amount = *self->mutate / 100.0f;  // 0-1
    
    if (mutate_amount < 0.01f) {
        // No mutation, reset to base values
        for (int i = 0; i < NUM_STEPS; i++) {
            self->mutated_pitch[i] = 0;
            self->mutated_velocity[i] = 0;
        }
        return;
    }
    
    // Mutate pitch and velocity for each step based on probability
    for (int i = 0; i < NUM_STEPS; i++) {
        // Decide if this step gets mutated (based on mutate amount)
        if (random_float(&self->rng_state) < mutate_amount) {
            // Mutate pitch: ±12 semitones max
            float pitch_range = 12.0f * mutate_amount;
            self->mutated_pitch[i] = (int)((random_float(&self->rng_state) - 0.5f) * 2.0f * pitch_range);
        }
        
        if (random_float(&self->rng_state) < mutate_amount) {
            // Mutate velocity: ±40 max
            float vel_range = 40.0f * mutate_amount;
            self->mutated_velocity[i] = (int)((random_float(&self->rng_state) - 0.5f) * 2.0f * vel_range);
        }
    }
}

static void
advance_step(MutatedSequencer* self)
{
    int num_active_steps = (int)(*self->num_steps);
    if (num_active_steps < 1) num_active_steps = 1;
    if (num_active_steps > NUM_STEPS) num_active_steps = NUM_STEPS;
    
    self->current_step = (self->current_step + 1) % num_active_steps;
    self->samples_since_step = 0;
    
    // Check if we looped back to step 0, trigger mutation
    if (self->current_step == 0) {
        mutate_sequence(self);
    }
}

static void
calculate_step_timing(MutatedSequencer* self)
{
    double bpm_to_use = 120.0;
    int clock_src = (int)(*self->clock_source);
    if (clock_src < 0) clock_src = 0;
    if (clock_src > 2) clock_src = 0;
    
    // Determine BPM based on clock source
    if (clock_src == 0) {
        // Internal clock
        bpm_to_use = *self->bpm;
    } else if (clock_src == 1) {
        // MIDI Clock sync
        if (self->clock_interval > 0) {
            // MIDI clock sends 24 pulses per quarter note
            // Calculate BPM from the interval between clock messages
            double seconds_per_clock = (double)self->clock_interval / self->sample_rate;
            double seconds_per_quarter = seconds_per_clock * 24.0;
            bpm_to_use = 60.0 / seconds_per_quarter;
        } else {
            // No MIDI clock received yet, use internal BPM
            bpm_to_use = *self->bpm;
        }
    } else if (clock_src == 2) {
        // Host tempo
        bpm_to_use = self->host_bpm;
    }
    
    // Clamp BPM to safe range
    if (bpm_to_use < 20.0) bpm_to_use = 20.0;
    if (bpm_to_use > 300.0) bpm_to_use = 300.0;
    
    // Clock division: 0=1/4, 1=1/8, 2=1/16, 3=1/32
    int division = (int)(*self->clock_div);
    if (division < 0) division = 2;
    if (division > 3) division = 2;
    double beats_per_step;
    switch (division) {
        case 0: beats_per_step = 1.0; break;      // Quarter note
        case 1: beats_per_step = 0.5; break;      // 8th note
        case 2: beats_per_step = 0.25; break;     // 16th note
        case 3: beats_per_step = 0.125; break;    // 32nd note
        default: beats_per_step = 0.25; break;
    }
    
    double seconds_per_beat = 60.0 / bpm_to_use;
    double seconds_per_step = seconds_per_beat * beats_per_step;
    
    // Apply swing: shorten even steps, lengthen odd steps to maintain tempo
    // This creates triplet-style swing where pairs of steps maintain constant duration
    float swing_amount = *self->swing / 100.0f;  // 0-1
    if (swing_amount > 0.01f) {
        if (self->current_step % 2 == 0) {
            // Even steps get shorter (down to 67% at max swing)
            seconds_per_step *= (1.0 - swing_amount * 0.33);
        } else {
            // Odd steps get longer (up to 133% at max swing) 
            seconds_per_step *= (1.0 + swing_amount * 0.33);
        }
    }
    
    self->samples_per_step = (uint64_t)(seconds_per_step * self->sample_rate);
    if (self->samples_per_step < 1) self->samples_per_step = 1;  // Ensure minimum timing
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    MutatedSequencer* self = (MutatedSequencer*)instance;
    
    // Safety: ensure output port is connected
    if (!self->midi_out || !self->midi_in) {
        return;
    }
    
    // Set up forge to write to output
    const uint32_t out_capacity = self->midi_out->atom.size;
    lv2_atom_forge_set_buffer(&self->forge, (uint8_t*)self->midi_out, out_capacity);
    lv2_atom_forge_sequence_head(&self->forge, &self->frame, 0);
    
    // Read host time information and MIDI clock messages
    LV2_ATOM_SEQUENCE_FOREACH(self->midi_in, ev) {
        if (ev->body.type == self->uris.atom_Object) {
            const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;
            if (obj->body.otype == self->uris.time_Position) {
                // Extract BPM
                const LV2_Atom* bpm_atom = NULL;
                lv2_atom_object_get(obj, self->uris.time_beatsPerMinute, &bpm_atom, 0);
                if (bpm_atom && bpm_atom->type == self->uris.atom_Float) {
                    self->host_bpm = ((const LV2_Atom_Float*)bpm_atom)->body;
                }
                
                // Extract speed (playing state)
                const LV2_Atom* speed_atom = NULL;
                lv2_atom_object_get(obj, self->uris.time_speed, &speed_atom, 0);
                if (speed_atom && speed_atom->type == self->uris.atom_Float) {
                    self->host_playing = (((const LV2_Atom_Float*)speed_atom)->body > 0.0f);
                }
            }
        } else if (ev->body.type == self->uris.midi_Event) {
            // Handle MIDI clock messages
            const uint8_t* const msg = (const uint8_t*)(ev + 1);
            
            if (msg[0] == 0xF8) {  // MIDI Clock (24 ppqn)
                uint64_t current_time = self->sample_position + ev->time.frames;
                
                if (self->last_clock_time > 0) {
                    // Calculate interval between clocks
                    uint64_t interval = current_time - self->last_clock_time;
                    // Smooth the interval with simple averaging
                    if (self->clock_interval == 0) {
                        self->clock_interval = interval;
                    } else {
                        self->clock_interval = (self->clock_interval * 3 + interval) / 4;
                    }
                }
                
                self->last_clock_time = current_time;
                self->midi_clock_count++;
                if (self->midi_clock_count >= 24) {
                    self->midi_clock_count = 0;  // Reset after one quarter note
                }
            } else if (msg[0] == 0xFA) {  // MIDI Start
                self->midi_clock_running = true;
                self->midi_clock_count = 0;
                self->clock_interval = 0;
            } else if (msg[0] == 0xFB) {  // MIDI Continue
                self->midi_clock_running = true;
            } else if (msg[0] == 0xFC) {  // MIDI Stop
                self->midi_clock_running = false;
            }
        }
    }
    
    self->sample_position += n_samples;
    
    // Check if running based on clock source
    bool is_running = false;
    int clock_src = (int)(*self->clock_source);
    if (clock_src < 0) clock_src = 0;
    if (clock_src > 2) clock_src = 0;
    
    if (clock_src == 0) {
        // Internal clock: use Running control OR host transport
        // This allows both manual control and DAW/Zynthian transport control
        bool manual_run = (*self->running > 0.5f);
        is_running = manual_run || self->host_playing;
    } else if (clock_src == 1) {
        // MIDI Clock: run if we have clock AND (Running control OR MIDI Start received)
        // This allows both manual control and automatic sync
        bool manual_run = (*self->running > 0.5f);
        bool midi_start = self->midi_clock_running;
        bool has_clock = (self->clock_interval > 0);
        is_running = has_clock && (manual_run || midi_start);
    } else if (clock_src == 2) {
        // Host Tempo: follow DAW transport (also allow manual Running control as override)
        is_running = self->host_playing || (*self->running > 0.5f);
    }
    
    if (!is_running) {
        // Stop any playing note
        if (self->note_is_on) {
            send_note_off(self, 0);
        }
        // Reset ratchet state so it reinitializes on next start
        self->samples_per_ratchet = 0;
        self->gate_active = false;
        return;
    }
    
    // Initialize ratchet if needed (first time running or after stop)
    if (self->samples_per_ratchet == 0) {
        // Calculate timing for current step before initializing ratchet
        calculate_step_timing(self);
        
        int step = self->current_step;
        self->ratchet_count = (int)(*self->step_ratchet[step]);
        if (self->ratchet_count < 1) self->ratchet_count = 1;
        if (self->ratchet_count > 8) self->ratchet_count = 8;
        self->samples_per_ratchet = self->samples_per_step / self->ratchet_count;
        if (self->samples_per_ratchet < 1) self->samples_per_ratchet = 1;
        self->current_ratchet = 0;
        self->samples_since_ratchet = 0;  // Start at 0 so it triggers immediately
        self->step_is_active = false;
    }
    
    // Process the buffer
    for (uint32_t i = 0; i < n_samples; i++) {
        // Check if we should advance to next step (check at START of this sample)
        if (self->samples_since_step >= self->samples_per_step) {
            advance_step(self);
            calculate_step_timing(self);
            
            // Setup ratchet for this step
            int step = self->current_step;
            self->ratchet_count = (int)(*self->step_ratchet[step]);
            if (self->ratchet_count < 1) self->ratchet_count = 1;
            if (self->ratchet_count > 8) self->ratchet_count = 8;
            
            self->current_ratchet = 0;
            // Extra safety: ensure ratchet_count is valid before division
            if (self->ratchet_count < 1) self->ratchet_count = 1;
            if (self->ratchet_count > 8) self->ratchet_count = 8;
            self->samples_per_ratchet = self->samples_per_step / self->ratchet_count;
            if (self->samples_per_ratchet < 1) self->samples_per_ratchet = 1;
            self->samples_since_ratchet = 0;  // Will trigger this sample
            self->step_is_active = false;  // Reset for new step
        }
        
        // Increment counters at start of sample processing
        self->samples_since_step++;
        self->samples_since_ratchet++;
        
        // Check if we should trigger a ratchet
        bool should_trigger = false;
        if (self->current_ratchet < self->ratchet_count && 
            self->samples_since_ratchet >= self->samples_per_ratchet) {
            
            self->current_ratchet++;
            self->samples_since_ratchet = 0;
            
            // For first ratchet of step, check patterns and probability
            if (self->current_ratchet == 1) {
                int step = self->current_step;
                int num_active_steps = (int)(*self->num_steps);
                
                // Check per-step probability (0-100%)
                bool gate_on = true;  // Always attempt to trigger
                float step_prob = *self->step_probability[step] / 100.0f;
                if (step_prob < 0.99f) {  // If not 100%, apply probability
                    float roll = random_float(&self->rng_state);
                    if (roll > step_prob) {
                        gate_on = false;
                    }
                }
                
                // Apply rhythm pattern
                int pattern = (int)(*self->pattern);
                if (pattern < 0) pattern = 0;
                if (pattern > 29) pattern = 0;
                float pattern_param = *self->pattern_param;
                bool pattern_gate = apply_pattern(step, num_active_steps, pattern, pattern_param);
                gate_on = gate_on && pattern_gate;
                
                // Apply global probability
                float prob = *self->probability / 100.0f;
                if (prob < 0.99f) {
                    float roll = random_float(&self->rng_state);
                    if (roll > prob) {
                        gate_on = false;
                    }
                }
                
                // Store gate decision for this step and use for triggering
                self->step_is_active = gate_on;
                should_trigger = gate_on;
            } else {
                // Subsequent ratchets only fire if the step is active
                should_trigger = self->step_is_active;
            }
        }
        
        // Check if we should turn off the gate
        if (self->gate_active) {
            float gate_len = *self->gate_length / 100.0f;  // 0-1
            uint64_t gate_samples = (uint64_t)(self->samples_per_ratchet * gate_len);
            if (gate_samples < 1) gate_samples = 1;  // Ensure at least 1 sample gate
            
            if (self->samples_since_ratchet >= gate_samples) {
                send_note_off(self, i);
                self->gate_active = false;
            }
        }
        
        // Trigger note if needed
        if (should_trigger) {
            int step = self->current_step;
            int pitch = (int)(*self->step_pitch[step]);
            int velocity = (int)(*self->step_velocity[step]);
            
            // Apply mutations first (before generative modes)
            pitch += self->mutated_pitch[step];
            velocity += self->mutated_velocity[step];
            
            // Apply generative pitch mode
            pitch = calculate_pitch(self, step, pitch);
            
            // Apply generative velocity mode
            velocity = calculate_velocity(self, step, velocity);
            
            // Apply transpose
            int transpose = (int)(*self->transpose);
            pitch += transpose;
            
            // Clamp values
            if (pitch < 0) pitch = 0;
            if (pitch > 127) pitch = 127;
            velocity = velocity < 0 ? 0 : (velocity > 127 ? 127 : velocity);
            
            if (velocity > 0) {
                // Apply humanization (timing jitter)
                uint32_t trigger_offset = i;
                float humanize = *self->humanize / 100.0f;
                if (humanize > 0.001f) {
                    float jitter = (random_float(&self->rng_state) - 0.5f) * 2.0f;  // -1 to 1
                    int max_jitter = (int)(humanize * self->sample_rate * 0.01);  // Up to 10ms
                    int jitter_samples = (int)(jitter * max_jitter);
                    
                    // Safe offset calculation with proper bounds checking
                    if (jitter_samples < 0 && (uint32_t)(-jitter_samples) > i) {
                        trigger_offset = 0;  // Would go negative, clamp to start
                    } else if (jitter_samples > 0 && i + (uint32_t)jitter_samples >= n_samples) {
                        trigger_offset = n_samples - 1;  // Would exceed buffer, clamp to end
                    } else {
                        trigger_offset = i + jitter_samples;  // Safe to add
                    }
                }
                
                send_note_on(self, (uint8_t)pitch, (uint8_t)velocity, trigger_offset);
                self->gate_active = true;
            }
        }
    }
}

static void
cleanup(LV2_Handle instance)
{
    free(instance);
}

static const LV2_Descriptor descriptor = {
    MUTATED_SEQ_URI,
    instantiate,
    connect_port,
    NULL,  // activate
    run,
    NULL,  // deactivate
    cleanup,
    NULL   // extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
