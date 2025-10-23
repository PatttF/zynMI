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

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    Plaits* plaits = (Plaits*)instance;
    
    if (!plaits || !plaits->voice || !plaits->out_l || !plaits->out_r ||
        !plaits->model || !plaits->freq || !plaits->harmonics || 
        !plaits->timbre || !plaits->morph || !plaits->lpg_color || !plaits->lpg_decay ||
        !plaits->env_attack || !plaits->env_decay || !plaits->env_sustain || !plaits->env_release ||
        !plaits->env_attack_shape || !plaits->env_decay_shape || !plaits->env_release_shape) {
        for (uint32_t i = 0; i < n_samples; i++) {
            plaits->out_l[i] = 0.0f;
            plaits->out_r[i] = 0.0f;
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
                trigger_value = plaits->trigger_in[offset];
                if (trigger_value > 0.5f && plaits->last_trigger <= 0.5f) {
                    modulations.trigger = 1.0f;
                }
                plaits->last_trigger = trigger_value;
            }
        }
        if (!plaits->note_on) {
            if (plaits->level_in) {
                modulations.level = plaits->level_in[offset];
                if (modulations.level > 0.01f) {
                    plaits->envelope_active = true;
                }
            } else if (trigger_value > 0.1f) {
                modulations.level = trigger_value;
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
            bool has_output = false;
            for (uint32_t i = 0; i < block_size; i++) {
                float out_sample = plaits->output[i].out / 32768.0f;
                float aux_sample = plaits->output[i].aux / 32768.0f;
                float env = env_values[i] * vel_scale;
                
                out_sample *= env;
                aux_sample *= env;
                float mixed = out_sample + aux_sample;
                
                if (mixed > 0.0001f || mixed < -0.0001f) {
                    has_output = true;
                }
                
                plaits->out_l[offset + i] = mixed;
                plaits->out_r[offset + i] = mixed;
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
