/*
 * Mutated Instruments LV2 Plugin
 * Dual Oscillator (Braids + Plaits) with Modulation Matrix and Filters
 * 
 * MIT License
 * 
 * Copyright (c) 2025 zynMI Project
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * ============================================================================
 * 
 * This plugin combines multiple Mutable Instruments modules:
 * 
 * - Braids macro oscillator
 *   Copyright 2012 Émilie Gillet (emilie.o.gillet@gmail.com)
 * 
 * - Plaits macro oscillator
 *   Copyright 2016 Émilie Gillet (emilie.o.gillet@gmail.com)
 * 
 * All Mutable Instruments DSP code is licensed under the MIT License.
 * We gratefully acknowledge Émilie Gillet and Mutable Instruments for
 * their incredible contributions to open-source synthesizer design.
 * 
 * The modulation matrix, filter implementations, and LFO system are
 * original work by the zynMI Project, also released under the MIT License.
 * 
 * Filter implementations include classic designs:
 * - Moog Ladder Filter
 * - MS-20 Filter
 * - TB-303 Filter
 * - SEM Filter
 * - Sallen-Key Filter
 * - Diode Ladder Filter
 * - Oberheim Filter
 * 
 * For more information about Mutable Instruments:
 * https://mutable-instruments.net/
 */

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Simple ADSR Envelope
class SimpleADSR {
public:
    enum Stage {
        STAGE_OFF,
        STAGE_ATTACK,
        STAGE_DECAY,
        STAGE_SUSTAIN,
        STAGE_RELEASE
    };

    SimpleADSR() : stage_(STAGE_OFF), value_(0.0f), sample_rate_(48000.0f),
                   attack_time_(0.01f), decay_time_(0.1f), sustain_level_(0.7f), release_time_(0.3f) {}

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        stage_ = STAGE_OFF;
        value_ = 0.0f;
    }

    void SetParameters(float attack, float decay, float sustain, float release) {
        attack_time_ = attack;
        decay_time_ = decay;
        sustain_level_ = sustain;
        release_time_ = release;
    }

    void Trigger() {
        stage_ = STAGE_ATTACK;
    }

    void Release() {
        if (stage_ != STAGE_OFF) {
            stage_ = STAGE_RELEASE;
        }
    }

    float Process() {
        switch (stage_) {
            case STAGE_OFF:
                value_ = 0.0f;
                break;

            case STAGE_ATTACK:
                if (attack_time_ < 0.001f) {
                    value_ = 1.0f;
                    stage_ = STAGE_DECAY;
                } else {
                    float attack_delta = 1.0f / (attack_time_ * sample_rate_);
                    value_ += attack_delta;
                    if (value_ >= 1.0f) {
                        value_ = 1.0f;
                        stage_ = STAGE_DECAY;
                    }
                }
                break;

            case STAGE_DECAY:
                if (decay_time_ < 0.001f) {
                    value_ = sustain_level_;
                    stage_ = STAGE_SUSTAIN;
                } else {
                    float decay_delta = (1.0f - sustain_level_) / (decay_time_ * sample_rate_);
                    value_ -= decay_delta;
                    if (value_ <= sustain_level_) {
                        value_ = sustain_level_;
                        stage_ = STAGE_SUSTAIN;
                    }
                }
                break;

            case STAGE_SUSTAIN:
                value_ = sustain_level_;
                break;

            case STAGE_RELEASE:
                if (release_time_ < 0.001f) {
                    value_ = 0.0f;
                    stage_ = STAGE_OFF;
                } else {
                    float release_delta = value_ / (release_time_ * sample_rate_);
                    value_ -= release_delta;
                    if (value_ <= 0.0001f) {
                        value_ = 0.0f;
                        stage_ = STAGE_OFF;
                    }
                }
                break;
        }

        // Hard gate at threshold
        if (value_ < 0.0001f) {
            value_ = 0.0f;
        }

        return value_;
    }

    bool IsActive() const {
        return stage_ != STAGE_OFF;
    }

    Stage GetStage() const {
        return stage_;
    }

    float GetValue() const {
        return value_;
    }

private:
    Stage stage_;
    float value_;
    float sample_rate_;
    float attack_time_;
    float decay_time_;
    float sustain_level_;
    float release_time_;
};

// Moog-style ladder filter (4-pole lowpass)
// Improved version with better stability and thermal modeling
class MoogFilter {
public:
    MoogFilter() : sample_rate_(48000.0f) {
        Reset();
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        Reset();
    }

    void Reset() {
        for (int i = 0; i < 4; i++) {
            stage_[i] = 0.0f;
            stage_tanh_[i] = 0.0f;
        }
    }

    // Fast tanh approximation for thermal modeling
    inline float FastTanh(float x) {
        float x2 = x * x;
        float x3 = x2 * x;
        float x5 = x3 * x2;
        return x - (x3 / 3.0f) + (x5 / 5.0f);
    }

    // Denormal protection
    inline float Undenormalize(float x) {
        if (fabs(x) < 1e-10f) return 0.0f;
        return x;
    }

    float Process(float input, float cutoff, float resonance) {
        // Cutoff: 0-1 (maps to ~20Hz - 20kHz)
        // Resonance: 0-1 (0 = no resonance, 1 = self-oscillation)
        
        // Improved cutoff mapping with better low-frequency response
        float fc = cutoff * cutoff * 0.5f;
        fc = fmin(fc, 0.499f);  // Prevent instability
        
        // Resonance with better scaling - reduced maximum to prevent pure oscillation
        float res = resonance * resonance * 3.5f;  // Squared for more control, max 3.5
        
        // Thermal compensation (Moog ladder thermal feedback)
        float thermal_feedback = stage_tanh_[3];
        
        // Limit feedback to prevent runaway oscillation
        thermal_feedback = fmax(-0.95f, fmin(0.95f, thermal_feedback));
        
        input = FastTanh(input - res * thermal_feedback);
        
        // 4 cascaded one-pole filters with thermal modeling
        for (int i = 0; i < 4; i++) {
            // One-pole lowpass with oversampling compensation
            stage_[i] = stage_[i] + fc * (((i == 0) ? input : stage_tanh_[i-1]) - stage_[i]);
            stage_[i] = Undenormalize(stage_[i]);
            stage_tanh_[i] = FastTanh(stage_[i]);
        }
        
        return stage_[3];
    }

private:
    float sample_rate_;
    float stage_[4];
    float stage_tanh_[4];
};

// MS-20 style filter (2-pole with overdrive)
// Improved version with better stability
class MS20Filter {
public:
    MS20Filter() : sample_rate_(48000.0f) {
        Reset();
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        Reset();
    }

    void Reset() {
        lp_ = bp_ = 0.0f;
        delay_ = 0.0f;
    }

    // Denormal protection
    inline float Undenormalize(float x) {
        if (fabs(x) < 1e-10f) return 0.0f;
        return x;
    }

    float Process(float input, float cutoff, float resonance) {
        // MS-20 has aggressive character
        cutoff = fmin(cutoff * cutoff * 0.5f, 0.499f);
        float res = resonance * resonance * 1.5f;  // Squared for better control, reduced max
        
        // Soft clipping on input for MS-20 character
        input = tanh(input * 1.2f) * 0.9f;
        
        // Improved state variable filter with better stability
        float q = fmax(0.5f, 1.0f - cutoff * 0.7f);
        float f = cutoff * 1.5f;
        
        // Resonance feedback with limiting
        float fb = Undenormalize(delay_) * res;
        fb = fmax(-1.5f, fmin(1.5f, fb));  // Tighter clamp to prevent oscillation
        
        // State variable core
        lp_ = Undenormalize(lp_ + f * bp_);
        float hp = input - lp_ - q * bp_ - fb;
        bp_ = Undenormalize(bp_ + f * hp);
        
        // Store delayed feedback sample with soft limiting
        delay_ = tanh(bp_ * 0.9f);
        
        // Output with slight saturation
        float output = lp_ + bp_ * 0.3f;
        return tanh(output * 0.8f);
    }

private:
    float sample_rate_;
    float lp_, bp_;
    float delay_;
};

