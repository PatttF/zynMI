#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>

#define LV2_TIME__Position "http://lv2plug.in/ns/ext/time#Position"
#define LV2_TIME__beatsPerMinute "http://lv2plug.in/ns/ext/time#beatsPerMinute"
#define LV2_TIME__speed "http://lv2plug.in/ns/ext/time#speed"
#define LV2_TIME__frame "http://lv2plug.in/ns/ext/time#frame"

#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "marbles/ramp/ramp_extractor.h"

#define MI_MARBLES_URI "http://mutable-instruments.net/plugins/marbles"

static const int BLOCK_SIZE = 8;

typedef enum {
    MARBLES_MIDI_IN = 0,
    MARBLES_MIDI_OUT,
    // T Generator controls
    MARBLES_T_RATE,
    MARBLES_T_BIAS,
    MARBLES_T_JITTER,
    MARBLES_T_MODE,
    MARBLES_T_RANGE,
    // X Generator controls  
    MARBLES_X_SPREAD,
    MARBLES_X_BIAS,
    MARBLES_X_STEPS,
    MARBLES_X_MODE,
    MARBLES_X_RANGE,
    MARBLES_X_CLOCK_SOURCE,
    MARBLES_X_DEJA_VU,
    MARBLES_X_LENGTH,
    MARBLES_X_SCALE,
    // Y Generator controls
    MARBLES_Y_SPREAD,
    MARBLES_Y_BIAS,
    MARBLES_Y_STEPS,
    MARBLES_Y_MODE,
    MARBLES_Y_RANGE,
    MARBLES_Y_CLOCK_SOURCE,
    MARBLES_Y_DEJA_VU,
    MARBLES_Y_LENGTH,
    MARBLES_Y_SCALE
} PortIndex;

