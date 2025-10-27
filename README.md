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
- **T outputs**: 3 channels of random voltages/gates
  - Rate: -48 to +48 semitones (with tempo sync)
  - Bias, Jitter, Steps, Length controls
  - Mode: Async/Locked/Locked with Ratcheting
  - Range: Slow (÷16), Medium (1x), Fast (×16)
- **X/Y outputs**: 3 channels each of smooth random voltages
  - Spread, Bias, Steps controls
