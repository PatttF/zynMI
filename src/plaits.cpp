#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <stdlib.h>
#include <string.h>

#define TEST
#include "plaits/dsp/voice.h"
#include "stages/segment_generator.h"
#include "rings/dsp/part.h"
#include "rings/dsp/strummer.h"
#include "stmlib/dsp/filter.h"

#define MI_PLAITS_URI "http://mutable-instruments.net/plugins/plaits"

static const int BLOCK_SIZE = 8;

typedef enum {
    PLAITS_MIDI_IN = 0,
    PLAITS_MODEL,
    PLAITS_FREQ,
    PLAITS_HARMONICS,
    PLAITS_TIMBRE,
    PLAITS_MORPH,
    PLAITS_LPG_COLOR,
    PLAITS_LPG_DECAY,
    PLAITS_ENV_ATTACK,
    PLAITS_ENV_DECAY,
    PLAITS_ENV_SUSTAIN,
    PLAITS_ENV_RELEASE,
    PLAITS_ENV_ATTACK_SHAPE,
    PLAITS_ENV_DECAY_SHAPE,
    PLAITS_ENV_RELEASE_SHAPE,
    PLAITS_TRIGGER_IN,
    PLAITS_LEVEL_IN,
    PLAITS_NOTE_IN,
    PLAITS_RINGS_ENABLE,
    PLAITS_RINGS_POLYPHONY,
    PLAITS_RINGS_MODEL,
    PLAITS_RINGS_FREQUENCY,
    PLAITS_RINGS_STRUCTURE,
    PLAITS_RINGS_BRIGHTNESS,
    PLAITS_RINGS_DAMPING,
    PLAITS_RINGS_POSITION,
    PLAITS_FILTER_TYPE,
    PLAITS_FILTER_CUTOFF,
    PLAITS_FILTER_RESONANCE,
    PLAITS_OUT_L,
    PLAITS_OUT_R
} PortIndex;