// TB-303 style filter (resonant lowpass with characteristic sound)
// Improved version with diode ladder emulation
class TB303Filter {
public:
    TB303Filter() : sample_rate_(48000.0f) {
        Reset();
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        Reset();
    }

    void Reset() {
        lp_ = bp_ = hp_ = 0.0f;
    }

    // Denormal protection
    inline float Undenormalize(float x) {
        if (fabs(x) < 1e-10f) return 0.0f;
        return x;
    }

    float Process(float input, float cutoff, float resonance) {
        // 303 filter is less steep but very resonant
        cutoff = fmin(cutoff * cutoff * 0.55f, 0.499f);
        float res = resonance * resonance * 1.8f;  // Squared for better control, reduced max
        
        // 303-style input gain and saturation
        input = tanh(input * 1.5f) * 0.8f;
        
        // Improved state variable filter with better Q control
        float q = fmax(0.3f, 1.0f - cutoff * 0.8f);
        float f = cutoff * 1.8f;
        
        // Add resonance feedback with soft limiting
        float fb = Undenormalize(bp_) * res;
        fb = tanh(fb * 0.4f) * 1.5f;  // Tighter soft limit to prevent oscillation
        
        // State variable filter equations
        hp_ = input - lp_ - q * bp_ - fb;
        bp_ = Undenormalize(bp_ + f * hp_);
        lp_ = Undenormalize(lp_ + f * bp_);
        
        // 303 characteristic: mix lowpass with some bandpass
        float output = lp_ + bp_ * 0.4f;
        
        // Final 303-style saturation
        return tanh(output * 0.9f);
    }

private:
    float sample_rate_;
    float lp_, bp_, hp_;
};

// SEM-style state variable filter (multimode with LP/BP/HP)
class SEMFilter {
public:
    SEMFilter() : sample_rate_(48000.0f) {
        Reset();
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        Reset();
    }

    void Reset() {
        lp_ = bp_ = 0.0f;
        z1_ = z2_ = 0.0f;
    }

    inline float Undenormalize(float x) {
        if (fabs(x) < 1e-10f) return 0.0f;
        return x;
    }

    float Process(float input, float cutoff, float resonance) {
        // SEM-style multimode filter
        cutoff = fmin(cutoff * cutoff * 0.48f, 0.499f);
        float res = resonance * resonance * 2.0f;  // Squared, reduced max
        
        // Pre-gain for character
        input = tanh(input * 1.1f) * 0.95f;
        
        // State variable with proper damping
        float f = cutoff * 2.0f;
        float damp = fmin(2.0f * (1.0f - pow(resonance, 0.25f)), fmin(2.0f, 2.0f / f - f * 0.5f));
        
        // Filter core with resonance feedback
        float notch = input - damp * bp_;
        lp_ = Undenormalize(lp_ + f * bp_);
        float hp = notch - lp_;
        bp_ = Undenormalize(f * hp + bp_);
        
        // Add resonance with tighter control
        bp_ += res * bp_ * 0.08f;  // Reduced from 0.1
        bp_ = tanh(bp_ * 0.9f);  // Tighter soft limit
        
        // Multimode mix: mostly LP with some BP for character
        return lp_ * 0.8f + bp_ * 0.2f;
    }

private:
    float sample_rate_;
    float lp_, bp_;
    float z1_, z2_;
};

// Sallen-Key style filter (2-pole Butterworth topology)
class SallenKeyFilter {
public:
    SallenKeyFilter() : sample_rate_(48000.0f) {
        Reset();
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        Reset();
    }

    void Reset() {
        z1_ = z2_ = 0.0f;
        y1_ = y2_ = 0.0f;
    }

    inline float Undenormalize(float x) {
        if (fabs(x) < 1e-10f) return 0.0f;
        return x;
    }

    float Process(float input, float cutoff, float resonance) {
        // Sallen-Key topology (smooth 2-pole response)
        cutoff = fmin(cutoff * cutoff * 0.45f, 0.499f);
        
        // Calculate filter coefficients with limited Q to prevent oscillation
        float wd = cutoff * M_PI;
        float wa = tan(wd);
        float g = wa * wa;
        float Q = 0.5f + resonance * resonance * 3.0f;  // Squared resonance, max Q = 3.5
        Q = fmin(Q, 3.5f);  // Hard limit Q factor
        float r = 1.0f / Q;
        
        float a0 = 1.0f + r * wa + g;
        float a1 = 2.0f * (g - 1.0f);
        float a2 = 1.0f - r * wa + g;
        float b0 = g / a0;
        float b1 = 2.0f * b0;
        float b2 = b0;
        
        // Normalize coefficients
        a1 /= a0;
        a2 /= a0;
        
        // Biquad filter implementation
        float output = b0 * input + b1 * z1_ + b2 * z2_ - a1 * y1_ - a2 * y2_;
        output = Undenormalize(output);
        
        // Clamp output to prevent instability
        output = fmax(-2.0f, fmin(2.0f, output));
        
        // Update state
        z2_ = z1_;
        z1_ = input;
        y2_ = y1_;
        y1_ = output;
        
        // Soft limiting for stability
        return tanh(output * 0.8f);
    }

private:
    float sample_rate_;
    float z1_, z2_;  // Input delays
    float y1_, y2_;  // Output delays
};

// Diode ladder filter (inspired by EMS VCS3 / Steiner-Parker)
class DiodeLadderFilter {
public:
    DiodeLadderFilter() : sample_rate_(48000.0f) {
        Reset();
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        Reset();
    }

    void Reset() {
        for (int i = 0; i < 4; i++) {
            stage_[i] = 0.0f;
        }
        feedback_ = 0.0f;
    }

    inline float Undenormalize(float x) {
        if (fabs(x) < 1e-10f) return 0.0f;
        return x;
    }

    // Diode pair simulation
    inline float DiodePair(float x) {
        // Simplified diode characteristics
        if (x > 0.0f) {
            return tanh(x * 2.0f) * 0.5f;
        } else {
            return tanh(x * 2.0f) * 0.5f;
        }
    }

    float Process(float input, float cutoff, float resonance) {
        // Diode ladder has unique saturation characteristics
        cutoff = fmin(cutoff * cutoff * 0.52f, 0.499f);
        float res = resonance * resonance * 3.2f;  // Squared, reduced max
        
        // Input stage with diode-like saturation
        input = DiodePair(input);
        
        // Calculate feedback with tighter limiting
        float fb = feedback_ * res;
        fb = tanh(fb * 0.45f);  // Tighter soft limit to prevent oscillation
        
        // Apply feedback
        input -= fb;
        
        // 4-stage cascade with diode-like nonlinearity per stage
        float fc = cutoff * 1.3f;
        for (int i = 0; i < 4; i++) {
            float input_stage = (i == 0) ? input : stage_[i-1];
            stage_[i] = Undenormalize(stage_[i] + fc * (input_stage - stage_[i]));
            // Apply diode-like saturation per stage with reduced gain
            stage_[i] = DiodePair(stage_[i] * 1.0f);  // Reduced from 1.2
        }
        
        // Store feedback with limiting
        feedback_ = tanh(stage_[3] * 0.9f);
        
        return stage_[3];
    }

private:
    float sample_rate_;
    float stage_[4];
    float feedback_;
};

