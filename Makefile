# Mutable Instruments LV2 Plugins Makefile
# Supports both x86_64 (native) and ARM64 (cross-compile) builds

# Paths
AUDIBLE_DIR = ./AudibleInstruments
EURORACK_DIR = $(AUDIBLE_DIR)/eurorack
SRC_DIR = ./src

# Build architecture selection
# Use: make ARCH=x86_64 for native x86_64 builds (default)
# Use: make ARCH=aarch64 for ARM64 cross-compilation
ARCH ?= x86_64

# Architecture-specific configuration
ifeq ($(ARCH),aarch64)
    CXX = aarch64-linux-gnu-g++
    ARCH_FLAGS = -march=armv8-a+crc -mcpu=cortex-a72 -mtune=cortex-a72
    BUILD_DIR = ./aarch64-build
else ifeq ($(ARCH),x86_64)
    CXX = g++
    ARCH_FLAGS = -march=native
    BUILD_DIR = ./x86_64-build
else
    $(error Unsupported ARCH: $(ARCH). Use x86_64 or aarch64)
endif

CXXFLAGS = -std=c++11 -O2 -fPIC -DTEST \
	$(ARCH_FLAGS) \
	-fno-finite-math-only \
	-I$(EURORACK_DIR) \
	-Wno-unused-local-typedefs \
	-Wno-unused-parameter

LDFLAGS = -shared -lm

# Plugin names
PLUGINS = plaits braids marbles

# Default target - build both architectures
all: x86_64 aarch64

# Architecture-specific targets
x86_64:
	@$(MAKE) ARCH=x86_64 plugins

aarch64:
	@$(MAKE) ARCH=aarch64 plugins

# Build plugins for current architecture
plugins: $(PLUGINS)

# Common Mutable Instruments source files
STMLIB_SOURCES = \
	$(EURORACK_DIR)/stmlib/utils/random.cc \
	$(EURORACK_DIR)/stmlib/dsp/atan.cc \
	$(EURORACK_DIR)/stmlib/dsp/units.cc