typedef struct {
    // Port buffers
    const LV2_Atom_Sequence* midi_in;
    const float* model;
    const float* freq;
    const float* harmonics;
    const float* timbre;
    const float* morph;
    const float* lpg_color;
    const float* lpg_decay;
    const float* env_attack;
    const float* env_decay;
    const float* env_sustain;
    const float* env_release;
    const float* env_attack_shape;
    const float* env_decay_shape;
    const float* env_release_shape;
    const float* trigger_in;
    const float* level_in;
    const float* note_in;
    const float* rings_enable;
    const float* rings_polyphony;
    const float* rings_model;
    const float* rings_frequency;
    const float* rings_structure;
    const float* rings_brightness;
    const float* rings_damping;
    const float* rings_position;
    const float* filter_type;
    const float* filter_cutoff;
    const float* filter_resonance;
    float* out_l;
    float* out_r;
    
    // DSP
    plaits::Voice* voice;
    plaits::Patch patch;
    char shared_buffer[16384];
    plaits::Voice::Frame output[24];
    
    // Stages Envelope (full segment generator)
    stages::SegmentGenerator envelope;
    stages::segment::Configuration segment_config[4];  // ADSR = 4 segments
    float envelope_buffer[BLOCK_SIZE];
    stmlib::GateFlags gate_flags[BLOCK_SIZE];
    stmlib::GateFlags last_gate_flag;
    int block_index;
    uint8_t velocity;
    
    // Rings DSP
    rings::Part rings_part;
    rings::Strummer rings_strummer;
    rings::PerformanceState rings_performance_state;
    uint16_t rings_reverb_buffer[32768];
    float rings_in[24];
    float rings_out[24];
    float rings_aux[24];
    int rings_buffer_index;
    
    // Filter DSP - stereo SVF filters
    stmlib::Svf filter_left;
    stmlib::Svf filter_right;
    // Moog ladder filter states (4-pole cascade)
    float ladder_state_left[4];
    float ladder_state_right[4];
    // MS20 filter states (HPF + LPF)
    float ms20_hp_left[2];
    float ms20_hp_right[2];
    float ms20_lp_left[2];
    float ms20_lp_right[2];
    // SVF filter states (lowpass, bandpass, highpass)
    float svf_state_left[2];
    float svf_state_right[2];
    // One-pole filter states
    float onepole_state_left;
    float onepole_state_right;
    
    // MIDI state
    LV2_URID_Map* map;
    LV2_URID midi_event_uri;
    uint8_t current_note;
    bool note_on;
    float gate_level;
    
    double sample_rate;
    float last_trigger;
    bool envelope_active;
} Plaits;

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double rate,
            const char* bundle_path,
            const LV2_Feature* const* features)
{
    Plaits* plaits = (Plaits*)calloc(1, sizeof(Plaits));
    if (!plaits) {
        return NULL;
    }
    
    plaits->map = NULL;
    if (features) {
        for (int i = 0; features[i]; i++) {
            if (!strcmp(features[i]->URI, LV2_URID__map)) {
                plaits->map = (LV2_URID_Map*)features[i]->data;
                break;
            }
        }
    }
    if (plaits->map) {
        plaits->midi_event_uri = plaits->map->map(plaits->map->handle, LV2_MIDI__MidiEvent);
    } else {
        plaits->midi_event_uri = 0;
    }
    
    plaits->sample_rate = rate;
    plaits->current_note = 60;  // Middle C
    plaits->note_on = false;
    plaits->gate_level = 0.0f;
    plaits->velocity = 100;
    plaits->last_trigger = 0.0f;
    plaits->envelope_active = false;
    
    // Initialize ADSR envelope
    plaits->envelope.Init();
    plaits->envelope.SetSampleRate(rate);
    
    plaits->segment_config[0].type = stages::segment::TYPE_RAMP;
    plaits->segment_config[0].loop = false;
    plaits->segment_config[1].type = stages::segment::TYPE_RAMP;
    plaits->segment_config[1].loop = false;
    plaits->segment_config[2].type = stages::segment::TYPE_HOLD;
    plaits->segment_config[2].loop = false;
    plaits->segment_config[3].type = stages::segment::TYPE_RAMP;
    plaits->segment_config[3].loop = false;
    
    plaits->envelope.Configure(true, plaits->segment_config, 4);
    plaits->last_gate_flag = stmlib::GATE_FLAG_LOW;
    plaits->block_index = 0;
    
    // Initialize Rings
    plaits->rings_part.Init(plaits->rings_reverb_buffer);
    plaits->rings_strummer.Init(0.01f, rate / 24.0f);
    plaits->rings_buffer_index = 0;
    memset(plaits->rings_in, 0, sizeof(plaits->rings_in));
    memset(plaits->rings_out, 0, sizeof(plaits->rings_out));
    memset(plaits->rings_aux, 0, sizeof(plaits->rings_aux));
    
    plaits->rings_performance_state.internal_exciter = true;
    plaits->rings_performance_state.internal_strum = false;
    plaits->rings_performance_state.internal_note = false;
    plaits->rings_performance_state.tonic = 12.0f;
    plaits->rings_performance_state.fm = 0.0f;
    plaits->rings_performance_state.chord = 0;
    
    // Initialize filters
    plaits->filter_left.Init();
    plaits->filter_right.Init();
    memset(plaits->ladder_state_left, 0, sizeof(plaits->ladder_state_left));
    memset(plaits->ladder_state_right, 0, sizeof(plaits->ladder_state_right));
    memset(plaits->ms20_hp_left, 0, sizeof(plaits->ms20_hp_left));
    memset(plaits->ms20_hp_right, 0, sizeof(plaits->ms20_hp_right));
    memset(plaits->ms20_lp_left, 0, sizeof(plaits->ms20_lp_left));
    memset(plaits->ms20_lp_right, 0, sizeof(plaits->ms20_lp_right));
    memset(plaits->svf_state_left, 0, sizeof(plaits->svf_state_left));
    memset(plaits->svf_state_right, 0, sizeof(plaits->svf_state_right));
    plaits->onepole_state_left = 0.0f;
    plaits->onepole_state_right = 0.0f;
    
    plaits->midi_in = NULL;
    plaits->model = NULL;
    plaits->freq = NULL;
    plaits->harmonics = NULL;
    plaits->timbre = NULL;
    plaits->morph = NULL;
    plaits->lpg_color = NULL;
    plaits->lpg_decay = NULL;
    plaits->env_attack = NULL;
    plaits->env_decay = NULL;
    plaits->env_sustain = NULL;
    plaits->env_release = NULL;
    plaits->env_attack_shape = NULL;
    plaits->env_decay_shape = NULL;
    plaits->env_release_shape = NULL;
    plaits->trigger_in = NULL;
    plaits->level_in = NULL;
    plaits->note_in = NULL;
    plaits->rings_enable = NULL;
    plaits->rings_polyphony = NULL;
    plaits->rings_model = NULL;
    plaits->rings_frequency = NULL;
    plaits->rings_structure = NULL;
    plaits->rings_brightness = NULL;
    plaits->rings_damping = NULL;
    plaits->rings_position = NULL;
    plaits->filter_type = NULL;
    plaits->filter_cutoff = NULL;
    plaits->filter_resonance = NULL;
    plaits->out_l = NULL;
    plaits->out_r = NULL;
    
    plaits->voice = new plaits::Voice();
    if (plaits->voice) {
        stmlib::BufferAllocator allocator;
        allocator.Init(plaits->shared_buffer, sizeof(plaits->shared_buffer));
        plaits->voice->Init(&allocator);
    }
    
    plaits->patch.engine = 0;
    plaits->patch.lpg_colour = 0.5f;
    plaits->patch.decay = 0.5f;
    plaits->patch.note = 48.0f;
    plaits->patch.harmonics = 0.5f;
    plaits->patch.timbre = 0.5f;
    plaits->patch.morph = 0.5f;
    plaits->patch.frequency_modulation_amount = 0.0f;
    plaits->patch.timbre_modulation_amount = 0.0f;
    plaits->patch.morph_modulation_amount = 0.0f;
    
    return (LV2_Handle)plaits;
}

