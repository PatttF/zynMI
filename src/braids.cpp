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
#include "stages/segment_generator.h"

#define MI_BRAIDS_URI "http://mutable-instruments.net/plugins/braids"

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
    BRAIDS_OUT
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
    float* out;
    
    // DSP
    braids::MacroOscillator osc;
    braids::Quantizer quantizer;
    uint8_t sync_buffer[24];
    int16_t render_buffer[24];
    
    // Stages Envelope (full segment generator)
    stages::SegmentGenerator envelope;
    stages::segment::Configuration segment_config[4];  // ADSR = 4 segments
    float envelope_buffer[8];  // BLOCK_SIZE
    stmlib::GateFlags gate_flags[8];
    stmlib::GateFlags last_gate_flag;
    int block_index;
    uint8_t velocity;
    
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
    braids->out = NULL;
    
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
        case BRAIDS_OUT:
            braids->out = (float*)data;
            break;
    }
}

static void
activate(LV2_Handle instance)
{
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    Braids* braids = (Braids*)instance;
    
    if (!braids || !braids->out || !braids->shape || !braids->timbre || !braids->color || 
        !braids->fine || !braids->coarse ||
        !braids->env_attack || !braids->env_decay || !braids->env_sustain || !braids->env_release ||
        !braids->env_attack_shape || !braids->env_decay_shape || !braids->env_release_shape) {
        for (uint32_t i = 0; i < n_samples; i++) {
            braids->out[i] = 0.0f;
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
    
    // Process audio in blocks of 24 samples
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
            if (env_block > 8) {
                env_block = 8;
            }
            for (uint32_t i = 0; i < env_block; i++) {
                bool gate_high = braids->note_on;
                stmlib::GateFlags current = gate_high ? stmlib::GATE_FLAG_HIGH : stmlib::GATE_FLAG_LOW;
                
                // Force retrigger on new note, even during legato
                if (i == 0 && new_midi_trigger) {
                    current |= stmlib::GATE_FLAG_RISING;
                    new_midi_trigger = false;
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
        
        float vel_scale = braids->velocity / 127.0f;
        float pitch_cv = 0.0f;
        float note = 0.0f;
        
        if (braids->note_on) {
            // MIDI note
            note = (float)braids->current_note + *braids->coarse + *braids->fine;
        } else {
            // CV inputs
            pitch_cv = braids->pitch_in ? braids->pitch_in[offset] : 0.0f;
            float fm_cv = braids->fm_in ? braids->fm_in[offset] : 0.0f;
            note = 60.0f + *braids->coarse + *braids->fine + pitch_cv * 12.0f + fm_cv * 12.0f;
        }
        
        // Convert to pitch (Braids internal format: 0 to 16383, where 8192 = C4)
        int32_t pitch = static_cast<int32_t>((note - 12.0f) * 128.0f);
        pitch = Clip(pitch, 0, 16383);
        braids->osc.set_pitch(pitch);
        
        memset(braids->sync_buffer, 0, block_size);
        bool has_activity = false;
        if (new_midi_trigger && offset == 0) {
            // Generate short trigger pulse at start of buffer
            for (uint32_t i = 0; i < 4 && i < block_size; i++) {
                braids->sync_buffer[i] = 255;
            }
            has_activity = true;
            new_midi_trigger = false;  // Clear flag after using it
        }
        
        // CV trigger input (fallback when no MIDI)
        if (!braids->note_on && braids->trig_in) {
            for (uint32_t i = 0; i < block_size; i++) {
                bool trig = braids->trig_in[offset + i] > 0.1f;
                braids->sync_buffer[i] = trig ? 255 : 0;
                if (trig) {
                    has_activity = true;
                    braids->gate_high = true;
                } else if (braids->trig_in[offset + i] < 0.05f) {
                    braids->gate_high = false;
                }
            }
        }
        
        if (braids->note_on || (braids->pitch_in && (braids->pitch_in[offset] != 0.0f || braids->gate_high))) {
            has_activity = true;
        }
        if (has_activity || braids->has_trigger) {
            braids->osc.Render(braids->sync_buffer, braids->render_buffer, block_size);
            braids->has_trigger = has_activity;
            
            for (uint32_t i = 0; i < block_size; i++) {
                float sample = braids->render_buffer[i] / 32768.0f;
                float env = env_values[i] * vel_scale;
                braids->out[offset + i] = sample * env;
            }
        } else {
            for (uint32_t i = 0; i < block_size; i++) {
                braids->out[offset + i] = 0.0f;
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
    free(instance);
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
