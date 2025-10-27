#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TEST
#include "braids/macro_oscillator.h"
#include "braids/quantizer.h"
#include "stmlib/utils/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stages/segment_generator.h"
#include "rings/dsp/part.h"
#include "rings/dsp/strummer.h"

#define MI_BRAIDS_URI "http://mutable-instruments.net/plugins/braids"
#define RINGS_BLOCK_SIZE 24
#define RINGS_TARGET_SAMPLE_RATE 48000.0

// Helper function for clipping
template<typename T>
inline T Clip(T value, T min, T max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

typedef enum {
    BRAIDS_MIDI_IN = 0,
    BRAIDS_SHAPE,
    BRAIDS_FINE,
    BRAIDS_COARSE,
    BRAIDS_TIMBRE,
    BRAIDS_COLOR,
    BRAIDS_ENV_ATTACK,
    BRAIDS_ENV_DECAY,
    BRAIDS_ENV_SUSTAIN,
    BRAIDS_ENV_RELEASE,
    BRAIDS_ENV_ATTACK_SHAPE,
    BRAIDS_ENV_DECAY_SHAPE,
    BRAIDS_ENV_RELEASE_SHAPE,
    BRAIDS_TRIG_IN,
    BRAIDS_PITCH_IN,
    BRAIDS_FM_IN,
    // Rings parameters
    BRAIDS_RINGS_ENABLE,
    BRAIDS_RINGS_POLYPHONY,
    BRAIDS_RINGS_MODEL,
    BRAIDS_RINGS_FREQUENCY,
    BRAIDS_RINGS_STRUCTURE,
    BRAIDS_RINGS_BRIGHTNESS,
    BRAIDS_RINGS_DAMPING,
    BRAIDS_RINGS_POSITION,
    // Filter parameters
    BRAIDS_FILTER_TYPE,
    BRAIDS_FILTER_CUTOFF,
    BRAIDS_FILTER_RESONANCE,
    // Outputs
    BRAIDS_OUT_L,
    BRAIDS_OUT_R
} PortIndex;

typedef struct {
    // Port buffers
    const LV2_Atom_Sequence* midi_in;
    const float* shape;
    const float* fine;
    const float* coarse;
    const float* timbre;
    const float* color;
    const float* env_attack;
    const float* env_decay;
    const float* env_sustain;
    const float* env_release;
    const float* env_attack_shape;
    const float* env_decay_shape;
    const float* env_release_shape;
    const float* trig_in;
    const float* pitch_in;
    const float* fm_in;
    // Rings parameters
    const float* rings_enable;
    const float* rings_use_internal_exciter;
    const float* rings_polyphony;
    const float* rings_model;
    const float* rings_frequency;
    const float* rings_structure;
    const float* rings_brightness;
    const float* rings_damping;
    const float* rings_position;
    // Filter parameters
    const float* filter_type;
    const float* filter_cutoff;
    const float* filter_resonance;
    // Outputs
    float* out_l;
    float* out_r;
    
    // DSP
    braids::MacroOscillator osc;
    braids::Quantizer quantizer;
    uint8_t sync_buffer[24];
    int16_t render_buffer[24];
    
    // Rings DSP
    rings::Part* rings_part;
    rings::Strummer* rings_strummer;
    uint16_t rings_reverb_buffer[32768];
    // Process Rings only when we have accumulated 24 samples
    float rings_input_buffer[24];
    float rings_output_odd_buffer[24];
    float rings_output_even_buffer[24];
    uint32_t rings_buffer_index;
    // Output ring buffers for smooth consumption (like VCV Rack's outputBuffer)
    float rings_output_ring_odd[256];  // Ring buffer for continuous output
    float rings_output_ring_even[256];
    uint32_t rings_output_write_idx;
    uint32_t rings_output_read_idx;
    bool rings_strum;
    bool rings_last_strum;
    
    // Stages Envelope (full segment generator)
    stages::SegmentGenerator envelope;
    stages::segment::Configuration segment_config[4];  // ADSR = 4 segments
    float envelope_buffer[8];  // BLOCK_SIZE
    stmlib::GateFlags gate_flags[8];
    stmlib::GateFlags last_gate_flag;
    int block_index;
    uint8_t velocity;
    
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
    float phase;
    bool has_trigger;
    bool gate_high;
} Braids;

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double rate,
            const char* bundle_path,
            const LV2_Feature* const* features)
{
    Braids* braids = (Braids*)calloc(1, sizeof(Braids));
    if (!braids) {
        return NULL;
    }
    
    braids->map = NULL;
    if (features) {
        for (int i = 0; features[i]; i++) {
            if (!strcmp(features[i]->URI, LV2_URID__map)) {
                braids->map = (LV2_URID_Map*)features[i]->data;
                break;
            }
        }
    }
    
    braids->midi_event_uri = braids->map ? 
        braids->map->map(braids->map->handle, LV2_MIDI__MidiEvent) : 0;
    
    braids->sample_rate = rate;
    braids->phase = 0.0f;
    braids->has_trigger = false;
    braids->gate_high = false;
    braids->current_note = 60;  // Middle C
    braids->note_on = false;
    braids->gate_level = 0.0f;
    braids->velocity = 100;
    
    // Initialize ADSR envelope
    braids->envelope.Init();
    braids->envelope.SetSampleRate(rate);
    
    braids->segment_config[0].type = stages::segment::TYPE_RAMP;
    braids->segment_config[0].loop = false;
    braids->segment_config[1].type = stages::segment::TYPE_RAMP;
    braids->segment_config[1].loop = false;
    braids->segment_config[2].type = stages::segment::TYPE_HOLD;
    braids->segment_config[2].loop = false;
    braids->segment_config[3].type = stages::segment::TYPE_RAMP;
    braids->segment_config[3].loop = false;
    
    braids->envelope.Configure(true, braids->segment_config, 4);
    braids->last_gate_flag = stmlib::GATE_FLAG_LOW;
    braids->block_index = 0;
    
    braids->osc.Init();
    braids->quantizer.Init();
    
    // Initialize Rings DSP
    braids->rings_part = new rings::Part();
    if (braids->rings_part) {
        memset(braids->rings_reverb_buffer, 0, sizeof(braids->rings_reverb_buffer));
        braids->rings_part->Init(braids->rings_reverb_buffer);
        braids->rings_part->set_polyphony(1);
        braids->rings_part->set_model(rings::RESONATOR_MODEL_MODAL);
    }
    
    braids->rings_strummer = new rings::Strummer();
    if (braids->rings_strummer) {
        // Strummer needs: sample_time (1/samplerate) and block_rate (samplerate/24)
        braids->rings_strummer->Init(1.0f / rate, rate / 24.0f);
    }
    
    memset(braids->rings_input_buffer, 0, sizeof(braids->rings_input_buffer));
    memset(braids->rings_output_odd_buffer, 0, sizeof(braids->rings_output_odd_buffer));
    memset(braids->rings_output_even_buffer, 0, sizeof(braids->rings_output_even_buffer));
    memset(braids->rings_output_ring_odd, 0, sizeof(braids->rings_output_ring_odd));
    memset(braids->rings_output_ring_even, 0, sizeof(braids->rings_output_ring_even));
    braids->rings_buffer_index = 0;
    // Start both pointers at same position - no fake latency buffer
    braids->rings_output_write_idx = 0;
    braids->rings_output_read_idx = 0;
    braids->rings_strum = false;
    braids->rings_last_strum = false;
    
    // Initialize filters
    braids->filter_left.Init();
    braids->filter_right.Init();
    memset(braids->ladder_state_left, 0, sizeof(braids->ladder_state_left));
    memset(braids->ladder_state_right, 0, sizeof(braids->ladder_state_right));
    memset(braids->ms20_hp_left, 0, sizeof(braids->ms20_hp_left));
    memset(braids->ms20_hp_right, 0, sizeof(braids->ms20_hp_right));
    memset(braids->ms20_lp_left, 0, sizeof(braids->ms20_lp_left));
    memset(braids->ms20_lp_right, 0, sizeof(braids->ms20_lp_right));
    memset(braids->svf_state_left, 0, sizeof(braids->svf_state_left));
    memset(braids->svf_state_right, 0, sizeof(braids->svf_state_right));
    braids->onepole_state_left = 0.0f;
    braids->onepole_state_right = 0.0f;
    
    braids->midi_in = NULL;
    braids->shape = NULL;
    braids->fine = NULL;
    braids->coarse = NULL;
    braids->timbre = NULL;
    braids->color = NULL;
    braids->trig_in = NULL;
    braids->pitch_in = NULL;
    braids->fm_in = NULL;
    braids->env_attack = NULL;
    braids->env_decay = NULL;
    braids->env_sustain = NULL;
    braids->env_release = NULL;
    braids->env_attack_shape = NULL;
    braids->env_decay_shape = NULL;
    braids->env_release_shape = NULL;
    braids->out_l = NULL;
    braids->out_r = NULL;
    braids->rings_enable = NULL;
    braids->rings_polyphony = NULL;
    braids->rings_model = NULL;
    braids->rings_frequency = NULL;
    braids->rings_structure = NULL;
    braids->rings_brightness = NULL;
    braids->rings_damping = NULL;
    braids->rings_position = NULL;
    braids->filter_type = NULL;
    braids->filter_cutoff = NULL;
    braids->filter_resonance = NULL;
    
    return (LV2_Handle)braids;
}