// Oberheim SEM-style 12dB multimode filter
class OberheimFilter {
public:
    OberheimFilter() : sample_rate_(48000.0f) {
        Reset();
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        Reset();
    }

    void Reset() {
        lp1_ = lp2_ = 0.0f;
        bp1_ = bp2_ = 0.0f;
        hp1_ = hp2_ = 0.0f;
    }

    inline float Undenormalize(float x) {
        if (fabs(x) < 1e-10f) return 0.0f;
        return x;
    }

    float Process(float input, float cutoff, float resonance) {
        // Oberheim multimode character
        cutoff = fmin(cutoff * cutoff * 0.46f, 0.499f);
        float res = resonance * resonance * 2.2f;  // Squared, reduced max
        
        // Input conditioning
        input = tanh(input * 1.15f) * 0.9f;
        
        // Two cascaded state variable sections
        float f = cutoff * 1.4f;
        float q = 1.0f - cutoff * 0.7f;
        q = fmax(0.4f, q);
        
        // First stage
        lp1_ = Undenormalize(lp1_ + f * bp1_);
        hp1_ = input - lp1_ - q * bp1_;
        bp1_ = Undenormalize(bp1_ + f * hp1_);
        
        // Second stage with resonance and tighter limiting
        float fb = lp2_ * res;
        fb = tanh(fb * 0.4f);  // Tighter soft limit
        
        lp2_ = Undenormalize(lp2_ + f * bp2_);
        hp2_ = bp1_ - lp2_ - q * bp2_ - fb;
        bp2_ = Undenormalize(bp2_ + f * hp2_);
        
        // Soft limit internal states
        bp2_ = tanh(bp2_ * 0.95f);
        
        // Output mix (LP heavy with BP character)
        return lp2_ * 0.85f + bp2_ * 0.15f;
    }

private:
    float sample_rate_;
    float lp1_, lp2_;
    float bp1_, bp2_;
    float hp1_, hp2_;
};

// LFO with multiple waveforms
class LFO {
public:
    LFO() : sample_rate_(48000.0f), phase_(0.0f) {}

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        phase_ = 0.0f;
    }

    void Reset() {
        phase_ = 0.0f;
    }

    float Process(float rate, float shape, int waveform) {
        // Rate: 0-1 (0.01Hz - 20Hz)
        float freq = 0.01f + rate * 19.99f;
        float phase_increment = freq / sample_rate_;
        
        phase_ += phase_increment;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
        }

        float output = 0.0f;
        
        switch (waveform) {
            case 0: // Sine
                output = sin(phase_ * 2.0f * M_PI);
                break;
                
            case 1: // Triangle
                if (phase_ < 0.5f) {
                    output = -1.0f + 4.0f * phase_;
                } else {
                    output = 3.0f - 4.0f * phase_;
                }
                break;
                
            case 2: // Sawtooth
                output = -1.0f + 2.0f * phase_;
                break;
                
            case 3: // Reverse Sawtooth
                output = 1.0f - 2.0f * phase_;
                break;
                
            case 4: { // Square/PWM
                // Shape controls pulse width (0.1 to 0.9)
                float pw = 0.1f + shape * 0.8f;
                output = (phase_ < pw) ? 1.0f : -1.0f;
                break;
            }
                
            case 5: { // Sample & Hold (random)
                static float sh_value = 0.0f;
                if (phase_ < phase_increment) {
                    // New random value at phase wrap
                    sh_value = -1.0f + 2.0f * (rand() / (float)RAND_MAX);
                }
                output = sh_value;
                break;
            }
        }
        
        return output;
    }

    float GetPhase() const {
        return phase_;
    }

private:
    float sample_rate_;
    float phase_;
};

#define TEST
#include "eurorack/braids/macro_oscillator.h"
#include "eurorack/plaits/dsp/voice.h"
#include "eurorack/stmlib/utils/buffer_allocator.h"

#define MUTATED_URI "https://github.com/PatttF/zynMI/plugins/mutated"

