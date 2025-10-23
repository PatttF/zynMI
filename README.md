# Mutable Instruments LV2 Plugins

Plugins for Zynthian featuring Plaits and Braids oscillators with Stages envelopes. Thanks to Mutable Instruments for all of the wonderful work and for releasing open source.
## Building

```bash
# Build both plugins
make all

# Install to aarch64-build/
make install

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

### Plaits
- 16 synthesis models (virtual analog, FM, physical modeling, drums, etc.)
- Full ADSR envelope with shape controls
- LPG (Low Pass Gate) with color and decay
- Stereo output

### Braids
- 48 synthesis algorithms
- Full ADSR envelope with shape controls
- Fine/Coarse tuning
- Timbre and Color controls
- Mono output