static void
connect_port(LV2_Handle instance,
             uint32_t port,
             void* data)
{
    Braids* braids = (Braids*)instance;
    
    switch ((PortIndex)port) {
        case BRAIDS_MIDI_IN:
            braids->midi_in = (const LV2_Atom_Sequence*)data;
            break;
        case BRAIDS_SHAPE:
            braids->shape = (const float*)data;
            break;
        case BRAIDS_FINE:
            braids->fine = (const float*)data;
            break;
        case BRAIDS_COARSE:
            braids->coarse = (const float*)data;
            break;
        case BRAIDS_TIMBRE:
            braids->timbre = (const float*)data;
            break;
        case BRAIDS_COLOR:
            braids->color = (const float*)data;
            break;
        case BRAIDS_ENV_ATTACK:
            braids->env_attack = (const float*)data;
            break;
        case BRAIDS_ENV_DECAY:
            braids->env_decay = (const float*)data;
            break;
        case BRAIDS_ENV_SUSTAIN:
            braids->env_sustain = (const float*)data;
            break;
        case BRAIDS_ENV_RELEASE:
            braids->env_release = (const float*)data;
            break;
        case BRAIDS_ENV_ATTACK_SHAPE:
            braids->env_attack_shape = (const float*)data;
            break;
        case BRAIDS_ENV_DECAY_SHAPE:
            braids->env_decay_shape = (const float*)data;
            break;
        case BRAIDS_ENV_RELEASE_SHAPE:
            braids->env_release_shape = (const float*)data;
            break;
        case BRAIDS_TRIG_IN:
            braids->trig_in = (const float*)data;
            break;
        case BRAIDS_PITCH_IN:
            braids->pitch_in = (const float*)data;
            break;
        case BRAIDS_FM_IN:
            braids->fm_in = (const float*)data;
            break;
        case BRAIDS_RINGS_ENABLE:
            braids->rings_enable = (const float*)data;
            break;
        case BRAIDS_RINGS_POLYPHONY:
            braids->rings_polyphony = (const float*)data;
            break;
        case BRAIDS_RINGS_MODEL:
            braids->rings_model = (const float*)data;
            break;
        case BRAIDS_RINGS_FREQUENCY:
            braids->rings_frequency = (const float*)data;
            break;
        case BRAIDS_RINGS_STRUCTURE:
            braids->rings_structure = (const float*)data;
            break;
        case BRAIDS_RINGS_BRIGHTNESS:
            braids->rings_brightness = (const float*)data;
            break;
        case BRAIDS_RINGS_DAMPING:
            braids->rings_damping = (const float*)data;
            break;
        case BRAIDS_RINGS_POSITION:
            braids->rings_position = (const float*)data;
            break;
        case BRAIDS_FILTER_TYPE:
            braids->filter_type = (const float*)data;
            break;
        case BRAIDS_FILTER_CUTOFF:
            braids->filter_cutoff = (const float*)data;
            break;
        case BRAIDS_FILTER_RESONANCE:
            braids->filter_resonance = (const float*)data;
            break;
        case BRAIDS_OUT_L:
            braids->out_l = (float*)data;
            break;
        case BRAIDS_OUT_R:
            braids->out_r = (float*)data;
            break;
    }
}

