# Zynthian & VST plugins

zynMI (for Zynthian), VST3(Windows and Linux) plugins featuring Mutated Instruments, Chaos Engine random generator, and Mutated Sequences rhythm generator. Thanks go out to Mutable Instruments and Zynthian. 




## About Mutated Instruments

**Mutated Instruments** is a custom dual-oscillator synthesizer plugin that combines Mutable Instruments Braids and Plaits macro oscillators with a powerful 4x4 modulation matrix, dual mixers, filters, full envelope/LFO control, and several effects. It was created to provide complex modulation routing and sound design capabilities not found in the standalone Braids or Plaits plugins.

### Architecture

- **Two Independent Oscillators (Braids and Plaits) with all engines and controls**
  - Per oscillator effects (Ring Mod, Bit Crusher) and Overdrive
- **4x4 Modulation Matrix**: Route or sync any source (OSC A output, OSC B output, ADSR Envelope, LFO, etc...) to any destination with individual depth controls or sync controls.
- **Shared Processing**:
  - 4 filter types
  - Full ADSR envelope controls
  - LFO with 5 waveforms and tempo sync
  - 4 different very powerful global effects (Reverb, Delay, Tape, and Glitch)

This design enables rich, evolving timbres through cross-modulation between oscillators, rhythmic modulation from the LFO, dynamic envelope control over multiple parameters simultaneously, and a million ways to destroy and tweak it all with effects.
### VST3 Features

- **OpenGL-based UI**: Modern tabbed interface with background image and audio reactive effects
- **Cross-platform**: Native Linux, Windows, Mac, and Zynthian
- **Real-time Control**: Full parameter automation and MIDI learn support

### Use Cases

- **Complex drones**: Two different algorithms with slow LFO modulation
- **Rhythmic patches**: LFO modulating timbre/color of both oscillators at different rates
- **Evolving textures**: Envelope modulating multiple parameters creates dynamic, animated sounds
- **Cross-modulation**: Route OSC A → OSC B pitch for FM-style effects
- **Layered synthesis**: Blend two different synthesis methods (e.g., analog sawtooth + FM)

-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
### About Chaos Engine

- **MIDI random generator** port of Mutated Instruments Marbles with host tempo sync
- **T outputs**: 3 channels of random gates (T1, T2, T3 as MIDI notes 60, 61, 62)
  - **T1**: Random slave gate derived from T2 (slave channel 0)
  - **T2**: Master clock with jitter control (the main rhythmic clock)
  - **T3**: Random slave gate derived from T2 (slave channel 1)
  - Rate: -48 to +48 semitones (with tempo sync to host BPM)
  - Bias: Controls probability distribution between T1 and T3
  - Jitter: Clock timing randomness (stable to chaotic)
  - Mode: 6 generation modes (Complementary Bernoulli, Clusters, Drums, Independent Bernoulli, Divider, Three States)
  - Range: Slow (0.25x), Medium (1x), Fast (4x)
- **X/Y outputs**: Random CV voltages as MIDI CC (X1, X2, X3 as CC 1-3, Y as CC 4)
  - Spread, Bias, Steps controls
  - Voltage ranges: ±2V, 0-5V, ±5V
  - Control modes: Identical, Bump, Tilt

-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
## About Mutated Sequences

**Mutated Sequences** is a DFAM-inspired 8-step MIDI rhythm sequencer with per-step pitch, velocity, probability, and ratcheting. Create complex drum patterns, melodic sequences, and generative rhythms with extensive creative controls. Available for Zynthian with full MIDI output capabilities.

### Sequencer Features

- **8-step sequencer** with individual control per step:
  - **Pitch**: MIDI note (0-127)
  - **Velocity**: Note velocity (0-127)
  - **Probability**: Step trigger probability (0-100%)
  - **Ratchet**: Repeat count per step (1-8)
- **Global Controls**:
  - **Clock Source**: Internal, MIDI Clock, or Host Tempo sync
  - **BPM**: Internal clock tempo (20-300 BPM)
  - **Clock Division**: 1/4, 1/8, 1/16, 1/32 notes
  - **Swing**: Groove timing (0-100%)
  - **Gate Length**: Note duration as % of step
  - **Num Steps**: Active sequence length (1-8)
  - **Transpose**: Global pitch offset (-24 to +24 semitones)

### Creative Controls

- **Pattern Modes**: 10 rhythm formulas/patterns with tweakable parameter
- **Velocity Modes**:
  - Manual (per-step control)
  - Accent (emphasize certain beats)
  - Ramp Up/Down (dynamic curves)
  - Random (generative variation)
- **Pitch Modes**:
  - Manual (per-step control)
  - Euclidean (algorithmic distribution)
  - Pentatonic/Chromatic scales
- **Generative Features**:
  - **Probability**: Global gate probability control
  - **Humanize**: Timing variation (0-100%)
  - **Mutate**: Random parameter changes on loop (0-100%)
  - **Pitch Spread**: Range for generative pitch modes

### Use Cases

- **Drum programming**: Create complex rhythm patterns with ratchets and probability
- **Melodic sequences**: Step-sequenced basslines and melodies with scale quantization
- **Generative music**: Use probability, humanize, and mutate for evolving patterns
- **Live performance**: Real-time pattern tweaking with creative controls
- **Polyrhythms**: Variable step lengths (1-8) create complex metric relationships