// Helper function for clipping
template<typename T>
inline T Clip(T value, T min, T max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

typedef enum {
    MUTATED_MIDI_IN = 0,
    // Braids
    BRAIDS_LEVEL,
    BRAIDS_SHAPE,
    BRAIDS_COARSE,
    BRAIDS_FINE,
    BRAIDS_FM,
    BRAIDS_TIMBRE,
    BRAIDS_COLOR,
    BRAIDS_ATTACK,
    BRAIDS_DECAY,
    BRAIDS_SUSTAIN,
    BRAIDS_RELEASE,
    // Plaits
    PLAITS_LEVEL,
    PLAITS_ENGINE,
    PLAITS_COARSE,
    PLAITS_FINE,
    PLAITS_HARMONICS,
    PLAITS_TIMBRE,
    PLAITS_MORPH,
    PLAITS_LPG_DECAY,
    PLAITS_LPG_COLOUR,
    PLAITS_ATTACK,
    PLAITS_DECAY,
    PLAITS_SUSTAIN,
    PLAITS_RELEASE,
    // Modulation 1
    MOD1_SOURCE,
    MOD1_TARGET,
    MOD1_AMOUNT,
    MOD1_DETUNE,
    // Modulation 2
    MOD2_SOURCE,
    MOD2_TARGET,
    MOD2_AMOUNT,
    MOD2_DETUNE,
    // Modulation 3
    MOD3_SOURCE,
    MOD3_TARGET,
    MOD3_AMOUNT,
    MOD3_DETUNE,
    // Filter 1
    FILTER_TYPE,
    FILTER_ROUTING,
    FILTER_CUTOFF,
    FILTER_RESONANCE,
    // Filter 2
    FILTER2_TYPE,
    FILTER2_ROUTING,
    FILTER2_CUTOFF,
    FILTER2_RESONANCE,
    // Panning and Glide
    BRAIDS_PAN,
    BRAIDS_GLIDE,
    PLAITS_PAN,
    PLAITS_GLIDE,
    // Detune (Octave shift)
    BRAIDS_DETUNE,
    PLAITS_DETUNE,
    // Outputs
    MUTATED_OUT_L,
    MUTATED_OUT_R
} PortIndex;

// Modulation sources
enum ModSource {
    MOD_SRC_NONE = 0,
    MOD_SRC_BRAIDS_OUT,
    MOD_SRC_PLAITS_OUT,
    MOD_SRC_BRAIDS_ENV,
    MOD_SRC_PLAITS_ENV,
    MOD_SRC_VELOCITY,
    MOD_SRC_BRAIDS_TIMBRE,
    MOD_SRC_BRAIDS_COLOR,
    MOD_SRC_PLAITS_HARMONICS,
    MOD_SRC_PLAITS_TIMBRE,
    MOD_SRC_PLAITS_MORPH,
    MOD_SRC_LFO_SINE,
    MOD_SRC_LFO_SAW,
    MOD_SRC_LFO_PWM
};

// Modulation targets
enum ModTarget {
    MOD_TGT_NONE = 0,
    MOD_TGT_BRAIDS_TIMBRE,
    MOD_TGT_BRAIDS_COLOR,
    MOD_TGT_BRAIDS_FM,
    MOD_TGT_PLAITS_HARMONICS,
    MOD_TGT_PLAITS_TIMBRE,
    MOD_TGT_PLAITS_MORPH,
    MOD_TGT_PLAITS_LPG_DECAY,
    MOD_TGT_PLAITS_LPG_COLOUR,
    MOD_TGT_BRAIDS_PITCH,
    MOD_TGT_PLAITS_PITCH,
    MOD_TGT_BRAIDS_LEVEL,
    MOD_TGT_PLAITS_LEVEL,
    MOD_TGT_BRAIDS_OUT,
    MOD_TGT_PLAITS_OUT
};

// Filter types
enum FilterType {
    FILTER_OFF = 0,
    FILTER_MOOG,
    FILTER_MS20,
    FILTER_TB303,
    FILTER_SEM,
    FILTER_SALLENKEY,
    FILTER_DIODE,
    FILTER_OBERHEIM
};

// Filter routing
enum FilterRouting {
    ROUTE_BRAIDS = 0,
    ROUTE_PLAITS,
    ROUTE_BOTH
};

typedef struct {
    // Port buffers
    const LV2_Atom_Sequence* midi_in;
    const float* braids_level;
    const float* braids_shape;
    const float* braids_coarse;
    const float* braids_fine;
    const float* braids_fm;
    const float* braids_timbre;
    const float* braids_color;
    const float* braids_attack;
    const float* braids_decay;
    const float* braids_sustain;
    const float* braids_release;
    const float* plaits_level;
    const float* plaits_engine;
    const float* plaits_coarse;
    const float* plaits_fine;
    const float* plaits_harmonics;
    const float* plaits_timbre;
    const float* plaits_morph;
    const float* plaits_lpg_decay;
    const float* plaits_lpg_colour;
    const float* plaits_attack;
    const float* plaits_decay;
    const float* plaits_sustain;
    const float* plaits_release;
    const float* mod1_source;
    const float* mod1_target;
    const float* mod1_amount;
    const float* mod1_detune;
    const float* mod2_source;
    const float* mod2_target;
    const float* mod2_amount;
    const float* mod2_detune;
    const float* mod3_source;
    const float* mod3_target;
    const float* mod3_amount;
    const float* mod3_detune;
    const float* filter_type;
    const float* filter_routing;
    const float* filter_cutoff;
    const float* filter_resonance;
    const float* filter2_type;
    const float* filter2_routing;
    const float* filter2_cutoff;
    const float* filter2_resonance;
    const float* braids_pan;
    const float* braids_glide;
    const float* plaits_pan;
    const float* plaits_glide;
    const float* braids_detune;
    const float* plaits_detune;
    float* out_l;
    float* out_r;
    
    // MIDI
    LV2_URID_Map* map;
    LV2_URID midi_event_uri;
    uint8_t current_note;
    uint8_t velocity;
    bool note_on;
    
    // Braids oscillator
    braids::MacroOscillator osc;
    uint8_t sync_buffer[24];
    int16_t render_buffer[24];
    SimpleADSR braids_envelope;
    
    // Plaits oscillator
    plaits::Voice* plaits_voice;
    uint8_t plaits_shared_buffer[16384];
    plaits::Patch plaits_patch;
    plaits::Modulations plaits_modulations;
    SimpleADSR plaits_envelope;
    
    // Modulation values (computed per-sample)
    float braids_raw_output;
    float plaits_raw_output;
    
    // Filters (Filter 1)
    MoogFilter moog_filter;
    MS20Filter ms20_filter;
    TB303Filter tb303_filter;
    SEMFilter sem_filter;
    SallenKeyFilter sallenkey_filter;
    DiodeLadderFilter diode_filter;
    OberheimFilter oberheim_filter;
    
    // Filters (Filter 2)
    MoogFilter moog_filter2;
    MS20Filter ms20_filter2;
    TB303Filter tb303_filter2;
    SEMFilter sem_filter2;
    SallenKeyFilter sallenkey_filter2;
    DiodeLadderFilter diode_filter2;
    OberheimFilter oberheim_filter2;
    
    // LFOs
    LFO lfo_sine;
    LFO lfo_saw;
    LFO lfo_pwm;
    float lfo_sine_value;
    float lfo_saw_value;
    float lfo_pwm_value;
    float lfo_sine_rate;
    float lfo_saw_rate;
    float lfo_pwm_rate;
    
    // Glide (portamento)
    float braids_current_note;
    float plaits_current_note;
    
    // Track previous shape/engine to avoid unnecessary Strike() calls
    int previous_braids_shape;
    int previous_plaits_engine;
    
    double sample_rate;
} Mutated;

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double rate,
            const char* bundle_path,
            const LV2_Feature* const* features)
{
    Mutated* mutated = (Mutated*)calloc(1, sizeof(Mutated));
    if (!mutated) {
        return NULL;
    }
    
    mutated->map = NULL;
    if (features) {
        for (int i = 0; features[i]; i++) {
            if (!strcmp(features[i]->URI, LV2_URID__map)) {
                mutated->map = (LV2_URID_Map*)features[i]->data;
                break;
            }
        }
    }
    
    mutated->midi_event_uri = mutated->map ? 
        mutated->map->map(mutated->map->handle, LV2_MIDI__MidiEvent) : 0;
    
    mutated->sample_rate = rate;
    mutated->current_note = 60;
    mutated->velocity = 100;
    mutated->note_on = false;
    
    // Initialize Braids
    mutated->osc.Init();
    mutated->braids_envelope.Init(rate);
    
    // Initialize Plaits
    mutated->plaits_voice = new plaits::Voice;
    if (mutated->plaits_voice) {
        memset(mutated->plaits_shared_buffer, 0, sizeof(mutated->plaits_shared_buffer));
        stmlib::BufferAllocator allocator;
        allocator.Init(mutated->plaits_shared_buffer, sizeof(mutated->plaits_shared_buffer));
        mutated->plaits_voice->Init(&allocator);
    }
    memset(&mutated->plaits_patch, 0, sizeof(mutated->plaits_patch));
    memset(&mutated->plaits_modulations, 0, sizeof(mutated->plaits_modulations));
    mutated->plaits_patch.engine = 0;
    mutated->plaits_patch.lpg_colour = 0.5f;
    mutated->plaits_patch.decay = 0.5f;
    mutated->plaits_patch.note = 48.0f;
    mutated->plaits_patch.harmonics = 0.5f;
    mutated->plaits_patch.timbre = 0.5f;
    mutated->plaits_patch.morph = 0.5f;
    mutated->plaits_envelope.Init(rate);
    
    // Initialize filters (Filter 1)
    mutated->moog_filter.Init(rate);
    mutated->ms20_filter.Init(rate);
    mutated->tb303_filter.Init(rate);
    mutated->sem_filter.Init(rate);
    mutated->sallenkey_filter.Init(rate);
    mutated->diode_filter.Init(rate);
    mutated->oberheim_filter.Init(rate);
    
    // Initialize filters (Filter 2)
    mutated->moog_filter2.Init(rate);
    mutated->ms20_filter2.Init(rate);
    mutated->tb303_filter2.Init(rate);
    mutated->sem_filter2.Init(rate);
    mutated->sallenkey_filter2.Init(rate);
    mutated->diode_filter2.Init(rate);
    mutated->oberheim_filter2.Init(rate);
    
    // Initialize LFOs
    mutated->lfo_sine.Init(rate);
    mutated->lfo_saw.Init(rate);
    mutated->lfo_pwm.Init(rate);
    mutated->lfo_sine_value = 0.0f;
    mutated->lfo_saw_value = 0.0f;
    
    // Initialize previous shape/engine tracking
    mutated->previous_braids_shape = -1;
    mutated->previous_plaits_engine = -1;
    mutated->lfo_pwm_value = 0.0f;
    mutated->lfo_sine_rate = 0.2f;
    mutated->lfo_saw_rate = 0.2f;
    mutated->lfo_pwm_rate = 0.2f;
    
    // Initialize glide
    mutated->braids_current_note = 60.0f;
    mutated->plaits_current_note = 60.0f;
    
    return (LV2_Handle)mutated;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    Mutated* mutated = (Mutated*)instance;
    
    switch ((PortIndex)port) {
        case MUTATED_MIDI_IN:
            mutated->midi_in = (const LV2_Atom_Sequence*)data;
            break;
        case BRAIDS_LEVEL:
            mutated->braids_level = (const float*)data;
            break;
        case BRAIDS_SHAPE:
            mutated->braids_shape = (const float*)data;
            break;
        case BRAIDS_COARSE:
            mutated->braids_coarse = (const float*)data;
            break;
        case BRAIDS_FINE:
            mutated->braids_fine = (const float*)data;
            break;
        case BRAIDS_FM:
            mutated->braids_fm = (const float*)data;
            break;
        case BRAIDS_TIMBRE:
            mutated->braids_timbre = (const float*)data;
            break;
        case BRAIDS_COLOR:
            mutated->braids_color = (const float*)data;
            break;
        case BRAIDS_ATTACK:
            mutated->braids_attack = (const float*)data;
            break;
        case BRAIDS_DECAY:
            mutated->braids_decay = (const float*)data;
            break;
        case BRAIDS_SUSTAIN:
            mutated->braids_sustain = (const float*)data;
            break;
        case BRAIDS_RELEASE:
            mutated->braids_release = (const float*)data;
            break;
        case PLAITS_LEVEL:
            mutated->plaits_level = (const float*)data;
            break;
        case PLAITS_ENGINE:
            mutated->plaits_engine = (const float*)data;
            break;
        case PLAITS_COARSE:
            mutated->plaits_coarse = (const float*)data;
            break;
        case PLAITS_FINE:
            mutated->plaits_fine = (const float*)data;
            break;
        case PLAITS_HARMONICS:
            mutated->plaits_harmonics = (const float*)data;
            break;
        case PLAITS_TIMBRE:
            mutated->plaits_timbre = (const float*)data;
            break;
        case PLAITS_MORPH:
            mutated->plaits_morph = (const float*)data;
            break;
        case PLAITS_LPG_DECAY:
            mutated->plaits_lpg_decay = (const float*)data;
            break;
        case PLAITS_LPG_COLOUR:
            mutated->plaits_lpg_colour = (const float*)data;
            break;
        case PLAITS_ATTACK:
            mutated->plaits_attack = (const float*)data;
            break;
        case PLAITS_DECAY:
            mutated->plaits_decay = (const float*)data;
            break;
        case PLAITS_SUSTAIN:
            mutated->plaits_sustain = (const float*)data;
            break;
        case PLAITS_RELEASE:
            mutated->plaits_release = (const float*)data;
            break;
        case MOD1_SOURCE:
            mutated->mod1_source = (const float*)data;
            break;
        case MOD1_TARGET:
            mutated->mod1_target = (const float*)data;
            break;
        case MOD1_AMOUNT:
            mutated->mod1_amount = (const float*)data;
            break;
        case MOD1_DETUNE:
            mutated->mod1_detune = (const float*)data;
            break;
        case MOD2_SOURCE:
            mutated->mod2_source = (const float*)data;
            break;
        case MOD2_TARGET:
            mutated->mod2_target = (const float*)data;
            break;
        case MOD2_AMOUNT:
            mutated->mod2_amount = (const float*)data;
            break;
        case MOD2_DETUNE:
            mutated->mod2_detune = (const float*)data;
            break;
        case MOD3_SOURCE:
            mutated->mod3_source = (const float*)data;
            break;
        case MOD3_TARGET:
            mutated->mod3_target = (const float*)data;
            break;
        case MOD3_AMOUNT:
            mutated->mod3_amount = (const float*)data;
            break;
        case MOD3_DETUNE:
            mutated->mod3_detune = (const float*)data;
            break;
        case FILTER_TYPE:
            mutated->filter_type = (const float*)data;
            break;
        case FILTER_ROUTING:
            mutated->filter_routing = (const float*)data;
            break;
        case FILTER_CUTOFF:
            mutated->filter_cutoff = (const float*)data;
            break;
        case FILTER_RESONANCE:
            mutated->filter_resonance = (const float*)data;
            break;
        case FILTER2_TYPE:
            mutated->filter2_type = (const float*)data;
            break;
        case FILTER2_ROUTING:
            mutated->filter2_routing = (const float*)data;
            break;
        case FILTER2_CUTOFF:
            mutated->filter2_cutoff = (const float*)data;
            break;
        case FILTER2_RESONANCE:
            mutated->filter2_resonance = (const float*)data;
            break;
        case BRAIDS_PAN:
            mutated->braids_pan = (const float*)data;
            break;
        case BRAIDS_GLIDE:
            mutated->braids_glide = (const float*)data;
            break;
        case PLAITS_PAN:
            mutated->plaits_pan = (const float*)data;
            break;
        case PLAITS_GLIDE:
            mutated->plaits_glide = (const float*)data;
            break;
        case BRAIDS_DETUNE:
            mutated->braids_detune = (const float*)data;
            break;
        case PLAITS_DETUNE:
            mutated->plaits_detune = (const float*)data;
            break;
        case MUTATED_OUT_L:
            mutated->out_l = (float*)data;
            break;
        case MUTATED_OUT_R:
            mutated->out_r = (float*)data;
            break;
    }
}

