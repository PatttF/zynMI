# Zynthian & Linux LV2 Plugins

ARM64(for Zynthian) and Linux x86-64 LV2 plugins featuring Plaits and Braids macro oscillators  Marbles random generator. Thanks go out to Mutable Instruments and Zynthian. 

## Building

```bash
# Build all plugins for both x86_64 (testing) and aarch64 (Zynthian)
make all

# Build for specific architecture
make x86_64    # Native testing
make aarch64   # Zynthian ARM64

# Build individual plugins
make plaits
make braids
make marbles
make mutated

# Verify bundles
make verify

# Clean build artifacts
make clean
```

## Deployment to Zynthian

```bash
# Deploy to Zynthian
scp -r aarch64-build/*.lv2 root@zynthian.local:/zynthian/zynthian-plugins/lv2/

# Or use the shortcut
make install  # Shows the deploy command
```



## About Mutated Instruments

**Mutated Instruments** is a custom dual-oscillator synthesizer plugin that combines Mutable Instruments Braids and Plaits macro oscillators with a powerful 4x4 modulation matrix, dual mixers, filters, and full envelope/LFO control. It was created to provide complex modulation routing and sound design capabilities not found in the standalone Braids or Plaits plugins.

### Architecture

- **Two Independent Oscillators (Braids and Plaits)**
- **4x4 Modulation Matrix**: Route any of 4 sources (OSC A output, OSC B output, ADSR Envelope, LFO) to any of 4 destinations per oscillator (Pitch, Timbre, Color, Shape) with individual depth controls
- **Dual Mixer System**:
  - Pre-filter mixer with A/B balance and independent level controls
  - Post-filter master mixer
- **Shared Processing**:
  - 7 filter types (Moog Ladder, MS20, SVF LP/BP/HP, One-Pole LP)
  - Full ADSR envelope with shape controls
  - LFO with 5 waveforms and tempo sync
- **LPG (Low Pass Gate)**: Adds vactrol-style dynamics and filtering

This design enables rich, evolving timbres through cross-modulation between oscillators, rhythmic modulation from the LFO, and dynamic envelope control over multiple parameters simultaneously.

### Use Cases

- **Complex drones**: Two different algorithms with slow LFO modulation
- **Rhythmic patches**: LFO modulating timbre/color of both oscillators at different rates
- **Evolving textures**: Envelope modulating multiple parameters creates dynamic, animated sounds
- **Cross-modulation**: Route OSC A → OSC B pitch for FM-style effects
- **Layered synthesis**: Blend two different synthesis methods (e.g., analog sawtooth + FM)

-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
### About Chaos Engine

- **MIDI random generator** with host tempo sync
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