# Plaits sources
PLAITS_SOURCES = \
	$(wildcard $(EURORACK_DIR)/plaits/dsp/*.cc) \
	$(wildcard $(EURORACK_DIR)/plaits/dsp/engine/*.cc) \
	$(wildcard $(EURORACK_DIR)/plaits/dsp/speech/*.cc) \
	$(wildcard $(EURORACK_DIR)/plaits/dsp/physical_modelling/*.cc) \
	$(AUDIBLE_DIR)/eurorack/plaits/resources.cc

# Braids sources
BRAIDS_SOURCES = \
	$(EURORACK_DIR)/braids/macro_oscillator.cc \
	$(EURORACK_DIR)/braids/analog_oscillator.cc \
	$(EURORACK_DIR)/braids/digital_oscillator.cc \
	$(EURORACK_DIR)/braids/quantizer.cc \
	$(EURORACK_DIR)/braids/resources.cc

# Tides sources
TIDES_SOURCES = \
	$(EURORACK_DIR)/tides/generator.cc \
	$(EURORACK_DIR)/tides/resources.cc



# Stages sources
STAGES_SOURCES = \
	$(EURORACK_DIR)/stages/segment_generator.cc \
	$(EURORACK_DIR)/stages/resources.cc \
	$(EURORACK_DIR)/stages/ramp_extractor.cc

# Rings sources
RINGS_SOURCES = \
	$(EURORACK_DIR)/rings/dsp/part.cc \
	$(EURORACK_DIR)/rings/dsp/string.cc \
	$(EURORACK_DIR)/rings/dsp/resonator.cc \
	$(EURORACK_DIR)/rings/dsp/fm_voice.cc \
	$(EURORACK_DIR)/rings/dsp/string_synth_part.cc \
	$(EURORACK_DIR)/rings/resources.cc

# Marbles sources
MARBLES_SOURCES = \
	$(EURORACK_DIR)/marbles/random/t_generator.cc \
	$(EURORACK_DIR)/marbles/random/x_y_generator.cc \
	$(EURORACK_DIR)/marbles/random/output_channel.cc \
	$(EURORACK_DIR)/marbles/random/quantizer.cc \
	$(EURORACK_DIR)/marbles/random/lag_processor.cc \
	$(EURORACK_DIR)/marbles/random/discrete_distribution_quantizer.cc \
	$(EURORACK_DIR)/marbles/ramp/ramp_extractor.cc \
	$(EURORACK_DIR)/marbles/resources.cc

# Build Plaits
plaits: $(BUILD_DIR)/mi_plaits.lv2/plaits.so $(BUILD_DIR)/mi_plaits.lv2/manifest.ttl $(BUILD_DIR)/mi_plaits.lv2/plaits.ttl

$(BUILD_DIR)/mi_plaits.lv2/plaits.so: $(SRC_DIR)/plaits.cpp $(PLAITS_SOURCES) $(RINGS_SOURCES) $(TIDES_SOURCES) $(STAGES_SOURCES) $(STMLIB_SOURCES)
	@echo "Building Plaits..."
	@mkdir -p $(BUILD_DIR)/mi_plaits.lv2
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(PLAITS_SOURCES) $(RINGS_SOURCES) $(TIDES_SOURCES) $(STAGES_SOURCES) $(STMLIB_SOURCES)
	@echo "Plaits built successfully!"

$(BUILD_DIR)/mi_plaits.lv2/manifest.ttl: $(SRC_DIR)/plaits_manifest.ttl
	@mkdir -p $(BUILD_DIR)/mi_plaits.lv2
	@cp $< $@

$(BUILD_DIR)/mi_plaits.lv2/plaits.ttl: $(SRC_DIR)/plaits.ttl
	@mkdir -p $(BUILD_DIR)/mi_plaits.lv2
	@cp $< $@

# Build Braids
braids: $(BUILD_DIR)/mi_braids.lv2/braids.so $(BUILD_DIR)/mi_braids.lv2/manifest.ttl $(BUILD_DIR)/mi_braids.lv2/braids.ttl

$(BUILD_DIR)/mi_braids.lv2/braids.so: $(SRC_DIR)/braids.cpp $(BRAIDS_SOURCES) $(RINGS_SOURCES) $(TIDES_SOURCES) $(STAGES_SOURCES) $(STMLIB_SOURCES)
	@echo "Building Braids..."
	@mkdir -p $(BUILD_DIR)/mi_braids.lv2
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(BRAIDS_SOURCES) $(RINGS_SOURCES) $(TIDES_SOURCES) $(STAGES_SOURCES) $(STMLIB_SOURCES)
	@echo "Braids built successfully!"

$(BUILD_DIR)/mi_braids.lv2/manifest.ttl: $(SRC_DIR)/braids_manifest.ttl
	@mkdir -p $(BUILD_DIR)/mi_braids.lv2
	@cp $< $@

$(BUILD_DIR)/mi_braids.lv2/braids.ttl: $(SRC_DIR)/braids.ttl
	@mkdir -p $(BUILD_DIR)/mi_braids.lv2
	@cp $< $@

# Build Marbles
marbles: $(BUILD_DIR)/mi_marbles.lv2/marbles.so $(BUILD_DIR)/mi_marbles.lv2/manifest.ttl $(BUILD_DIR)/mi_marbles.lv2/marbles.ttl

$(BUILD_DIR)/mi_marbles.lv2/marbles.so: $(SRC_DIR)/marbles.cpp $(MARBLES_SOURCES) $(STMLIB_SOURCES)
	@echo "Building Marbles..."
	@mkdir -p $(BUILD_DIR)/mi_marbles.lv2
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(MARBLES_SOURCES) $(STMLIB_SOURCES)
	@echo "Marbles built successfully!"

$(BUILD_DIR)/mi_marbles.lv2/manifest.ttl: $(SRC_DIR)/marbles_manifest.ttl
	@mkdir -p $(BUILD_DIR)/mi_marbles.lv2
	@cp $< $@

$(BUILD_DIR)/mi_marbles.lv2/marbles.ttl: $(SRC_DIR)/marbles.ttl
	@mkdir -p $(BUILD_DIR)/mi_marbles.lv2
	@cp $< $@

# Install target
install: all
	@echo "Plugins built and ready to deploy from $(BUILD_DIR)/"
	@echo "Deploy to Zynthian: scp -r $(BUILD_DIR)/*.lv2 root@zynthian.local:/zynthian/zynthian-plugins/lv2/"

# Clean target
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete!"

# Verify target - checks bundle integrity
verify:
	@echo "Verifying LV2 bundles in $(BUILD_DIR)..."
	@for plugin in $(PLUGINS); do \
		echo "Checking mi_$$plugin.lv2:"; \
		if [ -f $(BUILD_DIR)/mi_$$plugin.lv2/$$plugin.so ]; then \
			arch_info=$$(file $(BUILD_DIR)/mi_$$plugin.lv2/$$plugin.so); \
			echo "  ✓ Binary: $$arch_info"; \
		else \
			echo "  ✗ Missing $$plugin.so"; \
		fi; \
		if [ -f $(BUILD_DIR)/mi_$$plugin.lv2/manifest.ttl ]; then \
			echo "  ✓ manifest.ttl present"; \
		else \
			echo "  ✗ Missing manifest.ttl"; \
		fi; \
		if [ -f $(BUILD_DIR)/mi_$$plugin.lv2/$$plugin.ttl ]; then \
			echo "  ✓ $$plugin.ttl present"; \
		else \
			echo "  ✗ Missing $$plugin.ttl"; \
		fi; \
		extra_files=$$(ls $(BUILD_DIR)/mi_$$plugin.lv2/ | grep -v "\.so$$" | grep -v "\.ttl$$" | wc -l); \
		if [ $$extra_files -eq 0 ]; then \
			echo "  ✓ No extra files"; \
		else \
			echo "  ⚠ Warning: Extra files found"; \
			ls $(BUILD_DIR)/mi_$$plugin.lv2/; \
		fi; \
		echo ""; \
	done

# Help target
help:
	@echo "Mutable Instruments LV2 Plugins"
	@echo ""
	@echo "Multi-Architecture Build System:"
	@echo "  make              - Build both x86_64 and aarch64 (default)"
	@echo "  make x86_64       - Build x86_64 only"
	@echo "  make aarch64      - Build aarch64 only"
	@echo "  make ARCH=x86_64  - Build x86_64 explicitly"
	@echo "  make ARCH=aarch64 - Build ARM64 (Zynthian cross-compile)"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build all plugins for both x86_64 and aarch64 (default)"
	@echo "  x86_64     - Build for x86_64 only"
	@echo "  aarch64    - Build for aarch64 only"
	@echo "  plugins    - Build plugins for current ARCH"
	@echo "  plaits     - Build Plaits macro oscillator"
	@echo "  braids     - Build Braids macro oscillator (with Rings resonator)"
	@echo "  marbles    - Build Marbles random modulation/gate generator"
	@echo "  install    - Build and prepare for deployment"
	@echo "  verify     - Verify bundle integrity"
	@echo "  clean      - Remove build artifacts"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Current configuration:"
	@echo "  ARCH: $(ARCH)"
	@echo "  CXX: $(CXX)"
	@echo "  BUILD_DIR: $(BUILD_DIR)"
	@echo ""
	@echo "Examples:"
	@echo "  make                            # Build both x86_64 and aarch64"
	@echo "  make clean && make              # Clean and build both architectures"
	@echo "  make x86_64                     # Build for x86_64 testing only"
	@echo "  make aarch64                    # Build for Zynthian ARM64 only"
	@echo "  make clean && make ARCH=aarch64 # Clean and build ARM64 explicitly"
	@echo "  make verify                     # Check build integrity"
	@echo ""
	@echo "Deploy to Zynthian (ARM64 only):"
	@echo "  scp -r aarch64-build/*.lv2 root@zynthian.local:/zynthian/zynthian-plugins/lv2/"

.PHONY: all x86_64 aarch64 plugins plaits braids marbles install verify clean help