static void
connect_port(LV2_Handle instance,
             uint32_t port,
             void* data)
{
    Plaits* plaits = (Plaits*)instance;
    
    switch ((PortIndex)port) {
        case PLAITS_MIDI_IN:
            plaits->midi_in = (const LV2_Atom_Sequence*)data;
            break;
        case PLAITS_MODEL:
            plaits->model = (const float*)data;
            break;
        case PLAITS_FREQ:
            plaits->freq = (const float*)data;
            break;
        case PLAITS_HARMONICS:
            plaits->harmonics = (const float*)data;
            break;
        case PLAITS_TIMBRE:
            plaits->timbre = (const float*)data;
            break;
        case PLAITS_MORPH:
            plaits->morph = (const float*)data;
            break;
        case PLAITS_LPG_COLOR:
            plaits->lpg_color = (const float*)data;
            break;
        case PLAITS_LPG_DECAY:
            plaits->lpg_decay = (const float*)data;
            break;
        case PLAITS_ENV_ATTACK:
            plaits->env_attack = (const float*)data;
            break;
        case PLAITS_ENV_DECAY:
            plaits->env_decay = (const float*)data;
            break;
        case PLAITS_ENV_SUSTAIN:
            plaits->env_sustain = (const float*)data;
            break;
        case PLAITS_ENV_RELEASE:
            plaits->env_release = (const float*)data;
            break;
        case PLAITS_ENV_ATTACK_SHAPE:
            plaits->env_attack_shape = (const float*)data;
            break;
        case PLAITS_ENV_DECAY_SHAPE:
            plaits->env_decay_shape = (const float*)data;
            break;
        case PLAITS_ENV_RELEASE_SHAPE:
            plaits->env_release_shape = (const float*)data;
            break;
        case PLAITS_TRIGGER_IN:
            plaits->trigger_in = (const float*)data;
            break;
        case PLAITS_LEVEL_IN:
            plaits->level_in = (const float*)data;
            break;
        case PLAITS_NOTE_IN:
            plaits->note_in = (const float*)data;
            break;
        case PLAITS_RINGS_ENABLE:
            plaits->rings_enable = (const float*)data;
            break;
        case PLAITS_RINGS_POLYPHONY:
            plaits->rings_polyphony = (const float*)data;
            break;
        case PLAITS_RINGS_MODEL:
            plaits->rings_model = (const float*)data;
            break;
        case PLAITS_RINGS_FREQUENCY:
            plaits->rings_frequency = (const float*)data;
            break;
        case PLAITS_RINGS_STRUCTURE:
            plaits->rings_structure = (const float*)data;
            break;
        case PLAITS_RINGS_BRIGHTNESS:
            plaits->rings_brightness = (const float*)data;
            break;
        case PLAITS_RINGS_DAMPING:
            plaits->rings_damping = (const float*)data;
            break;
        case PLAITS_RINGS_POSITION:
            plaits->rings_position = (const float*)data;
            break;
        case PLAITS_FILTER_TYPE:
            plaits->filter_type = (const float*)data;
            break;
        case PLAITS_FILTER_CUTOFF:
            plaits->filter_cutoff = (const float*)data;
            break;
        case PLAITS_FILTER_RESONANCE:
            plaits->filter_resonance = (const float*)data;
            break;
        case PLAITS_OUT_L:
            plaits->out_l = (float*)data;
            break;
        case PLAITS_OUT_R:
            plaits->out_r = (float*)data;
            break;
    }
}