typedef struct {
    // Port buffers
    const LV2_Atom_Sequence* midi_in;
    LV2_Atom_Sequence* midi_out;
    
    // T Generator
    const float* t_rate;
    const float* t_bias;
    const float* t_jitter;
    const float* t_mode;
    const float* t_range;
    
    // X Generator
    const float* x_spread;
    const float* x_bias;
    const float* x_steps;
    const float* x_mode;
    const float* x_range;
    const float* x_clock_source;
    const float* x_deja_vu;
    const float* x_length;
    const float* x_scale;
    
    // Y Generator
    const float* y_spread;
    const float* y_bias;
    const float* y_steps;
    const float* y_mode;
    const float* y_range;
    const float* y_clock_source;
    const float* y_deja_vu;
    const float* y_length;
    const float* y_scale;
    
    // DSP
    marbles::TGenerator* t_generator;
    marbles::XYGenerator* xy_generator;
    marbles::RampExtractor* ramp_extractor;
    marbles::RandomGenerator random_generator;
    marbles::RandomStream random_stream;
    
    // State
    float internal_clock_phase;
    float internal_clock_frequency;
    float host_bpm;
    bool host_transport_rolling;
    bool last_clock_state;
    bool last_t_gates[2];  // Only 2 T channels exist
    uint8_t last_cc_values[4];  // Track last CC values to avoid flooding
    uint32_t cc_update_counter;
    stmlib::GateFlags gate_flags[BLOCK_SIZE];
    float ramps_external[BLOCK_SIZE];
    float ramps_master[BLOCK_SIZE];
    float ramps_slave[marbles::kNumTChannels][BLOCK_SIZE];
    marbles::Ramps ramps;
    bool t_gates[marbles::kNumTChannels * BLOCK_SIZE];
    float xy_output[4 * BLOCK_SIZE];
    
    // MIDI state
    LV2_URID_Map* map;
    LV2_Atom_Forge forge;
    LV2_URID midi_event_uri;
    LV2_URID atom_blank_uri;
    LV2_URID time_position_uri;
    LV2_URID time_bpm_uri;
    LV2_URID time_speed_uri;
    LV2_URID time_frame_uri;
    bool midi_clock_running;
    uint32_t midi_clock_counter;
    uint32_t frame_offset;
    
    double sample_rate;
} Marbles;

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double rate,
            const char* bundle_path,
            const LV2_Feature* const* features)
{
    Marbles* marbles = (Marbles*)calloc(1, sizeof(Marbles));
    if (!marbles) {
        return NULL;
    }
    
    marbles->map = NULL;
    if (features) {
        for (int i = 0; features[i]; i++) {
            if (!strcmp(features[i]->URI, LV2_URID__map)) {
                marbles->map = (LV2_URID_Map*)features[i]->data;
                break;
            }
        }
    }
    
    if (!marbles->map) {
        free(marbles);
        return NULL;
    }
    
    lv2_atom_forge_init(&marbles->forge, marbles->map);
    marbles->midi_event_uri = marbles->map->map(marbles->map->handle, LV2_MIDI__MidiEvent);
    marbles->atom_blank_uri = marbles->map->map(marbles->map->handle, LV2_ATOM__Blank);
    marbles->time_position_uri = marbles->map->map(marbles->map->handle, LV2_TIME__Position);
    marbles->time_bpm_uri = marbles->map->map(marbles->map->handle, LV2_TIME__beatsPerMinute);
    marbles->time_speed_uri = marbles->map->map(marbles->map->handle, LV2_TIME__speed);
    marbles->time_frame_uri = marbles->map->map(marbles->map->handle, LV2_TIME__frame);
    
    marbles->sample_rate = rate;
    marbles->internal_clock_phase = 0.0f;
    marbles->internal_clock_frequency = 2.0f;
    marbles->host_bpm = 120.0f;
    marbles->host_transport_rolling = false;
    marbles->last_clock_state = false;
    marbles->last_t_gates[0] = false;
    marbles->last_t_gates[1] = false;
    marbles->last_cc_values[0] = 0;
    marbles->last_cc_values[1] = 0;
    marbles->last_cc_values[2] = 0;
    marbles->last_cc_values[3] = 0;
    marbles->cc_update_counter = 0;
    marbles->midi_clock_running = false;
    marbles->midi_clock_counter = 0;
    marbles->frame_offset = 0;
    
    // Initialize ramps structure
    marbles->ramps.external = marbles->ramps_external;
    marbles->ramps.master = marbles->ramps_master;
    for (int i = 0; i < marbles::kNumTChannels; i++) {
        marbles->ramps.slave[i] = marbles->ramps_slave[i];
    }
    
    // Initialize random generator and stream
    marbles->random_generator.Init(0x21);
    marbles->random_stream.Init(&marbles->random_generator);
    
    // Initialize T generator
    marbles->t_generator = new marbles::TGenerator();
    marbles->t_generator->Init(&marbles->random_stream, rate);
    
    // Initialize XY generator
    marbles->xy_generator = new marbles::XYGenerator();
    marbles->xy_generator->Init(&marbles->random_stream, rate);
    
    // Initialize ramp extractor
    marbles->ramp_extractor = new marbles::RampExtractor();
    marbles->ramp_extractor->Init(rate);
    
    return (LV2_Handle)marbles;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    Marbles* marbles = (Marbles*)instance;
    
    switch ((PortIndex)port) {
        case MARBLES_MIDI_IN:
            marbles->midi_in = (const LV2_Atom_Sequence*)data;
            break;
        case MARBLES_MIDI_OUT:
            marbles->midi_out = (LV2_Atom_Sequence*)data;
            break;
        // T Generator
        case MARBLES_T_RATE:
            marbles->t_rate = (const float*)data;
            break;
        case MARBLES_T_BIAS:
            marbles->t_bias = (const float*)data;
            break;
        case MARBLES_T_JITTER:
            marbles->t_jitter = (const float*)data;
            break;
        case MARBLES_T_MODE:
            marbles->t_mode = (const float*)data;
            break;
        case MARBLES_T_RANGE:
            marbles->t_range = (const float*)data;
            break;
        // X Generator
        case MARBLES_X_SPREAD:
            marbles->x_spread = (const float*)data;
            break;
        case MARBLES_X_BIAS:
            marbles->x_bias = (const float*)data;
            break;
        case MARBLES_X_STEPS:
            marbles->x_steps = (const float*)data;
            break;
        case MARBLES_X_MODE:
            marbles->x_mode = (const float*)data;
            break;
        case MARBLES_X_RANGE:
            marbles->x_range = (const float*)data;
            break;
        case MARBLES_X_CLOCK_SOURCE:
            marbles->x_clock_source = (const float*)data;
            break;
        case MARBLES_X_DEJA_VU:
            marbles->x_deja_vu = (const float*)data;
            break;
        case MARBLES_X_LENGTH:
            marbles->x_length = (const float*)data;
            break;
        case MARBLES_X_SCALE:
            marbles->x_scale = (const float*)data;
            break;
        // Y Generator
        case MARBLES_Y_SPREAD:
            marbles->y_spread = (const float*)data;
            break;
        case MARBLES_Y_BIAS:
            marbles->y_bias = (const float*)data;
            break;
        case MARBLES_Y_STEPS:
            marbles->y_steps = (const float*)data;
            break;
        case MARBLES_Y_MODE:
            marbles->y_mode = (const float*)data;
            break;
        case MARBLES_Y_RANGE:
            marbles->y_range = (const float*)data;
            break;
        case MARBLES_Y_CLOCK_SOURCE:
            marbles->y_clock_source = (const float*)data;
            break;
        case MARBLES_Y_DEJA_VU:
            marbles->y_deja_vu = (const float*)data;
            break;
        case MARBLES_Y_LENGTH:
            marbles->y_length = (const float*)data;
            break;
        case MARBLES_Y_SCALE:
            marbles->y_scale = (const float*)data;
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
    Marbles* marbles = (Marbles*)instance;
    
    // Get MIDI output capacity
    const uint32_t out_capacity = marbles->midi_out->atom.size;
    
    // Initialize forge to write to MIDI output
    lv2_atom_forge_set_buffer(&marbles->forge, (uint8_t*)marbles->midi_out, out_capacity);
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_sequence_head(&marbles->forge, &frame, 0);
    
    // Process input events (MIDI clock and time position)
    if (marbles->midi_in && marbles->midi_in->atom.size > 0) {
        LV2_ATOM_SEQUENCE_FOREACH(marbles->midi_in, ev) {
            if (ev->body.type == marbles->midi_event_uri) {
                // Handle MIDI clock
                const uint8_t* const msg = (const uint8_t*)(ev + 1);
                if ((msg[0] & 0xF0) == 0xF0) {
                    if (msg[0] == 0xF8) {  // MIDI Clock
                        marbles->midi_clock_counter++;
                        if (marbles->midi_clock_counter >= 24) {
                            marbles->midi_clock_counter = 0;
                        }
                    } else if (msg[0] == 0xFA || msg[0] == 0xFB) {  // Start/Continue
                        marbles->midi_clock_running = true;
                        marbles->midi_clock_counter = 0;
                    } else if (msg[0] == 0xFC) {  // Stop
                        marbles->midi_clock_running = false;
                    }
                }
            } else if (ev->body.type == marbles->atom_blank_uri || 
                       ev->body.type == marbles->time_position_uri) {
                // Handle time position from host
                const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;
                
                // Read BPM
                const LV2_Atom* bpm = NULL;
                lv2_atom_object_get(obj, marbles->time_bpm_uri, &bpm, NULL);
                if (bpm && bpm->type == marbles->forge.Float) {
                    marbles->host_bpm = ((const LV2_Atom_Float*)bpm)->body;
                }
                
                // Read transport speed (0 = stopped, 1 = playing)
                const LV2_Atom* speed = NULL;
                lv2_atom_object_get(obj, marbles->time_speed_uri, &speed, NULL);
                if (speed && speed->type == marbles->forge.Float) {
                    marbles->host_transport_rolling = (((const LV2_Atom_Float*)speed)->body > 0.0f);
                }
            }
        }
    }
    
    // Process in blocks
    uint32_t offset = 0;
    while (offset < n_samples) {
        uint32_t block_size = (n_samples - offset < BLOCK_SIZE) ? 
            (n_samples - offset) : BLOCK_SIZE;
        
        // Read T generator parameters
        float t_rate = marbles->t_rate ? *marbles->t_rate : 0.5f;
        float t_bias = marbles->t_bias ? *marbles->t_bias : 0.5f;
        float t_jitter = marbles->t_jitter ? *marbles->t_jitter : 0.0f;
        int t_mode = marbles->t_mode ? (int)(*marbles->t_mode) : 0;
        int t_range = marbles->t_range ? (int)(*marbles->t_range) : 1;  // Default to Medium (1x)
        
        // Calculate effective rate based on host tempo or internal clock
        // The T generator expects rate in semitones, not 0-1
        // Map our 0-1 parameter to -48 to +48 semitones (8 octaves range)
        float rate_semitones = (t_rate - 0.5f) * 96.0f;
        
        if (marbles->host_transport_rolling && marbles->host_bpm > 0.0f) {
            // Map t_rate to tempo sync (0.25x to 4x multiplier on host BPM)
            float tempo_multiplier = 0.25f + t_rate * 3.75f;
            float clock_hz = (marbles->host_bpm / 60.0f) * tempo_multiplier;
            // Convert Hz to semitones relative to base frequency of 2Hz at t_range=1x
            // semitones = 12 * log2(clock_hz / 2.0)
            rate_semitones = 12.0f * log2f(clock_hz / 2.0f);
        }
        
        // Set T generator parameters
        marbles->t_generator->set_model((marbles::TGeneratorModel)t_mode);
        marbles->t_generator->set_range((marbles::TGeneratorRange)t_range);
        marbles->t_generator->set_rate(rate_semitones);
        marbles->t_generator->set_bias(t_bias);
        marbles->t_generator->set_jitter(t_jitter);
        
        // Initialize ramps structure (not used for internal clock, but needed for interface)
        for (size_t i = 0; i < block_size; i++) {
            marbles->ramps_external[i] = 0.0f;
            marbles->ramps_master[i] = 0.0f;
            for (int j = 0; j < marbles::kNumTChannels; j++) {
                marbles->ramps_slave[j][i] = 0.0f;
            }
            // Initialize gate flags to low for XY generator
            marbles->gate_flags[i] = stmlib::GATE_FLAG_LOW;
        }
        
        // Process T generator with internal clock
        marbles->t_generator->Process(
            false,  // use_external_clock = false (use internal clock)
            nullptr,  // no external clock gates needed
            marbles->ramps,  // ramps structure (T generator will fill slave ramps)
            marbles->t_gates,
            block_size
        );
        
        // Generate gate flags from T gate outputs for XY generator
        // The XY generator needs clock triggers to generate new values
        for (size_t i = 0; i < block_size; i++) {
            // Use T1 gate to generate rising/falling edges
            bool t1_gate = marbles->t_gates[i];
            bool prev_gate = (i > 0) ? marbles->t_gates[i - 1] : marbles->last_t_gates[0];
            
            if (t1_gate && !prev_gate) {
                marbles->gate_flags[i] = stmlib::GATE_FLAG_RISING;
            } else if (!t1_gate && prev_gate) {
                marbles->gate_flags[i] = stmlib::GATE_FLAG_FALLING;
            } else {
                marbles->gate_flags[i] = t1_gate ? 
                    stmlib::GATE_FLAG_HIGH : stmlib::GATE_FLAG_LOW;
            }
        }
        
        // Read X/Y generator parameters
        marbles::GroupSettings x_settings;
        x_settings.control_mode = (marbles::ControlMode)(marbles->x_mode ? 
            (int)(*marbles->x_mode) : 0);
        x_settings.voltage_range = (marbles::VoltageRange)(marbles->x_range ? 
            (int)(*marbles->x_range) : 2);
        x_settings.register_mode = false;
        x_settings.register_value = 0.0f;
        x_settings.spread = marbles->x_spread ? *marbles->x_spread : 0.0f;
        x_settings.bias = marbles->x_bias ? *marbles->x_bias : 0.5f;
        x_settings.steps = marbles->x_steps ? *marbles->x_steps : 0.0f;
        x_settings.deja_vu = marbles->x_deja_vu ? *marbles->x_deja_vu : 0.0f;
        x_settings.scale_index = marbles->x_scale ? (int)(*marbles->x_scale) : 0;
        x_settings.length = marbles->x_length ? (int)(*marbles->x_length) : 1;
        x_settings.ratio.p = 1;
        x_settings.ratio.q = 1;
        
        marbles::GroupSettings y_settings;
        y_settings.control_mode = (marbles::ControlMode)(marbles->y_mode ? 
            (int)(*marbles->y_mode) : 0);
        y_settings.voltage_range = (marbles::VoltageRange)(marbles->y_range ? 
            (int)(*marbles->y_range) : 2);
        y_settings.register_mode = false;
        y_settings.register_value = 0.0f;
        y_settings.spread = marbles->y_spread ? *marbles->y_spread : 0.0f;
        y_settings.bias = marbles->y_bias ? *marbles->y_bias : 0.5f;
        y_settings.steps = marbles->y_steps ? *marbles->y_steps : 0.0f;
        y_settings.deja_vu = marbles->y_deja_vu ? *marbles->y_deja_vu : 0.0f;
        y_settings.scale_index = marbles->y_scale ? (int)(*marbles->y_scale) : 0;
        y_settings.length = marbles->y_length ? (int)(*marbles->y_length) : 1;
        y_settings.ratio.p = 1;
        y_settings.ratio.q = 1;
        
        marbles::ClockSource x_clock = (marbles::ClockSource)(marbles->x_clock_source ? 
            (int)(*marbles->x_clock_source) : 0);
        
        // Process X/Y generator
        marbles->xy_generator->Process(
            x_clock,
            x_settings,
            y_settings,
            marbles->gate_flags,
            marbles->ramps,
            marbles->xy_output,
            block_size
        );
        
        // Generate MIDI events from T gates and X/Y values
        for (size_t i = 0; i < block_size; i++) {
            uint32_t frame_time = offset + i;
            
            // T1 gate -> MIDI Note On/Off (channel 1, note 60)
            bool t1_gate = marbles->t_gates[i];
            if (t1_gate && !marbles->last_t_gates[0]) {
                uint8_t midi_note_on[3] = {0x90, 60, 100};  // Note On
                lv2_atom_forge_frame_time(&marbles->forge, frame_time);
                lv2_atom_forge_atom(&marbles->forge, 3, marbles->midi_event_uri);
                lv2_atom_forge_write(&marbles->forge, midi_note_on, 3);
            } else if (!t1_gate && marbles->last_t_gates[0]) {
                uint8_t midi_note_off[3] = {0x80, 60, 0};  // Note Off
                lv2_atom_forge_frame_time(&marbles->forge, frame_time);
                lv2_atom_forge_atom(&marbles->forge, 3, marbles->midi_event_uri);
                lv2_atom_forge_write(&marbles->forge, midi_note_off, 3);
            }
            marbles->last_t_gates[0] = t1_gate;
            
            // T2 gate -> MIDI Note On/Off (channel 1, note 62)
            bool t2_gate = marbles->t_gates[block_size + i];
            if (t2_gate && !marbles->last_t_gates[1]) {
                uint8_t midi_note_on[3] = {0x90, 62, 100};
                lv2_atom_forge_frame_time(&marbles->forge, frame_time);
                lv2_atom_forge_atom(&marbles->forge, 3, marbles->midi_event_uri);
                lv2_atom_forge_write(&marbles->forge, midi_note_on, 3);
            } else if (!t2_gate && marbles->last_t_gates[1]) {
                uint8_t midi_note_off[3] = {0x80, 62, 0};
                lv2_atom_forge_frame_time(&marbles->forge, frame_time);
                lv2_atom_forge_atom(&marbles->forge, 3, marbles->midi_event_uri);
                lv2_atom_forge_write(&marbles->forge, midi_note_off, 3);
            }
            marbles->last_t_gates[1] = t2_gate;
            
            // X/Y outputs -> MIDI CC messages (send every 8 blocks to avoid flooding)
            // Update at ~100 Hz at 48kHz sample rate
            if (i == 0 && (marbles->cc_update_counter % 8) == 0) {
                // Helper lambda to clamp and convert CV to MIDI CC
                auto cv_to_cc = [](float cv) -> uint8_t {
                    float normalized = (cv + 5.0f) / 10.0f;  // -5V to +5V -> 0 to 1
                    normalized = fmaxf(0.0f, fminf(1.0f, normalized));  // Clamp to [0, 1]
                    return (uint8_t)(normalized * 127.0f);
                };
                
                // X1 -> CC 1 (Mod Wheel), X2 -> CC 2, X3 -> CC 3, Y -> CC 4
                for (int ch = 0; ch < 4; ch++) {
                    float cv = marbles->xy_output[ch];
                    uint8_t cc_value = cv_to_cc(cv);
                    
                    // Only send if value changed significantly (hysteresis of 2)
                    if (abs((int)cc_value - (int)marbles->last_cc_values[ch]) >= 2) {
                        uint8_t midi_cc[3] = {0xB0, (uint8_t)(1 + ch), cc_value};
                        lv2_atom_forge_frame_time(&marbles->forge, frame_time);
                        lv2_atom_forge_atom(&marbles->forge, 3, marbles->midi_event_uri);
                        lv2_atom_forge_write(&marbles->forge, midi_cc, 3);
                        marbles->last_cc_values[ch] = cc_value;
                    }
                }
            }
            marbles->cc_update_counter++;
        }
        
        offset += block_size;
    }
    
    lv2_atom_forge_pop(&marbles->forge, &frame);
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
    Marbles* marbles = (Marbles*)instance;
    delete marbles->t_generator;
    delete marbles->xy_generator;
    delete marbles->ramp_extractor;
    free(instance);
}

static const void*
extension_data(const char* uri)
{
    return NULL;
}

static const LV2_Descriptor descriptor = {
    MI_MARBLES_URI,
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
    return index == 0 ? &descriptor : NULL;
}