static void
activate(LV2_Handle instance)
{
}

// Get modulation source value
static float get_mod_source(Mutated* mutated, int source) {
    switch (source) {
        case MOD_SRC_BRAIDS_OUT: return mutated->braids_raw_output;
        case MOD_SRC_PLAITS_OUT: return mutated->plaits_raw_output;
        case MOD_SRC_BRAIDS_ENV: return mutated->braids_envelope.GetValue();
        case MOD_SRC_PLAITS_ENV: return mutated->plaits_envelope.GetValue();
        case MOD_SRC_VELOCITY: return mutated->velocity / 127.0f;
        case MOD_SRC_BRAIDS_TIMBRE: return *mutated->braids_timbre;
        case MOD_SRC_BRAIDS_COLOR: return *mutated->braids_color;
        case MOD_SRC_PLAITS_HARMONICS: return *mutated->plaits_harmonics;
        case MOD_SRC_PLAITS_TIMBRE: return *mutated->plaits_timbre;
        case MOD_SRC_PLAITS_MORPH: return *mutated->plaits_morph;
        case MOD_SRC_LFO_SINE: return mutated->lfo_sine_value;
        case MOD_SRC_LFO_SAW: return mutated->lfo_saw_value;
        case MOD_SRC_LFO_PWM: return mutated->lfo_pwm_value;
        default: return 0.0f;
    }
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    Mutated* mutated = (Mutated*)instance;
    
    if (!mutated || !mutated->out_l || !mutated->out_r) {
        return;
    }
    
    // Process MIDI
    bool new_trigger = false;
    if (mutated->midi_event_uri && mutated->midi_in) {
        LV2_ATOM_SEQUENCE_FOREACH(mutated->midi_in, ev) {
            if (ev->body.type == mutated->midi_event_uri) {
                const uint8_t* const msg = (const uint8_t*)(ev + 1);
                
                switch (lv2_midi_message_type(msg)) {
                    case LV2_MIDI_MSG_NOTE_ON:
                        if (msg[2] > 0) {
                            mutated->current_note = msg[1];
                            mutated->velocity = msg[2];
                            mutated->note_on = true;
                            new_trigger = true;
                            // Trigger both envelopes
                            mutated->braids_envelope.Trigger();
                            mutated->plaits_envelope.Trigger();
                            // Trigger strike for Braids percussion engines (drum/kick modes)
                            mutated->osc.Strike();
                        } else {
                            if (msg[1] == mutated->current_note) {
                                mutated->note_on = false;
                                // Release both envelopes
                                mutated->braids_envelope.Release();
                                mutated->plaits_envelope.Release();
                            }
                        }
                        break;
                        
                    case LV2_MIDI_MSG_NOTE_OFF:
                        if (msg[1] == mutated->current_note) {
                            mutated->note_on = false;
                            // Release both envelopes
                            mutated->braids_envelope.Release();
                            mutated->plaits_envelope.Release();
                        }
                        break;
                }
            }
        }
    }
    
    // Calculate LFO rates based on active modulation slots using them
    // Base rate: 0.2 Hz (5 second cycle)
    // Detune/Speed parameter maps to rate multiplier: -1 to 1 -> 0.1x to 10x
    float sine_max_speed = 0.0f;
    float saw_max_speed = 0.0f;
    float pwm_max_speed = 0.0f;
    
    for (int mod_slot = 0; mod_slot < 3; mod_slot++) {
        const float* src_param = (mod_slot == 0) ? mutated->mod1_source : 
                                 (mod_slot == 1) ? mutated->mod2_source : mutated->mod3_source;
        const float* det_param = (mod_slot == 0) ? mutated->mod1_detune :
                                 (mod_slot == 1) ? mutated->mod2_detune : mutated->mod3_detune;
        
        int source = (int)(*src_param);
        float detune = *det_param;
        
        // Map detune (-1 to 1) to speed multiplier (0.1x to 10x)
        // 0 = 1x (base rate), negative = slower, positive = faster
        float speed_mult = powf(10.0f, detune);  // 10^detune gives 0.1 to 10 range
        
        if (source == MOD_SRC_LFO_SINE && speed_mult > sine_max_speed) {
            sine_max_speed = speed_mult;
        }
        if (source == MOD_SRC_LFO_SAW && speed_mult > saw_max_speed) {
            saw_max_speed = speed_mult;
        }
        if (source == MOD_SRC_LFO_PWM && speed_mult > pwm_max_speed) {
            pwm_max_speed = speed_mult;
        }
    }
    
    // If no modulation slot is using an LFO, use base rate
    if (sine_max_speed == 0.0f) sine_max_speed = 1.0f;
    if (saw_max_speed == 0.0f) saw_max_speed = 1.0f;
    if (pwm_max_speed == 0.0f) pwm_max_speed = 1.0f;
    
    // Update LFO rates
    mutated->lfo_sine_rate = 0.2f * sine_max_speed;
    mutated->lfo_saw_rate = 0.2f * saw_max_speed;
    mutated->lfo_pwm_rate = 0.2f * pwm_max_speed;
    
    // Update LFOs with calculated rates
    for (uint32_t i = 0; i < n_samples; i++) {
        mutated->lfo_sine_value = mutated->lfo_sine.Process(mutated->lfo_sine_rate, 0.5f, 0);   // Sine
        mutated->lfo_saw_value = mutated->lfo_saw.Process(mutated->lfo_saw_rate, 0.5f, 2);      // Sawtooth
        mutated->lfo_pwm_value = mutated->lfo_pwm.Process(mutated->lfo_pwm_rate, 0.5f, 4);      // Square/PWM
    }
    
    // Get parameters
    int braids_shape = (int)(*mutated->braids_shape);
    bool braids_enabled = (braids_shape >= 0);
    if (braids_shape < 0) braids_shape = 0;
    if (braids_shape > 47) braids_shape = 47;
    
    int plaits_engine = (int)(*mutated->plaits_engine);
    bool plaits_enabled = (plaits_engine >= 0);
    if (plaits_engine < 0) plaits_engine = 0;
    if (plaits_engine > 15) plaits_engine = 15;
    
    float braids_level = *mutated->braids_level;
    float plaits_level = *mutated->plaits_level;
    
    // Update envelope parameters
    mutated->braids_envelope.SetParameters(*mutated->braids_attack, *mutated->braids_decay,
                                           *mutated->braids_sustain, *mutated->braids_release);
    mutated->plaits_envelope.SetParameters(*mutated->plaits_attack, *mutated->plaits_decay,
                                           *mutated->plaits_sustain, *mutated->plaits_release);
    
    // Initialize modulation storage
    mutated->braids_raw_output = 0.0f;
    mutated->plaits_raw_output = 0.0f;
    
    // Get base parameter values (including levels)
    float base_braids_timbre = *mutated->braids_timbre;
    float base_braids_color = *mutated->braids_color;
    float base_braids_fm = *mutated->braids_fm;
    float base_braids_level = *mutated->braids_level;
    float base_plaits_harmonics = *mutated->plaits_harmonics;
    float base_plaits_timbre = *mutated->plaits_timbre;
    float base_plaits_morph = *mutated->plaits_morph;
    float base_plaits_lpg_decay = *mutated->plaits_lpg_decay;
    float base_plaits_lpg_colour = *mutated->plaits_lpg_colour;
    float base_plaits_level = *mutated->plaits_level;
    
    // Process in 24-sample blocks
    uint32_t offset = 0;
    while (offset < n_samples) {
        uint32_t block_size = n_samples - offset;
        if (block_size > 24) {
            block_size = 24;
        }
        
        // Apply modulation (per-block)
        float mod_braids_timbre = base_braids_timbre;
        float mod_braids_color = base_braids_color;
        float mod_braids_fm = base_braids_fm;
        float mod_braids_pitch = 0.0f;
        float mod_braids_level = base_braids_level;
        float mod_braids_out = 1.0f;  // Output scaling
        float mod_plaits_harmonics = base_plaits_harmonics;
        float mod_plaits_timbre = base_plaits_timbre;
        float mod_plaits_morph = base_plaits_morph;
        float mod_plaits_lpg_decay = base_plaits_lpg_decay;
        float mod_plaits_lpg_colour = base_plaits_lpg_colour;
        float mod_plaits_pitch = 0.0f;
        float mod_plaits_level = base_plaits_level;
        float mod_plaits_out = 1.0f;  // Output scaling
        
        // Process modulation slots
        for (int mod_slot = 0; mod_slot < 3; mod_slot++) {
            const float* src_param = (mod_slot == 0) ? mutated->mod1_source : 
                                     (mod_slot == 1) ? mutated->mod2_source : mutated->mod3_source;
            const float* tgt_param = (mod_slot == 0) ? mutated->mod1_target :
                                     (mod_slot == 1) ? mutated->mod2_target : mutated->mod3_target;
            const float* amt_param = (mod_slot == 0) ? mutated->mod1_amount :
                                     (mod_slot == 1) ? mutated->mod2_amount : mutated->mod3_amount;
            const float* det_param = (mod_slot == 0) ? mutated->mod1_detune :
                                     (mod_slot == 1) ? mutated->mod2_detune : mutated->mod3_detune;
            
            int source = (int)(*src_param);
            int target = (int)(*tgt_param);
            float amount = *amt_param;
            float detune = *det_param;
            
            if (source != MOD_SRC_NONE && target != MOD_TGT_NONE) {
                float mod_value = get_mod_source(mutated, source) * amount;
                
                switch (target) {
                    case MOD_TGT_BRAIDS_TIMBRE: mod_braids_timbre += mod_value; break;
                    case MOD_TGT_BRAIDS_COLOR: mod_braids_color += mod_value; break;
                    case MOD_TGT_BRAIDS_FM: mod_braids_fm += mod_value; break;
                    case MOD_TGT_BRAIDS_PITCH: mod_braids_pitch += detune * mod_value * 12.0f; break;
                    case MOD_TGT_BRAIDS_LEVEL: mod_braids_level += mod_value; break;
                    case MOD_TGT_BRAIDS_OUT: mod_braids_out += mod_value; break;
                    case MOD_TGT_PLAITS_HARMONICS: mod_plaits_harmonics += mod_value; break;
                    case MOD_TGT_PLAITS_TIMBRE: mod_plaits_timbre += mod_value; break;
                    case MOD_TGT_PLAITS_MORPH: mod_plaits_morph += mod_value; break;
                    case MOD_TGT_PLAITS_LPG_DECAY: mod_plaits_lpg_decay += mod_value; break;
                    case MOD_TGT_PLAITS_LPG_COLOUR: mod_plaits_lpg_colour += mod_value; break;
                    case MOD_TGT_PLAITS_PITCH: mod_plaits_pitch += detune * mod_value * 12.0f; break;
                    case MOD_TGT_PLAITS_LEVEL: mod_plaits_level += mod_value; break;
                    case MOD_TGT_PLAITS_OUT: mod_plaits_out += mod_value; break;
                }
            }
        }
        
        // Clamp modulated values
        mod_braids_timbre = Clip(mod_braids_timbre, 0.0f, 1.0f);
        mod_braids_color = Clip(mod_braids_color, 0.0f, 1.0f);
        mod_braids_level = Clip(mod_braids_level, 0.0f, 1.0f);
        mod_braids_out = Clip(mod_braids_out, 0.0f, 2.0f);  // Allow up to 2x boost
        mod_plaits_harmonics = Clip(mod_plaits_harmonics, 0.0f, 1.0f);
        mod_plaits_timbre = Clip(mod_plaits_timbre, 0.0f, 1.0f);
        mod_plaits_morph = Clip(mod_plaits_morph, 0.0f, 1.0f);
        mod_plaits_lpg_decay = Clip(mod_plaits_lpg_decay, 0.0f, 1.0f);
        mod_plaits_lpg_colour = Clip(mod_plaits_lpg_colour, 0.0f, 1.0f);
        mod_plaits_level = Clip(mod_plaits_level, 0.0f, 1.0f);
        mod_plaits_out = Clip(mod_plaits_out, 0.0f, 2.0f);  // Allow up to 2x boost
        
        // Apply glide (portamento) to notes
        float braids_target_note = (float)mutated->current_note;
        float plaits_target_note = (float)mutated->current_note;
        
        // Glide time: 0 = instant, 1 = 1 second
        float braids_glide_time = *mutated->braids_glide;
        float plaits_glide_time = *mutated->plaits_glide;
        
        if (braids_glide_time > 0.001f) {
            // Calculate glide coefficient (higher = slower glide)
            float braids_glide_coeff = 1.0f - expf(-1.0f / (braids_glide_time * mutated->sample_rate / 24.0f));
            mutated->braids_current_note += (braids_target_note - mutated->braids_current_note) * braids_glide_coeff;
        } else {
            mutated->braids_current_note = braids_target_note;
        }
        
        if (plaits_glide_time > 0.001f) {
            float plaits_glide_coeff = 1.0f - expf(-1.0f / (plaits_glide_time * mutated->sample_rate / 24.0f));
            mutated->plaits_current_note += (plaits_target_note - mutated->plaits_current_note) * plaits_glide_coeff;
        } else {
            mutated->plaits_current_note = plaits_target_note;
        }
        
        // Calculate note for Braids (with glide, FM, detune and modulation)
        float braids_note = mutated->braids_current_note + *mutated->braids_coarse + *mutated->braids_fine + 
                           *mutated->braids_detune + (mod_braids_fm * 12.0f) + mod_braids_pitch;
        
        // Calculate note for Plaits (with glide, detune and modulation)
        float plaits_note = mutated->plaits_current_note + *mutated->plaits_coarse + *mutated->plaits_fine + 
                           *mutated->plaits_detune + mod_plaits_pitch;
        
        // --- BRAIDS ---
        float braids_output[24] = {0};
        
        if (braids_enabled) {
            // Only set shape if it changed (to avoid unnecessary Strike() calls)
            if (braids_shape != mutated->previous_braids_shape) {
                mutated->osc.set_shape((braids::MacroOscillatorShape)braids_shape);
                mutated->previous_braids_shape = braids_shape;
            }
            
            // Set parameters (with modulation)
            int16_t timbre = (int16_t)(mod_braids_timbre * 32767.0f);
            int16_t color = (int16_t)(mod_braids_color * 32767.0f);
            mutated->osc.set_parameters(timbre, color);
            
            // Render if envelope is active (including release phase)
            if (mutated->braids_envelope.IsActive()) {
                // Set pitch
                int32_t pitch = static_cast<int32_t>((braids_note - 12.0f) * 128.0f);
                pitch = Clip(pitch, 0, 16383);
                mutated->osc.set_pitch(pitch);
                
                // Clear sync buffer (no hard sync)
                memset(mutated->sync_buffer, 0, block_size);
                
                // Render
                mutated->osc.Render(mutated->sync_buffer, mutated->render_buffer, block_size);
                
                // Convert to float and apply envelope
                float vel_scale = mutated->velocity / 127.0f;
                float sum_abs = 0.0f;
                for (uint32_t i = 0; i < block_size; i++) {
                    float env_value = mutated->braids_envelope.Process();
                    float raw = (mutated->render_buffer[i] / 32768.0f);
                    sum_abs += fabs(raw);  // Accumulate absolute values
                    braids_output[i] = raw * vel_scale * mod_braids_level * env_value;
                }
                // Store average absolute value for modulation
                mutated->braids_raw_output = sum_abs / block_size;
            }
        }
        
        // --- PLAITS ---
        float plaits_output[24] = {0};
        
        if (plaits_enabled && mutated->plaits_envelope.IsActive() && mutated->plaits_voice) {
            // Only set engine if it changed (to avoid unnecessary reinitialization)
            if (plaits_engine != mutated->previous_plaits_engine) {
                mutated->plaits_patch.engine = plaits_engine;
                mutated->previous_plaits_engine = plaits_engine;
            }
            
            // Set parameters (with modulation)
            mutated->plaits_patch.note = plaits_note;
            mutated->plaits_patch.harmonics = mod_plaits_harmonics;
            mutated->plaits_patch.timbre = mod_plaits_timbre;
            mutated->plaits_patch.morph = mod_plaits_morph;
            mutated->plaits_patch.decay = mod_plaits_lpg_decay;
            mutated->plaits_patch.lpg_colour = mod_plaits_lpg_colour;
            
            // Set modulations
            mutated->plaits_modulations.engine = 0.0f;
            mutated->plaits_modulations.note = 0.0f;
            mutated->plaits_modulations.frequency = 0.0f;
            mutated->plaits_modulations.harmonics = 0.0f;
            mutated->plaits_modulations.timbre = 0.0f;
            mutated->plaits_modulations.morph = 0.0f;
            mutated->plaits_modulations.trigger = (new_trigger && offset == 0) ? 1.0f : 0.0f;
            mutated->plaits_modulations.level = 1.0f;
            mutated->plaits_modulations.frequency_patched = false;
            mutated->plaits_modulations.timbre_patched = false;
            mutated->plaits_modulations.morph_patched = false;
            mutated->plaits_modulations.trigger_patched = true;
            mutated->plaits_modulations.level_patched = false;
            
            // Render
            plaits::Voice::Frame plaits_frames[24];
            mutated->plaits_voice->Render(mutated->plaits_patch, mutated->plaits_modulations, 
                                         plaits_frames, block_size);
            
            // Convert to float and apply envelope (use ONLY main output, not aux)
            float vel_scale = mutated->velocity / 127.0f;
            float sum_abs = 0.0f;
            for (uint32_t i = 0; i < block_size; i++) {
                float env_value = mutated->plaits_envelope.Process();
                float raw = (plaits_frames[i].out / 32768.0f);
                sum_abs += fabs(raw);  // Accumulate absolute values
                plaits_output[i] = raw * vel_scale * mod_plaits_level * env_value;
            }
            // Store average absolute value for modulation
            mutated->plaits_raw_output = sum_abs / block_size;
        }
        
        // Apply filter
        int filter_type = (int)(*mutated->filter_type);
        int filter_routing = (int)(*mutated->filter_routing);
        float cutoff = *mutated->filter_cutoff;
        float resonance = *mutated->filter_resonance;
        
        if (filter_type != FILTER_OFF) {
            for (uint32_t i = 0; i < block_size; i++) {
                float filtered_braids = braids_output[i];
                float filtered_plaits = plaits_output[i];
                
                // Apply filter based on routing
                if (filter_routing == ROUTE_BRAIDS || filter_routing == ROUTE_BOTH) {
                    if (filter_type == FILTER_MOOG) {
                        filtered_braids = mutated->moog_filter.Process(braids_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_MS20) {
                        filtered_braids = mutated->ms20_filter.Process(braids_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_TB303) {
                        filtered_braids = mutated->tb303_filter.Process(braids_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_SEM) {
                        filtered_braids = mutated->sem_filter.Process(braids_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_SALLENKEY) {
                        filtered_braids = mutated->sallenkey_filter.Process(braids_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_DIODE) {
                        filtered_braids = mutated->diode_filter.Process(braids_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_OBERHEIM) {
                        filtered_braids = mutated->oberheim_filter.Process(braids_output[i], cutoff, resonance);
                    }
                }
                
                if (filter_routing == ROUTE_PLAITS || filter_routing == ROUTE_BOTH) {
                    if (filter_type == FILTER_MOOG) {
                        filtered_plaits = mutated->moog_filter.Process(plaits_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_MS20) {
                        filtered_plaits = mutated->ms20_filter.Process(plaits_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_TB303) {
                        filtered_plaits = mutated->tb303_filter.Process(plaits_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_SEM) {
                        filtered_plaits = mutated->sem_filter.Process(plaits_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_SALLENKEY) {
                        filtered_plaits = mutated->sallenkey_filter.Process(plaits_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_DIODE) {
                        filtered_plaits = mutated->diode_filter.Process(plaits_output[i], cutoff, resonance);
                    } else if (filter_type == FILTER_OBERHEIM) {
                        filtered_plaits = mutated->oberheim_filter.Process(plaits_output[i], cutoff, resonance);
                    }
                }
                
                braids_output[i] = filtered_braids;
                plaits_output[i] = filtered_plaits;
            }
        }
        
        // Apply second filter (cascaded after first filter)
        int filter2_type = (int)(*mutated->filter2_type);
        int filter2_routing = (int)(*mutated->filter2_routing);
        float cutoff2 = *mutated->filter2_cutoff;
        float resonance2 = *mutated->filter2_resonance;
        
        if (filter2_type != FILTER_OFF) {
            for (uint32_t i = 0; i < block_size; i++) {
                float filtered_braids = braids_output[i];
                float filtered_plaits = plaits_output[i];
                
                // Apply second filter based on routing
                if (filter2_routing == ROUTE_BRAIDS || filter2_routing == ROUTE_BOTH) {
                    if (filter2_type == FILTER_MOOG) {
                        filtered_braids = mutated->moog_filter2.Process(braids_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_MS20) {
                        filtered_braids = mutated->ms20_filter2.Process(braids_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_TB303) {
                        filtered_braids = mutated->tb303_filter2.Process(braids_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_SEM) {
                        filtered_braids = mutated->sem_filter2.Process(braids_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_SALLENKEY) {
                        filtered_braids = mutated->sallenkey_filter2.Process(braids_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_DIODE) {
                        filtered_braids = mutated->diode_filter2.Process(braids_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_OBERHEIM) {
                        filtered_braids = mutated->oberheim_filter2.Process(braids_output[i], cutoff2, resonance2);
                    }
                }
                
                if (filter2_routing == ROUTE_PLAITS || filter2_routing == ROUTE_BOTH) {
                    if (filter2_type == FILTER_MOOG) {
                        filtered_plaits = mutated->moog_filter2.Process(plaits_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_MS20) {
                        filtered_plaits = mutated->ms20_filter2.Process(plaits_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_TB303) {
                        filtered_plaits = mutated->tb303_filter2.Process(plaits_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_SEM) {
                        filtered_plaits = mutated->sem_filter2.Process(plaits_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_SALLENKEY) {
                        filtered_plaits = mutated->sallenkey_filter2.Process(plaits_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_DIODE) {
                        filtered_plaits = mutated->diode_filter2.Process(plaits_output[i], cutoff2, resonance2);
                    } else if (filter2_type == FILTER_OBERHEIM) {
                        filtered_plaits = mutated->oberheim_filter2.Process(plaits_output[i], cutoff2, resonance2);
                    }
                }
                
                braids_output[i] = filtered_braids;
                plaits_output[i] = filtered_plaits;
            }
        }
        
        // Get panning values (0 = left, 0.5 = center, 1 = right)
        float braids_pan = *mutated->braids_pan;
        float plaits_pan = *mutated->plaits_pan;
        
        // Calculate pan gains using constant power panning
        float braids_pan_l = cosf(braids_pan * M_PI * 0.5f);
        float braids_pan_r = sinf(braids_pan * M_PI * 0.5f);
        float plaits_pan_l = cosf(plaits_pan * M_PI * 0.5f);
        float plaits_pan_r = sinf(plaits_pan * M_PI * 0.5f);
        
        // Mix to output (stereo) with output modulation and panning
        for (uint32_t i = 0; i < block_size; i++) {
            float braids_final = braids_output[i] * mod_braids_out;
            float plaits_final = plaits_output[i] * mod_plaits_out;
            
            // Apply panning to each oscillator
            float braids_l = braids_final * braids_pan_l;
            float braids_r = braids_final * braids_pan_r;
            float plaits_l = plaits_final * plaits_pan_l;
            float plaits_r = plaits_final * plaits_pan_r;
            
            // Mix and scale
            float mixed_l = (braids_l + plaits_l) * 0.7f;
            float mixed_r = (braids_r + plaits_r) * 0.7f;
            
            mutated->out_l[offset + i] = mixed_l * 10.0f;  // Scale to ±5V
            mutated->out_r[offset + i] = mixed_r * 10.0f;
        }
        
        offset += block_size;
        new_trigger = false;
    }
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
    Mutated* mutated = (Mutated*)instance;
    if (mutated) {
        if (mutated->plaits_voice) {
            delete mutated->plaits_voice;
        }
        free(mutated);
    }
}

static const void*
extension_data(const char* uri)
{
    return NULL;
}

static const LV2_Descriptor descriptor = {
    MUTATED_URI,
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