static void
activate(LV2_Handle instance)
{
}

// Helper function to clamp values
static inline float Clip(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// Moog ladder filter (4-pole lowpass with resonance)
static inline float ProcessMoogLadder(float input, float* state, float cutoff, float resonance) {
    // Cutoff: 0-1, Resonance: 0-1
    // Oversample by 2x for stability
    const float k = resonance * 4.0f;  // Feedback amount
    const float p = cutoff;
    const float scale = 1.0f / (1.0f + k);
    
    for (int os = 0; os < 2; os++) {
        float x = input - k * state[3];
        
        // Four one-pole filters in series
        state[0] = state[0] + p * (x - state[0]);
        state[1] = state[1] + p * (state[0] - state[1]);
        state[2] = state[2] + p * (state[1] - state[2]);
        state[3] = state[3] + p * (state[2] - state[3]);
    }
    
    return state[3] * scale;
}

// MS20 filter (resonant HPF + LPF in series)
static inline float ProcessMS20Filter(float input, float* hp_state, float* lp_state, float cutoff, float resonance) {
    // MS20 has aggressive resonance character
    // HPF then LPF creates bandpass-like resonance
    const float k = resonance * 2.0f;
    const float p = cutoff;
    
    // High-pass stage
    float hp_out = input - hp_state[0];
    hp_state[0] += p * hp_out;
    hp_out -= k * hp_state[1];
    hp_state[1] += p * (hp_out - hp_state[1]);
    
    // Low-pass stage with resonance feedback
    float lp_in = hp_out - k * lp_state[1];
    lp_state[0] += p * (lp_in - lp_state[0]);
    lp_state[1] += p * (lp_state[0] - lp_state[1]);
    
    return lp_state[1];
}

// State Variable Filter - Lowpass mode
static inline float ProcessSVFLowpass(float input, float* state, float cutoff, float resonance) {
    const float f = cutoff;
    const float q = 1.0f - resonance * 0.9f;
    
    float low = state[0];
    float band = state[1];
    float high = input - low - q * band;
    
    band += f * high;
    low += f * band;
    
    state[0] = low;
    state[1] = band;
    
    return low;
}

// State Variable Filter - Bandpass mode
static inline float ProcessSVFBandpass(float input, float* state, float cutoff, float resonance) {
    const float f = cutoff;
    const float q = 1.0f - resonance * 0.9f;
    
    float low = state[0];
    float band = state[1];
    float high = input - low - q * band;
    
    band += f * high;
    low += f * band;
    
    state[0] = low;
    state[1] = band;
    
    return band;
}

// State Variable Filter - Highpass mode
static inline float ProcessSVFHighpass(float input, float* state, float cutoff, float resonance) {
    const float f = cutoff;
    const float q = 1.0f - resonance * 0.9f;
    
    float low = state[0];
    float band = state[1];
    float high = input - low - q * band;
    
    band += f * high;
    low += f * band;
    
    state[0] = low;
    state[1] = band;
    
    return high;
}

// Simple one-pole lowpass (gentle 6dB/oct slope)
static inline float ProcessOnePole(float input, float* state, float cutoff) {
    state[0] += cutoff * (input - state[0]);
    return state[0];
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    Plaits* plaits = (Plaits*)instance;
    
    if (!plaits || !plaits->voice || !plaits->out_l || !plaits->out_r ||
        !plaits->model || !plaits->freq || !plaits->harmonics || 
        !plaits->timbre || !plaits->morph || !plaits->lpg_color || !plaits->lpg_decay ||
        !plaits->env_attack || !plaits->env_decay || !plaits->env_sustain || !plaits->env_release ||
        !plaits->env_attack_shape || !plaits->env_decay_shape || !plaits->env_release_shape) {
        if (plaits && plaits->out_l && plaits->out_r) {
            for (uint32_t i = 0; i < n_samples; i++) {
                plaits->out_l[i] = 0.0f;
                plaits->out_r[i] = 0.0f;
            }
        }
        return;
    }
    
    bool new_note_triggered = false;
    if (plaits->midi_event_uri && plaits->midi_in && plaits->midi_in->atom.size > 0) {
        LV2_ATOM_SEQUENCE_FOREACH(plaits->midi_in, ev) {
            if (ev->body.type == plaits->midi_event_uri) {
            const uint8_t* const msg = (const uint8_t*)(ev + 1);
            
            switch (lv2_midi_message_type(msg)) {
                case LV2_MIDI_MSG_NOTE_ON:
                    if (msg[2] > 0) {
                        plaits->current_note = msg[1];
                        plaits->velocity = msg[2];
                        plaits->note_on = true;
                        plaits->gate_level = 0.8f;
                        new_note_triggered = true;
                    } else {
                        if (msg[1] == plaits->current_note) {
                            plaits->note_on = false;
                            plaits->gate_level = 0.0f;
                        }
                    }
                    break;
                    
                case LV2_MIDI_MSG_NOTE_OFF:
                    if (msg[1] == plaits->current_note) {
                        plaits->note_on = false;
                        plaits->gate_level = 0.0f;
                    }
                    break;
            }
        }
        }
    }
    
    // Update patch parameters
    int model = (int)(*plaits->model);
    if (model < 0) model = 0;
    if (model > 15) model = 15;
    plaits->patch.engine = model;
    
    plaits->patch.lpg_colour = *plaits->lpg_color;
    plaits->patch.decay = *plaits->lpg_decay;
    plaits->patch.harmonics = *plaits->harmonics;
    plaits->patch.timbre = *plaits->timbre;
    plaits->patch.morph = *plaits->morph;
    
    // Set modulation amounts to 0 since we don't have CV attenuverter controls
    // If we add CV attenuverter knobs in the future, update these from those knob values
    plaits->patch.frequency_modulation_amount = 0.0f;
    plaits->patch.timbre_modulation_amount = 0.0f;
    plaits->patch.morph_modulation_amount = 0.0f;
    
    // Configure ADSR envelope parameters
    plaits->envelope.set_segment_parameters(0, *plaits->env_attack, *plaits->env_attack_shape);
    plaits->envelope.set_segment_parameters(1, *plaits->env_decay, *plaits->env_decay_shape);
    plaits->envelope.set_segment_parameters(2, *plaits->env_sustain, 0.0f);
    plaits->envelope.set_segment_parameters(3, *plaits->env_release, *plaits->env_release_shape);
    
    // Process audio in blocks of 24 samples (Plaits block size)
    uint32_t offset = 0;
    while (offset < n_samples) {
        uint32_t block_size = n_samples - offset;
        if (block_size > 24) {
            block_size = 24;
        }
        
        float env_values[24];
        uint32_t env_offset = 0;
        
        while (env_offset < block_size) {
            uint32_t env_block = block_size - env_offset;
            if (env_block > BLOCK_SIZE) {
                env_block = BLOCK_SIZE;
            }
            for (uint32_t i = 0; i < env_block; i++) {
                bool gate_high = plaits->note_on;
                stmlib::GateFlags current = gate_high ? stmlib::GATE_FLAG_HIGH : stmlib::GATE_FLAG_LOW;
                
                // Force retrigger on new note, even during legato
                if (i == 0 && new_note_triggered) {
                    current |= stmlib::GATE_FLAG_RISING;
                    new_note_triggered = false;
                } else if (gate_high && !(plaits->last_gate_flag & stmlib::GATE_FLAG_HIGH)) {
                    current |= stmlib::GATE_FLAG_RISING;
                }
                
                if (!gate_high && (plaits->last_gate_flag & stmlib::GATE_FLAG_HIGH)) {
                    current |= stmlib::GATE_FLAG_FALLING;
                }
                
                plaits->gate_flags[i] = current;
                plaits->last_gate_flag = current;
            }
            
            stages::SegmentGenerator::Output env_out[BLOCK_SIZE];
            plaits->envelope.Process(plaits->gate_flags, env_out, env_block);
            for (uint32_t i = 0; i < env_block; i++) {
                env_values[env_offset + i] = env_out[i].value;
            }
            
            env_offset += env_block;
        }
        
        float vel_scale = plaits->velocity / 127.0f;
        plaits::Modulations modulations;
        modulations.engine = 0.0f;
        modulations.note = 0.0f;
        modulations.frequency = 0.0f;
        modulations.harmonics = 0.0f;
        modulations.timbre = 0.0f;
        modulations.morph = 0.0f;
        modulations.trigger = 0.0f;
        modulations.level = 0.0f;
        modulations.frequency_patched = false;
        modulations.timbre_patched = false;
        modulations.morph_patched = false;
        modulations.trigger_patched = false;
        modulations.level_patched = false;
        
        float trigger_value = 0.0f;
        if (plaits->note_on) {
            if (new_note_triggered) {
                modulations.trigger = 1.0f;
                new_note_triggered = false;
            }
            modulations.level = plaits->gate_level;
        } else {
            modulations.level = 0.0f;
            if (plaits->trigger_in) {
                trigger_value = plaits->trigger_in[offset] / 3.0f;  // Trigger voltage /3.0 
                if (trigger_value > 0.23f && plaits->last_trigger <= 0.23f) {  // Triggers at ~0.7V
                    modulations.trigger = 1.0f;
                }
                plaits->last_trigger = trigger_value;
            }
        }
        if (!plaits->note_on) {
            if (plaits->level_in) {
                modulations.level = plaits->level_in[offset] / 8.0f;  // VCV uses /8.0
                if (modulations.level > 0.01f) {
                    plaits->envelope_active = true;
                }
            } else if (trigger_value > 0.1f) {
                modulations.level = trigger_value / 3.0f;  // Trigger normalizes to /3.0
                plaits->envelope_active = true;
            } else {
                modulations.level = 0.0f;
            }
        }
        
        if (plaits->note_on) {
            plaits->patch.note = (float)plaits->current_note + *plaits->freq;
        } else if (plaits->note_in) {
            plaits->patch.note = 48.0f + *plaits->freq + plaits->note_in[offset] * 12.0f;
        } else {
            plaits->patch.note = 48.0f + *plaits->freq;
        }
        bool should_render = (modulations.level > 0.001f) || (modulations.trigger > 0.0f) || plaits->envelope_active;
        
        if (should_render) {
            plaits->voice->Render(plaits->patch, modulations, plaits->output, block_size);
            
            // Convert Plaits output to float array for Rings processing
            float plaits_out[24];
            for (uint32_t i = 0; i < block_size; i++) {
                float out_sample = plaits->output[i].out / 32768.0f;
                float aux_sample = plaits->output[i].aux / 32768.0f;
                plaits_out[i] = (out_sample + aux_sample) * 0.5f;
            }
            
            // Process through Rings if enabled
            float rings_out_l[24];
            float rings_out_r[24];
            
            bool rings_enabled = plaits->rings_enable && (*plaits->rings_enable > 0.5f);
            
            if (rings_enabled) {
                // Accumulate 24 samples for Rings processing
                for (uint32_t i = 0; i < block_size; i++) {
                    plaits->rings_in[plaits->rings_buffer_index] = plaits_out[i];
                    plaits->rings_buffer_index++;
                    
                    if (plaits->rings_buffer_index >= 24) {
                        // Update Rings polyphony and model
                        int polyphony = plaits->rings_polyphony ? (int)*plaits->rings_polyphony : 1;
                        if (polyphony < 1) polyphony = 1;
                        if (polyphony > 4) polyphony = 4;
                        plaits->rings_part.set_polyphony(polyphony);
                        
                        int model = plaits->rings_model ? (int)*plaits->rings_model : 0;
                        if (model < 0) model = 0;
                        if (model >= rings::RESONATOR_MODEL_LAST) model = rings::RESONATOR_MODEL_LAST - 1;
                        plaits->rings_part.set_model((rings::ResonatorModel)model);
                        
                        // Process Rings patch
                        rings::Patch rings_patch;
                        rings_patch.brightness = plaits->rings_brightness ? *plaits->rings_brightness : 0.5f;
                        rings_patch.damping = plaits->rings_damping ? *plaits->rings_damping : 0.5f;
                        rings_patch.position = plaits->rings_position ? *plaits->rings_position : 0.5f;
                        rings_patch.structure = plaits->rings_structure ? *plaits->rings_structure : 0.5f;
                        
                        // Setup performance state
                        rings::PerformanceState perf_state = plaits->rings_performance_state;
                        
                        // Use internal exciter
                        perf_state.internal_exciter = false;  // Use Plaits as exciter
                        perf_state.internal_strum = false;
                        perf_state.internal_note = false;
                        
                        // Trigger on note events
                        perf_state.strum = plaits->note_on;
                        
                        // Calculate frequency
                        float freq_transpose = plaits->rings_frequency ? (*plaits->rings_frequency - 0.5f) * 48.0f : 0.0f;
                        perf_state.note = plaits->patch.note;
                        perf_state.tonic = freq_transpose;
                        perf_state.fm = 0.0f;
                        
                        // Calculate chord from structure
                        const int kNumChords = 11;
                        perf_state.chord = (int)(rings_patch.structure * (kNumChords - 1));
                        if (perf_state.chord < 0) perf_state.chord = 0;
                        if (perf_state.chord >= kNumChords) perf_state.chord = kNumChords - 1;
                        
                        // Process with Strummer
                        plaits->rings_strummer.Process(plaits->rings_in, 24, &perf_state);
                        
                        // Process through Rings
                        plaits->rings_part.Process(perf_state, rings_patch, plaits->rings_in, 
                                                   plaits->rings_out, plaits->rings_aux, 24);
                        
                        // Reset buffer
                        plaits->rings_buffer_index = 0;
                    }
                }
                
                // Use Rings output (odd = left, aux = right)
                // Apply mono mixing for stereo
                for (uint32_t i = 0; i < block_size; i++) {
                    float mixed = (plaits->rings_out[i] + plaits->rings_aux[i]) * 0.5f * 0.8f;
                    rings_out_l[i] = mixed;
                    rings_out_r[i] = mixed;
                }
            } else {
                // Rings disabled - use Plaits output directly
                for (uint32_t i = 0; i < block_size; i++) {
                    rings_out_l[i] = plaits_out[i];
                    rings_out_r[i] = plaits_out[i];
                }
            }
            
            // Apply filters
            int filter_type = plaits->filter_type ? (int)*plaits->filter_type : 0;
            float filter_cutoff = plaits->filter_cutoff ? *plaits->filter_cutoff : 0.5f;
            float filter_resonance = plaits->filter_resonance ? *plaits->filter_resonance : 0.0f;
            
            // Clamp parameters
            filter_cutoff = Clip(filter_cutoff, 0.001f, 0.999f);
            filter_resonance = Clip(filter_resonance, 0.0f, 1.0f);
            
            // Filter state arrays (4 floats for ladder, 2 for others)
            static float ladder_state_l[4] = {0};
            static float ladder_state_r[4] = {0};
            static float ms20_hp_l[2] = {0};
            static float ms20_hp_r[2] = {0};
            static float ms20_lp_l[2] = {0};
            static float ms20_lp_r[2] = {0};
            static float svf_state_l[2] = {0};
            static float svf_state_r[2] = {0};
            static float onepole_l[1] = {0};
            static float onepole_r[1] = {0};
            
            bool has_output = false;
            for (uint32_t i = 0; i < block_size; i++) {
                float left = rings_out_l[i];
                float right = rings_out_r[i];
                
                // Apply filter
                if (filter_type > 0) {
                    if (filter_type == 1) {
                        // Moog ladder filter
                        left = ProcessMoogLadder(left, ladder_state_l, filter_cutoff, filter_resonance);
                        right = ProcessMoogLadder(right, ladder_state_r, filter_cutoff, filter_resonance);
                    } else if (filter_type == 2) {
                        // MS20 filter
                        left = ProcessMS20Filter(left, ms20_hp_l, ms20_lp_l, filter_cutoff, filter_resonance);
                        right = ProcessMS20Filter(right, ms20_hp_r, ms20_lp_r, filter_cutoff, filter_resonance);
                    } else if (filter_type == 3) {
                        // SVF Lowpass
                        left = ProcessSVFLowpass(left, svf_state_l, filter_cutoff, filter_resonance);
                        right = ProcessSVFLowpass(right, svf_state_r, filter_cutoff, filter_resonance);
                    } else if (filter_type == 4) {
                        // SVF Bandpass
                        left = ProcessSVFBandpass(left, svf_state_l, filter_cutoff, filter_resonance);
                        right = ProcessSVFBandpass(right, svf_state_r, filter_cutoff, filter_resonance);
                    } else if (filter_type == 5) {
                        // SVF Highpass
                        left = ProcessSVFHighpass(left, svf_state_l, filter_cutoff, filter_resonance);
                        right = ProcessSVFHighpass(right, svf_state_r, filter_cutoff, filter_resonance);
                    } else if (filter_type == 6) {
                        // One-Pole LP
                        left = ProcessOnePole(left, onepole_l, filter_cutoff);
                        right = ProcessOnePole(right, onepole_r, filter_cutoff);
                    }
                }
                
                // Apply envelope
                float env = env_values[i] * vel_scale;
                left *= env;
                right *= env;
                
                if ((left > 0.0001f || left < -0.0001f) || 
                    (right > 0.0001f || right < -0.0001f)) {
                    has_output = true;
                }
                
                // Scale to ±5V
                plaits->out_l[offset + i] = left * 5.0f;
                plaits->out_r[offset + i] = right * 5.0f;
            }
            
            if (!has_output && modulations.level < 0.001f && modulations.trigger == 0.0f) {
                plaits->envelope_active = false;
            }
        } else {
            for (uint32_t i = 0; i < block_size; i++) {
                plaits->out_l[offset + i] = 0.0f;
                plaits->out_r[offset + i] = 0.0f;
            }
        }
        
        offset += block_size;
    }
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
    Plaits* plaits = (Plaits*)instance;
    if (plaits) {
        if (plaits->voice) {
            delete plaits->voice;
            plaits->voice = NULL;
        }
        free(plaits);
    }
}

static const void*
extension_data(const char* uri)
{
    return NULL;
}

static const LV2_Descriptor descriptor = {
    MI_PLAITS_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    switch (index) {
    case 0:
        return &descriptor;
    default:
        return NULL;
    }
}
