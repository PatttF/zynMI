# Mutable Instruments LV2 Plugins

ARM64(for Zynthian) and Linux x86-64 LV2 plugins featuring Plaits and Braids macro oscillators with Rings resonator, 7 filter types, and Marbles random generator.

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

## Features

### MI Plaits+

- **16 synthesis models**: Virtual analog, FM, physical modeling, granular, speech, drums
- **Rings resonator**: 6 models (Modal, Sympathetic String, Inharmonic, FM, Quantized, Reverb)
- **7 filter types**: Moog Ladder, MS20, SVF (LP/BP/HP), One-Pole LP
- **Full ADSR envelope** with attack/decay/release shape controls
- **LPG (Low Pass Gate)** with color and decay


### MI Braids+

- **48 synthesis algorithms**: Analog modeling, digital, FM, physical modeling, noise, percussion
- **Rings resonator**: 6 models (Modal, Sympathetic String, Inharmonic, FM, Quantized, Reverb)
- **7 filter types**: Moog Ladder, MS20, SVF (LP/BP/HP), One-Pole LP
- **Full ADSR envelope** with attack/decay/release shape controls

### MI Marbles

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
