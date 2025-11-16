# Mutable Instruments LV2 Plugins Makefile
# Supports both x86_64 (native) and ARM64 (cross-compile) builds

# Paths
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
	-I$(SRC_DIR) \
	-Wno-unused-local-typedefs \
	-Wno-unused-parameter

LDFLAGS = -shared -lm

# UI-specific flags
UI_CXXFLAGS = $(CXXFLAGS) -I/usr/include/GL
UI_LDFLAGS = -shared -lGL -lGLU -lGLX -lX11 -lm

# Plugin names
PLUGINS = mutated mutated_sequences

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
	$(SRC_DIR)/stmlib/utils/random.cc \
	$(SRC_DIR)/stmlib/dsp/atan.cc \
	$(SRC_DIR)/stmlib/dsp/units.cc

# Plaits sources (used by Mutated Instruments)
PLAITS_SOURCES = \
	$(wildcard $(SRC_DIR)/plaits/dsp/*.cc) \
	$(wildcard $(SRC_DIR)/plaits/dsp/engine/*.cc) \
	$(wildcard $(SRC_DIR)/plaits/dsp/speech/*.cc) \
	$(wildcard $(SRC_DIR)/plaits/dsp/physical_modelling/*.cc) \
	$(SRC_DIR)/plaits/resources.cc

# Braids sources (used by Mutated Instruments)
BRAIDS_SOURCES = \
	$(SRC_DIR)/braids/macro_oscillator.cc \
	$(SRC_DIR)/braids/analog_oscillator.cc \
	$(SRC_DIR)/braids/digital_oscillator.cc \
	$(SRC_DIR)/braids/quantizer.cc \
	$(SRC_DIR)/braids/resources.cc

# Marbles sources (used by Chaos Engine)
MARBLES_SOURCES = \
	$(SRC_DIR)/marbles/random/t_generator.cc \
	$(SRC_DIR)/marbles/random/x_y_generator.cc \
	$(SRC_DIR)/marbles/random/output_channel.cc \
	$(SRC_DIR)/marbles/random/quantizer.cc \
	$(SRC_DIR)/marbles/random/lag_processor.cc \
	$(SRC_DIR)/marbles/random/discrete_distribution_quantizer.cc \
	$(SRC_DIR)/marbles/ramp/ramp_extractor.cc \
	$(SRC_DIR)/marbles/resources.cc

# Build Marbles (Chaos Engine)
marbles: $(BUILD_DIR)/mi_chaosengine.lv2/marbles.so $(BUILD_DIR)/mi_chaosengine.lv2/manifest.ttl $(BUILD_DIR)/mi_chaosengine.lv2/marbles.ttl

$(BUILD_DIR)/mi_chaosengine.lv2/marbles.so: $(SRC_DIR)/marbles.cpp $(MARBLES_SOURCES) $(STMLIB_SOURCES)
	@echo "Building Chaos Engine..."
	@mkdir -p $(BUILD_DIR)/mi_chaosengine.lv2
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(MARBLES_SOURCES) $(STMLIB_SOURCES)
	@echo "Chaos Engine built successfully!"

$(BUILD_DIR)/mi_chaosengine.lv2/manifest.ttl: $(SRC_DIR)/marbles_manifest.ttl
	@mkdir -p $(BUILD_DIR)/mi_chaosengine.lv2
	@cp $< $@

$(BUILD_DIR)/mi_chaosengine.lv2/marbles.ttl: $(SRC_DIR)/marbles.ttl
	@mkdir -p $(BUILD_DIR)/mi_chaosengine.lv2
	@cp $< $@

# Build Mutated Instruments
ifeq ($(ARCH),x86_64)
    MUTATED_UI_TARGETS = $(BUILD_DIR)/mi_mutated.lv2/mutated_ui.so $(BUILD_DIR)/mi_mutated.lv2/mutated_ui.ttl
else
    MUTATED_UI_TARGETS =
endif

mutated: $(BUILD_DIR)/mi_mutated.lv2/mutated.so $(MUTATED_UI_TARGETS) $(BUILD_DIR)/mi_mutated.lv2/manifest.ttl $(BUILD_DIR)/mi_mutated.lv2/mutated.ttl

$(BUILD_DIR)/mi_mutated.lv2/mutated.so: $(SRC_DIR)/mutated.cpp $(BRAIDS_SOURCES) $(PLAITS_SOURCES) $(STMLIB_SOURCES)
	@echo "Building Mutated Instruments..."
	@mkdir -p $(BUILD_DIR)/mi_mutated.lv2
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(BRAIDS_SOURCES) $(PLAITS_SOURCES) $(STMLIB_SOURCES)
	@echo "Mutated Instruments built successfully!"

$(BUILD_DIR)/mi_mutated.lv2/mutated_ui.so: $(SRC_DIR)/mutated_ui.cpp $(SRC_DIR)/imgui/imgui.cpp $(SRC_DIR)/imgui/imgui_draw.cpp $(SRC_DIR)/imgui/imgui_widgets.cpp $(SRC_DIR)/imgui/imgui_tables.cpp $(SRC_DIR)/imgui/imgui_impl_opengl2.cpp
	@echo "Building Mutated Instruments UI (x86_64 only)..."
	@mkdir -p $(BUILD_DIR)/mi_mutated.lv2
	$(CXX) $(UI_CXXFLAGS) -I$(SRC_DIR)/imgui -o $@ $< $(SRC_DIR)/imgui/imgui.cpp $(SRC_DIR)/imgui/imgui_draw.cpp $(SRC_DIR)/imgui/imgui_widgets.cpp $(SRC_DIR)/imgui/imgui_tables.cpp $(SRC_DIR)/imgui/imgui_impl_opengl2.cpp $(UI_LDFLAGS)
	@echo "Mutated Instruments UI built successfully!"

$(BUILD_DIR)/mi_mutated.lv2/manifest.ttl: $(SRC_DIR)/mutated_manifest.ttl
	@mkdir -p $(BUILD_DIR)/mi_mutated.lv2
	@cp $< $@

$(BUILD_DIR)/mi_mutated.lv2/mutated.ttl: $(SRC_DIR)/mutated.ttl
	@mkdir -p $(BUILD_DIR)/mi_mutated.lv2
	@cp $< $@

$(BUILD_DIR)/mi_mutated.lv2/mutated_ui.ttl: $(SRC_DIR)/mutated_ui.ttl
	@mkdir -p $(BUILD_DIR)/mi_mutated.lv2
	@cp $< $@

# Build Mutated Sequences
mutated_sequences: $(BUILD_DIR)/mi_mutated_sequences.lv2/mutated_sequences.so $(BUILD_DIR)/mi_mutated_sequences.lv2/manifest.ttl $(BUILD_DIR)/mi_mutated_sequences.lv2/mutated_sequences.ttl

$(BUILD_DIR)/mi_mutated_sequences.lv2/mutated_sequences.so: $(SRC_DIR)/mutated_sequences.cpp
	@echo "Building Mutated Sequences..."
	@mkdir -p $(BUILD_DIR)/mi_mutated_sequences.lv2
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $<
	@echo "Mutated Sequences built successfully!"

$(BUILD_DIR)/mi_mutated_sequences.lv2/manifest.ttl: $(SRC_DIR)/mutated_sequences_manifest.ttl
	@mkdir -p $(BUILD_DIR)/mi_mutated_sequences.lv2
	@cp $< $@

$(BUILD_DIR)/mi_mutated_sequences.lv2/mutated_sequences.ttl: $(SRC_DIR)/mutated_sequences.ttl
	@mkdir -p $(BUILD_DIR)/mi_mutated_sequences.lv2
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
		bundle_name=mi_$$plugin.lv2; \
		if [ "$$plugin" = "marbles" ]; then bundle_name=mi_chaosengine.lv2; fi; \
		echo "Checking $$bundle_name:"; \
		if [ -f $(BUILD_DIR)/$$bundle_name/$$plugin.so ]; then \
			arch_info=$$(file $(BUILD_DIR)/$$bundle_name/$$plugin.so); \
			echo "  ✓ Binary: $$arch_info"; \
		else \
			echo "  ✗ Missing $$plugin.so"; \
		fi; \
		if [ -f $(BUILD_DIR)/$$bundle_name/manifest.ttl ]; then \
			echo "  ✓ manifest.ttl present"; \
		else \
			echo "  ✗ Missing manifest.ttl"; \
		fi; \
		if [ -f $(BUILD_DIR)/$$bundle_name/$$plugin.ttl ]; then \
			echo "  ✓ $$plugin.ttl present"; \
		else \
			echo "  ✗ Missing $$plugin.ttl"; \
		fi; \
		extra_files=$$(ls $(BUILD_DIR)/$$bundle_name/ | grep -v "\.so$$" | grep -v "\.ttl$$" | wc -l); \
		if [ $$extra_files -eq 0 ]; then \
			echo "  ✓ No extra files"; \
		else \
			echo "  ⚠ Warning: Extra files found"; \
			ls $(BUILD_DIR)/$$bundle_name/; \
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
	@echo "  mutated            - Build Mutated Instruments dual oscillator synth"
	@echo "  mutated_sequences  - Build Mutated Sequences 8-step MIDI sequencer"
	@echo "  marbles            - Build Chaos Engine random modulation/gate generator"
	@echo "  install      - Build and prepare for deployment"
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

.PHONY: all x86_64 aarch64 plugins mutated mutated_sequences marbles install verify clean help