static void
activate(LV2_Handle instance)
{
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
    Braids* braids = (Braids*)instance;
    
    if (!braids || !braids->out_l || !braids->out_r || !braids->shape || !braids->timbre || !braids->color || 
        !braids->fine || !braids->coarse ||
        !braids->env_attack || !braids->env_decay || !braids->env_sustain || !braids->env_release ||
        !braids->env_attack_shape || !braids->env_decay_shape || !braids->env_release_shape) {
        if (braids && braids->out_l && braids->out_r) {
            for (uint32_t i = 0; i < n_samples; i++) {
                braids->out_l[i] = 0.0f;
                braids->out_r[i] = 0.0f;
            }
        }
        return;
    }
    
    bool new_midi_trigger = false;
    if (braids->midi_event_uri && braids->midi_in && braids->midi_in->atom.size > 0) {
        LV2_ATOM_SEQUENCE_FOREACH(braids->midi_in, ev) {
            if (ev->body.type == braids->midi_event_uri) {
            const uint8_t* const msg = (const uint8_t*)(ev + 1);
            
            switch (lv2_midi_message_type(msg)) {
                case LV2_MIDI_MSG_NOTE_ON:
                    if (msg[2] > 0) {
                        braids->current_note = msg[1];
                        braids->velocity = msg[2];
                        braids->note_on = true;
                        braids->gate_level = 0.8f;
                        braids->gate_high = true;
                        new_midi_trigger = true;
                    } else {
                        if (msg[1] == braids->current_note) {
                            braids->note_on = false;
                            braids->gate_level = 0.0f;
                            braids->gate_high = false;
                        }
                    }
                    break;
                    
                case LV2_MIDI_MSG_NOTE_OFF:
                    if (msg[1] == braids->current_note) {
                        braids->note_on = false;
                        braids->gate_level = 0.0f;
                        braids->gate_high = false;
                    }
                    break;
            }
        }
        }
    }
    
    // Update oscillator parameters
    int shape = (int)(*braids->shape);
    if (shape < 0) shape = 0;
    if (shape > 47) shape = 47;
    braids->osc.set_shape((braids::MacroOscillatorShape)shape);
    
    // Set timbre and color
    int16_t timbre = (int16_t)(*braids->timbre * 32767.0f);
    int16_t color = (int16_t)(*braids->color * 32767.0f);
    braids->osc.set_parameters(timbre, color);
    
    // Configure ADSR envelope parameters
    braids->envelope.set_segment_parameters(0, *braids->env_attack, *braids->env_attack_shape);
    braids->envelope.set_segment_parameters(1, *braids->env_decay, *braids->env_decay_shape);
    braids->envelope.set_segment_parameters(2, *braids->env_sustain, 0.0f);
    braids->envelope.set_segment_parameters(3, *braids->env_release, *braids->env_release_shape);
    
    // Check if Rings is enabled
    bool rings_enabled = braids->rings_enable && *braids->rings_enable > 0.5f;
    bool use_internal_exciter = rings_enabled && braids->rings_use_internal_exciter && 
                                *braids->rings_use_internal_exciter > 0.5f;
    
    // Update Rings parameters if enabled
    if (rings_enabled && braids->rings_part && braids->rings_strummer) {
        int poly = braids->rings_polyphony ? (int)(*braids->rings_polyphony) : 1;
        if (poly >= 1 && poly <= 4) {
            braids->rings_part->set_polyphony(poly);
        }
        
        int model = braids->rings_model ? (int)(*braids->rings_model) : 0;
        if (model >= 0 && model < rings::RESONATOR_MODEL_LAST) {
            braids->rings_part->set_model((rings::ResonatorModel)model);
        }
    }
    
    // Track strum events for Rings
    bool strum_event = false;
    if (new_midi_trigger) {
        strum_event = true;
    }
    
    // Process audio in blocks of 24 samples (required by both Braids and Rings)
    uint32_t offset = 0;
    while (offset < n_samples) {
        uint32_t block_size = n_samples - offset;
        if (block_size > 24) {
            block_size = 24;
        }
        
        // Calculate pitch/note for this block
        float pitch_cv = 0.0f;
        float note = 0.0f;
        
        if (braids->note_on) {
            // MIDI note
            note = (float)braids->current_note + *braids->coarse + *braids->fine;
        } else {
            // CV inputs (1V/Oct pitch, FM scaled)
            pitch_cv = braids->pitch_in ? braids->pitch_in[offset] : 0.0f;
            float fm_cv = braids->fm_in ? braids->fm_in[offset] : 0.0f;
            note = 60.0f + *braids->coarse + *braids->fine + pitch_cv * 12.0f + fm_cv * 12.0f;
        }
        
        // Convert to pitch (Braids internal format: 0 to 16383, where 8192 = C4)
        int32_t pitch = static_cast<int32_t>((note - 12.0f) * 128.0f);
        pitch = Clip(pitch, 0, 16383);
        braids->osc.set_pitch(pitch);
        
        // Setup sync/trigger buffer for Braids
        bool has_activity = false;
        bool has_midi_trigger = (new_midi_trigger && offset == 0);
        
        memset(braids->sync_buffer, 0, block_size);
        
        if (has_midi_trigger) {
            for (uint32_t i = 0; i < 4 && i < block_size; i++) {
                braids->sync_buffer[i] = 255;
            }
            has_activity = true;
            new_midi_trigger = false;
        }
        else if (!braids->note_on && braids->trig_in) {
            for (uint32_t i = 0; i < block_size; i++) {
                bool trig = braids->trig_in[offset + i] > 0.2f;
                braids->sync_buffer[i] = trig ? 255 : 0;
                if (trig) {
                    has_activity = true;
                    braids->gate_high = true;
                } else if (braids->trig_in[offset + i] < 0.1f) {
                    braids->gate_high = false;
                }
            }
        }
        
        if (braids->note_on || (braids->pitch_in && (braids->pitch_in[offset] != 0.0f || braids->gate_high))) {
            has_activity = true;
        }
        
        // Generate Braids oscillator output (no envelope yet!)
        float braids_output[24];
        memset(braids_output, 0, sizeof(braids_output));
        
        // CRITICAL: Only generate Braids output if NOT using Rings internal exciter
        // When internal exciter is enabled, Rings generates its own sound
        if (!use_internal_exciter && (has_activity || braids->has_trigger)) {
            braids->osc.Render(braids->sync_buffer, braids->render_buffer, block_size);
            braids->has_trigger = has_activity;
            
            for (uint32_t i = 0; i < block_size; i++) {
                braids_output[i] = braids->render_buffer[i] / 32768.0f;  // ±1.0 range
            }
        }
        
        // Process through Rings if enabled (BEFORE envelope!)
        float rings_out_odd[24];
        float rings_out_even[24];
        
        if (rings_enabled && braids->rings_part && braids->rings_strummer) {
            // Process each sample through Rings
            for (uint32_t i = 0; i < block_size; i++) {
                // Add sample to input buffer
                // When using internal exciter, input buffer should be zeros (Strummer will generate)
                // When using external exciter, input buffer gets Braids audio
                // BOOST: Multiply by 2.0 to give Rings a stronger excitation signal
                // Rings expects energetic input to drive the resonators properly
                float exciter_gain = use_internal_exciter ? 1.0f : 2.0f;
                braids->rings_input_buffer[braids->rings_buffer_index] = braids_output[i] * exciter_gain;
                braids->rings_buffer_index++;
                
                // When we have 24 samples, process through Rings
                if (braids->rings_buffer_index >= 24) {
                    // Prepare Rings patch
                    rings::Patch patch;
                    float structure = braids->rings_structure ? *braids->rings_structure : 0.5f;
                    patch.structure = structure < 0.0f ? 0.0f : (structure > 0.9995f ? 0.9995f : structure);
                    float brightness = braids->rings_brightness ? *braids->rings_brightness : 0.5f;
                    patch.brightness = brightness < 0.0f ? 0.0f : (brightness > 1.0f ? 1.0f : brightness);
                    float damping = braids->rings_damping ? *braids->rings_damping : 0.5f;
                    patch.damping = damping < 0.0f ? 0.0f : (damping > 0.9995f ? 0.9995f : damping);
                    float position = braids->rings_position ? *braids->rings_position : 0.5f;
                    patch.position = position < 0.0f ? 0.0f : (position > 0.9995f ? 0.9995f : position);
                    
                    // Prepare performance state
                    rings::PerformanceState performance_state;
                    performance_state.internal_exciter = use_internal_exciter;
                    performance_state.internal_strum = use_internal_exciter;
                    performance_state.internal_note = false;
                    
                    // Detect strum (note on/off transitions)
                    // CRITICAL: When using external exciter, we MUST trigger strum
                    // or Rings won't respond to the audio input!
                    bool current_strum = braids->note_on;
                    bool strum_trigger = (current_strum && !braids->rings_last_strum) || strum_event;
                    
                    // If NOT using internal exciter, ALWAYS strum on note events
                    // This ensures Rings responds to the Braids audio
                    if (!use_internal_exciter && strum_trigger) {
                        performance_state.strum = true;
                    } else {
                        performance_state.strum = strum_trigger;
                    }
                    
                    braids->rings_last_strum = current_strum;
                    strum_event = false;
                    
                    // CRITICAL: Rings pitch calculation
                    // Rings uses MIDI note numbers where the final pitch is: note + tonic
                    // VCV Rack sets: note = CV in semitones, tonic = 12.0 + transpose
                    // Since 'note' is already in MIDI note numbers (60 = C4), we just use it directly
                    // rings_frequency knob acts as transpose: 0.0-1.0 maps to -24 to +24 semitones
                    float freq_transpose = 0.0f;
                    if (braids->rings_frequency) {
                        // Map 0.5 = no transpose, 0.0 = -24 semitones, 1.0 = +24 semitones
                        freq_transpose = (*braids->rings_frequency - 0.5f) * 48.0f;
                    }
                    performance_state.note = note;
                    performance_state.tonic = freq_transpose;
                    performance_state.fm = 0.0f;
                    
                    // Calculate chord from structure
                    const int kNumChords = 11;
                    performance_state.chord = (int)(patch.structure * (kNumChords - 1));
                    if (performance_state.chord < 0) performance_state.chord = 0;
                    if (performance_state.chord >= kNumChords) performance_state.chord = kNumChords - 1;
                    
                    // CRITICAL: Strummer processes the input buffer
                    // When using external exciter (Braids audio), pass NULL to prevent adding synthetic clicks
                    // When using internal exciter, Strummer generates the excitation signal
                    if (use_internal_exciter) {
                        braids->rings_strummer->Process(NULL, 24, &performance_state);
                    } else {
                        // External exciter: Strummer still processes to detect note changes, but doesn't add clicks
                        braids->rings_strummer->Process(braids->rings_input_buffer, 24, &performance_state);
                    }
                    
                    // Process through Rings - ALWAYS exactly 24 samples
                    braids->rings_part->Process(performance_state, patch, braids->rings_input_buffer, 
                                               braids->rings_output_odd_buffer, braids->rings_output_even_buffer, 24);
                    
                    // Add processed samples to output ring buffers
                    for (int j = 0; j < 24; j++) {
                        braids->rings_output_ring_odd[braids->rings_output_write_idx] = braids->rings_output_odd_buffer[j];
                        braids->rings_output_ring_even[braids->rings_output_write_idx] = braids->rings_output_even_buffer[j];
                        braids->rings_output_write_idx = (braids->rings_output_write_idx + 1) % 256;
                    }
                    
                    // Reset buffer index
                    braids->rings_buffer_index = 0;
                }
                
                // Read from output ring buffer - check if we have samples available
                uint32_t available;
                if (braids->rings_output_write_idx >= braids->rings_output_read_idx) {
                    available = braids->rings_output_write_idx - braids->rings_output_read_idx;
                } else {
                    available = 256 - braids->rings_output_read_idx + braids->rings_output_write_idx;
                }
                
                // Only read if we have samples, otherwise output silence
                if (available > 0) {
                    rings_out_odd[i] = braids->rings_output_ring_odd[braids->rings_output_read_idx] * 0.8f;
                    rings_out_even[i] = braids->rings_output_ring_even[braids->rings_output_read_idx] * 0.8f;
                    braids->rings_output_read_idx = (braids->rings_output_read_idx + 1) % 256;
                } else {
                    // No samples available - output previous sample (zero-order hold)
                    uint32_t prev_idx = (braids->rings_output_read_idx > 0) ? (braids->rings_output_read_idx - 1) : 255;
                    rings_out_odd[i] = braids->rings_output_ring_odd[prev_idx] * 0.8f;
                    rings_out_even[i] = braids->rings_output_ring_even[prev_idx] * 0.8f;
                }
            }
            
            // Apply mono mixing: mix both channels and duplicate to left/right
            // This prevents harshness and phase issues from dual-harmonic stereo
            for (uint32_t i = 0; i < block_size; i++) {
                float mixed = (rings_out_odd[i] + rings_out_even[i]) * 0.5f;
                rings_out_odd[i] = mixed;
                rings_out_even[i] = mixed;
            }
        } else {
            // Rings disabled - use Braids output directly (mono to both channels)
            for (uint32_t i = 0; i < block_size; i++) {
                rings_out_odd[i] = braids_output[i] * 0.7f;
                rings_out_even[i] = braids_output[i] * 0.7f;
            }
        }
        
        // NOW apply ADSR envelope to the final output (Braids or Rings)
        float env_values[24];
        uint32_t env_offset = 0;
        
        while (env_offset < block_size) {
            uint32_t env_block = block_size - env_offset;
            if (env_block > 8) {
                env_block = 8;
            }
            
            for (uint32_t i = 0; i < env_block; i++) {
                bool gate_high = braids->note_on;
                stmlib::GateFlags current = gate_high ? stmlib::GATE_FLAG_HIGH : stmlib::GATE_FLAG_LOW;
                
                if (i == 0 && has_midi_trigger && env_offset == 0) {
                    current |= stmlib::GATE_FLAG_RISING;
                } else if (gate_high && !(braids->last_gate_flag & stmlib::GATE_FLAG_HIGH)) {
                    current |= stmlib::GATE_FLAG_RISING;
                }
                
                if (!gate_high && (braids->last_gate_flag & stmlib::GATE_FLAG_HIGH)) {
                    current |= stmlib::GATE_FLAG_FALLING;
                }
                
                braids->gate_flags[i] = current;
                braids->last_gate_flag = current;
            }
            
            stages::SegmentGenerator::Output env_out[8];
            braids->envelope.Process(braids->gate_flags, env_out, env_block);
            for (uint32_t i = 0; i < env_block; i++) {
                env_values[env_offset + i] = env_out[i].value;
            }
            
            env_offset += env_block;
        }
        
        // Apply envelope and velocity, then scale to ±5V
        float vel_scale = braids->velocity / 127.0f;
        
        // Get filter parameters
        int filter_type = 0;  // 0=off, 1=moog, 2=ms20
        float filter_cutoff = 0.5f;
        float filter_resonance = 0.0f;
        
        if (braids->filter_type) {
            filter_type = (int)(*braids->filter_type);
        }
        if (braids->filter_cutoff) {
            filter_cutoff = *braids->filter_cutoff;
        }
        if (braids->filter_resonance) {
            filter_resonance = *braids->filter_resonance;
        }
        
        // Clamp parameters
        filter_cutoff = Clip(filter_cutoff, 0.001f, 0.999f);
        filter_resonance = Clip(filter_resonance, 0.0f, 1.0f);
        
        for (uint32_t i = 0; i < block_size; i++) {
            float left = rings_out_odd[i];
            float right = rings_out_even[i];
            
            // Apply pre-envelope filter
            if (filter_type > 0) {
                if (filter_type == 1) {
                    // Moog ladder filter
                    left = ProcessMoogLadder(left, braids->ladder_state_left, filter_cutoff, filter_resonance);
                    right = ProcessMoogLadder(right, braids->ladder_state_right, filter_cutoff, filter_resonance);
                } else if (filter_type == 2) {
                    // MS20 filter
                    left = ProcessMS20Filter(left, braids->ms20_hp_left, braids->ms20_lp_left, filter_cutoff, filter_resonance);
                    right = ProcessMS20Filter(right, braids->ms20_hp_right, braids->ms20_lp_right, filter_cutoff, filter_resonance);
                } else if (filter_type == 3) {
                    // SVF Lowpass
                    left = ProcessSVFLowpass(left, braids->svf_state_left, filter_cutoff, filter_resonance);
                    right = ProcessSVFLowpass(right, braids->svf_state_right, filter_cutoff, filter_resonance);
                } else if (filter_type == 4) {
                    // SVF Bandpass
                    left = ProcessSVFBandpass(left, braids->svf_state_left, filter_cutoff, filter_resonance);
                    right = ProcessSVFBandpass(right, braids->svf_state_right, filter_cutoff, filter_resonance);
                } else if (filter_type == 5) {
                    // SVF Highpass
                    left = ProcessSVFHighpass(left, braids->svf_state_left, filter_cutoff, filter_resonance);
                    right = ProcessSVFHighpass(right, braids->svf_state_right, filter_cutoff, filter_resonance);
                } else if (filter_type == 6) {
                    // One-Pole Lowpass (no resonance)
                    left = ProcessOnePole(left, &braids->onepole_state_left, filter_cutoff);
                    right = ProcessOnePole(right, &braids->onepole_state_right, filter_cutoff);
                }
            }
            
            // Apply envelope
            float env = env_values[i] * vel_scale;
            left *= env;
            right *= env;
            
            // Scale to ±5V
            braids->out_l[offset + i] = left * 10.0f;
            braids->out_r[offset + i] = right * 10.0f;
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
    Braids* braids = (Braids*)instance;
    if (braids) {
        if (braids->rings_part) {
            delete braids->rings_part;
        }
        if (braids->rings_strummer) {
            delete braids->rings_strummer;
        }
        free(braids);
    }
}

static const void*
extension_data(const char* uri)
{
    return NULL;
}

static const LV2_Descriptor descriptor = {
    MI_BRAIDS_URI,
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

