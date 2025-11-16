/*
 * Mutated Instruments LV2 Plugin - Dear ImGui UI
 * 
 * Copyright (C) 2025 zynMI Project
 * Licensed under GPL v3
 */

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

#include "imgui.h"
#include "imgui_impl_opengl2.h"

#define MUTATED_UI_URI "https://github.com/PatttF/zynMI/plugins/mutated#ui"

// Surge XT-inspired color scheme
namespace Colors {
    const ImVec4 Background = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
    const ImVec4 Panel = ImVec4(0.16f, 0.16f, 0.17f, 1.0f);
    const ImVec4 PanelLight = ImVec4(0.20f, 0.20f, 0.21f, 1.0f);
    const ImVec4 Border = ImVec4(0.25f, 0.25f, 0.26f, 1.0f);
    const ImVec4 Text = ImVec4(0.85f, 0.85f, 0.86f, 1.0f);
    const ImVec4 TextDim = ImVec4(0.50f, 0.50f, 0.51f, 1.0f);
    const ImVec4 Accent = ImVec4(0.35f, 0.65f, 0.85f, 1.0f);
    const ImVec4 AccentHover = ImVec4(0.45f, 0.75f, 0.95f, 1.0f);
    const ImVec4 AccentActive = ImVec4(0.55f, 0.85f, 1.0f, 1.0f);
}

// Braids shape labels (matching TTL exactly, index -1 to 47)
const char* BRAIDS_SHAPES[] = {
    "Disabled", "CSAW", "/\\-_", "//-_", "FOLD", "uuuu", "SUB-", "SUB/", "SYN-", "SYN/",
    "//x3", "-_x3", "/\\x3", "SIx3", "RING", "////", "//uu", "TOY*", "ZLPF", "ZPKF",
    "ZBPF", "ZHPF", "VOSM", "VOWL", "VFOF", "HARM", "FM  ", "FBFM", "WTFM", "PLUK",
    "BOWD", "BLOW", "FLUT", "BELL", "DRUM", "KICK", "CYMB", "SNAR", "WTBL", "WMAP",
    "WLIN", "WTx4", "NOIS", "TWNQ", "CLKN", "CLOU", "PRTC", "QPSK", "    "
};

// Plaits engine labels (matching TTL exactly, index -1 to 15)
const char* PLAITS_ENGINES[] = {
    "Disabled", "Pair of classic waveforms", "Waveshaping oscillator", "Two operator FM",
    "Granular formant oscillator", "Harmonic oscillator", "Wavetable oscillator", "Chords",
    "Vowel and speech synthesis", "Granular cloud", "Filtered noise", "Particle noise",
    "Inharmonic string modeling", "Modal resonator", "Analog bass drum", "Analog snare drum",
    "Analog hi-hat"
};

// Modulation source labels (matching TTL exactly, index 0 to 13)
const char* MOD_SOURCES[] = {
    "None", "Braids Out", "Plaits Out", "Braids Env", "Plaits Env", "Velocity",
    "Braids Timbre", "Braids Color", "Plaits Harmonics", "Plaits Timbre", "Plaits Morph",
    "Sine", "Saw", "PWM"
};

// Modulation target labels (matching TTL exactly, index 0 to 18)
const char* MOD_TARGETS[] = {
    "None", "Braids Timbre", "Braids Color", "Braids FM", "Plaits Harmonics",
    "Plaits Timbre", "Plaits Morph", "Plaits LPG Decay", "Plaits LPG Colour", "Braids Pitch",
    "Plaits Pitch", "Braids Level", "Plaits Level", "Braids Out", "Plaits Out",
    "Reverb Time", "Reverb Mix", "Reverb Bass", "Reverb Treble"
};

// Filter type labels
const char* FILTER_TYPES[] = {
    "Disabled", "Lowpass", "Highpass", "Bandpass", "Notch", "Allpass", "Peak"
};

// Filter routing labels
const char* FILTER_ROUTINGS[] = {
    "Series", "Parallel", "Braids Only", "Plaits Only"
};

// Reverb routing labels
const char* REVERB_ROUTINGS[] = {
    "Off", "Braids", "Plaits", "Both", "Filter Out"
};

enum Tab {
    TAB_OSCILLATORS,
    TAB_MODULATION,
    TAB_FILTER1,
    TAB_FILTER2,
    TAB_REVERB
};

struct MutatedUI {
    LV2UI_Write_Function write;
    LV2UI_Controller controller;
    
    Display* display;
    Window parent;
    Window window;
    GLXContext gl_context;
    int width, height;
    
    Tab current_tab;
    float param_values[66];  // Store all parameter values
    
    ImGuiContext* imgui_context;
    bool imgui_initialized;
};

// ImGui X11 implementation helpers
static void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    style.WindowPadding = ImVec2(5, 15);
    style.FramePadding = ImVec2(5, 5);
    style.ItemSpacing = ImVec2(40, 12);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing = 25.0f;
    style.ScrollbarSize = 15.0f;
    style.GrabMinSize = 10.0f;
    
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 9.0f;
    
    style.Colors[ImGuiCol_Text] = Colors::Text;
    style.Colors[ImGuiCol_TextDisabled] = Colors::TextDim;
    style.Colors[ImGuiCol_WindowBg] = Colors::Panel;
    style.Colors[ImGuiCol_ChildBg] = Colors::Background;
    style.Colors[ImGuiCol_Border] = Colors::Border;
    style.Colors[ImGuiCol_FrameBg] = Colors::Background;
    style.Colors[ImGuiCol_FrameBgHovered] = Colors::PanelLight;
    style.Colors[ImGuiCol_FrameBgActive] = Colors::Accent;
    style.Colors[ImGuiCol_TitleBg] = Colors::Background;
    style.Colors[ImGuiCol_TitleBgActive] = Colors::Accent;
    style.Colors[ImGuiCol_SliderGrab] = Colors::Accent;
    style.Colors[ImGuiCol_SliderGrabActive] = Colors::AccentActive;
    style.Colors[ImGuiCol_Button] = Colors::Accent;
    style.Colors[ImGuiCol_ButtonHovered] = Colors::AccentHover;
    style.Colors[ImGuiCol_ButtonActive] = Colors::AccentActive;
    style.Colors[ImGuiCol_Tab] = Colors::Panel;
    style.Colors[ImGuiCol_TabHovered] = Colors::AccentHover;
    style.Colors[ImGuiCol_TabActive] = Colors::Accent;
    style.Colors[ImGuiCol_Header] = Colors::Accent;
    style.Colors[ImGuiCol_HeaderHovered] = Colors::AccentHover;
    style.Colors[ImGuiCol_HeaderActive] = Colors::AccentActive;
}

static void SendParameterToPlugin(MutatedUI* ui, uint32_t port, float value) {
    ui->param_values[port] = value;
    if (ui->write) {
        ui->write(ui->controller, port, sizeof(float), 0, &value);
    }
}

// Custom rotary knob widget
static bool DrawKnob(MutatedUI* ui, const char* label, uint32_t port, float min, float max, const char* format = "%.2f", float knob_size = 45.0f) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    
    float value = ui->param_values[port];
    float radius = knob_size * 0.5f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center = ImVec2(pos.x + radius, pos.y + radius);
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton(label, ImVec2(knob_size, knob_size + 25.0f));
    bool is_active = ImGui::IsItemActive();
    bool is_hovered = ImGui::IsItemHovered();
    bool value_changed = false;
    
    // Handle dragging
    if (is_active && io.MouseDelta.y != 0.0f) {
        float delta = -io.MouseDelta.y * 0.01f;
        value += delta * (max - min);
        if (value < min) value = min;
        if (value > max) value = max;
        ui->param_values[port] = value;
        SendParameterToPlugin(ui, port, value);
        value_changed = true;
    }
    
    // Calculate angle (-135° to +135°)
    float t = (value - min) / (max - min);
    float angle_min = -2.35619f; // -135 degrees in radians
    float angle_max = 2.35619f;  // +135 degrees in radians
    float angle = angle_min + (angle_max - angle_min) * t;
    
    // Colors
    ImU32 col_bg = ImGui::GetColorU32(is_hovered ? Colors::PanelLight : ImVec4(0.2f, 0.2f, 0.21f, 1.0f));
    ImU32 col_track = ImGui::GetColorU32(Colors::Border);
    ImU32 col_value = ImGui::GetColorU32(is_active ? Colors::AccentActive : (is_hovered ? Colors::AccentHover : Colors::Accent));
    ImU32 col_indicator = ImGui::GetColorU32(Colors::Text);
    
    // Draw knob background
    draw_list->AddCircleFilled(center, radius, col_bg, 32);
    
    // Draw arc track
    draw_list->PathArcTo(center, radius - 4.0f, angle_min, angle_max, 32);
    draw_list->PathStroke(col_track, false, 3.0f);
    
    // Draw value arc
    if (t > 0.0f) {
        draw_list->PathArcTo(center, radius - 4.0f, angle_min, angle, 32);
        draw_list->PathStroke(col_value, false, 3.0f);
    }
    
    // Draw indicator line
    float indicator_x = center.x + cosf(angle) * (radius - 8.0f);
    float indicator_y = center.y + sinf(angle) * (radius - 8.0f);
    draw_list->AddLine(center, ImVec2(indicator_x, indicator_y), col_indicator, 2.5f);
    
    // Draw center dot
    draw_list->AddCircleFilled(center, 3.0f, col_indicator);
    
    // Draw border
    draw_list->AddCircle(center, radius, col_track, 32, 1.5f);
    
    // Draw label
    char label_text[256];
    snprintf(label_text, sizeof(label_text), "%s", label);
    ImVec2 label_size = ImGui::CalcTextSize(label_text);
    ImVec2 label_pos = ImVec2(center.x - label_size.x * 0.5f, pos.y + knob_size + 5.0f);
    draw_list->AddText(label_pos, ImGui::GetColorU32(Colors::Text), label_text);
    
    // Draw value
    char value_text[64];
    snprintf(value_text, sizeof(value_text), format, value);
    ImVec2 value_size = ImGui::CalcTextSize(value_text);
    ImVec2 value_pos = ImVec2(center.x - value_size.x * 0.5f, label_pos.y + label_size.y + 2.0f);
    draw_list->AddText(value_pos, ImGui::GetColorU32(Colors::TextDim), value_text);
    
    return value_changed;
}

static void DrawCombo(MutatedUI* ui, const char* label, uint32_t port, const char* items[], int count, int offset = 0) {
    int current = (int)ui->param_values[port] - offset;
    if (current < 0) current = 0;
    if (current >= count) current = count - 1;
    
    ImGui::PushItemWidth(140);
    if (ImGui::Combo(label, &current, items, count)) {
        SendParameterToPlugin(ui, port, (float)(current + offset));
    }
    ImGui::PopItemWidth();
}

static void Separator() {
    ImGui::Spacing();
    ImGui::Spacing();
}

static void RenderOscillatorsTab(MutatedUI* ui) {
    ImGui::BeginChild("Oscillators", ImVec2(0, 0), false);
    ImGui::Indent(20.0f);
    
    // Braids section
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::AccentHover);
    ImGui::Text("BRAIDS MACRO OSCILLATOR");
    ImGui::PopStyleColor();
    ImGui::Separator();
    Separator();
    
    ImGui::Text("Shape");
    DrawCombo(ui, "##Shape", 2, BRAIDS_SHAPES, 49, -1);
    
    Separator();
    
    DrawKnob(ui, "Level", 1, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Coarse", 3, -24.0f, 24.0f, "%.0f");
    ImGui::SameLine();
    DrawKnob(ui, "Fine", 4, -1.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "FM", 5, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Timbre", 6, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Color", 7, 0.0f, 1.0f);
    
    DrawKnob(ui, "Attack", 8, 0.001f, 2.0f, "%.3fs");
    ImGui::SameLine();
    DrawKnob(ui, "Decay", 9, 0.001f, 2.0f, "%.3fs");
    ImGui::SameLine();
    DrawKnob(ui, "Sustain", 10, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Release", 11, 0.001f, 5.0f, "%.3fs");
    ImGui::SameLine();
    DrawKnob(ui, "Pan", 45, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Glide", 46, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Detune", 49, -2.0f, 2.0f);
    
    Separator();
    ImGui::Separator();
    Separator();
    
    // Plaits section
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::AccentHover);
    ImGui::Text("PLAITS MACRO OSCILLATOR");
    ImGui::PopStyleColor();
    ImGui::Separator();
    Separator();
    
    ImGui::Text("Engine");
    DrawCombo(ui, "##Engine", 13, PLAITS_ENGINES, 17, -1);
    
    Separator();
    
    DrawKnob(ui, "Level P", 12, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Coarse P", 14, -24.0f, 24.0f, "%.0f");
    ImGui::SameLine();
    DrawKnob(ui, "Fine P", 15, -1.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Harmonics", 16, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Timbre P", 17, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Morph", 18, 0.0f, 1.0f);
    
    DrawKnob(ui, "LPG Decay", 19, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "LPG Color", 20, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Attack P", 21, 0.001f, 2.0f, "%.3fs");
    ImGui::SameLine();
    DrawKnob(ui, "Decay P", 22, 0.001f, 2.0f, "%.3fs");
    ImGui::SameLine();
    DrawKnob(ui, "Sustain P", 23, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Release P", 24, 0.001f, 5.0f, "%.3fs");
    ImGui::SameLine();
    DrawKnob(ui, "Pan P", 47, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Glide P", 48, 0.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Detune P", 50, -2.0f, 2.0f);
    
    ImGui::Unindent(20.0f);
    ImGui::EndChild();
}

static void RenderModulationTab(MutatedUI* ui) {
    ImGui::BeginChild("Modulation", ImVec2(0, 0), false);
    ImGui::Indent(20.0f);
    
    // Mod slot 1
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::AccentHover);
    ImGui::Text("MODULATION SLOT 1");
    ImGui::PopStyleColor();
    ImGui::Separator();
    Separator();
    
    ImGui::Text("Source");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(80, 0));
    ImGui::SameLine();
    ImGui::Text("Target");
    
    DrawCombo(ui, "##Source1", 25, MOD_SOURCES, 14);
    ImGui::SameLine();
    DrawCombo(ui, "##Target1", 26, MOD_TARGETS, 19);
    ImGui::SameLine();
    DrawKnob(ui, "Amount 1", 27, -1.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Detune 1", 28, -2.0f, 2.0f);
    
    Separator();
    ImGui::Separator();
    Separator();
    
    // Mod slot 2
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::AccentHover);
    ImGui::Text("MODULATION SLOT 2");
    ImGui::PopStyleColor();
    ImGui::Separator();
    Separator();
    
    ImGui::Text("Source");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(80, 0));
    ImGui::SameLine();
    ImGui::Text("Target");
    
    DrawCombo(ui, "##Source2", 29, MOD_SOURCES, 14);
    ImGui::SameLine();
    DrawCombo(ui, "##Target2", 30, MOD_TARGETS, 19);
    ImGui::SameLine();
    DrawKnob(ui, "Amount 2", 31, -1.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Detune 2", 32, -2.0f, 2.0f);
    
    Separator();
    ImGui::Separator();
    Separator();
    
    // Mod slot 3
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::AccentHover);
    ImGui::Text("MODULATION SLOT 3");
    ImGui::PopStyleColor();
    ImGui::Separator();
    Separator();
    
    ImGui::Text("Source");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(80, 0));
    ImGui::SameLine();
    ImGui::Text("Target");
    
    DrawCombo(ui, "##Source3", 33, MOD_SOURCES, 14);
    ImGui::SameLine();
    DrawCombo(ui, "##Target3", 34, MOD_TARGETS, 19);
    ImGui::SameLine();
    DrawKnob(ui, "Amount 3", 35, -1.0f, 1.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Detune 3", 36, -2.0f, 2.0f);
    
    ImGui::Unindent(20.0f);
    ImGui::EndChild();
}

static void RenderFiltersTab(MutatedUI* ui) {
    ImGui::BeginChild("Filters", ImVec2(0, 0), false);
    ImGui::Indent(20.0f);
    
    // Filter 1
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::AccentHover);
    ImGui::Text("FILTER 1");
    ImGui::PopStyleColor();
    ImGui::Separator();
    Separator();
    
    ImGui::Text("Type");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(90, 0));
    ImGui::SameLine();
    ImGui::Text("Routing");
    
    DrawCombo(ui, "##Type1", 37, FILTER_TYPES, 7);
    ImGui::SameLine();
    DrawCombo(ui, "##Routing1", 38, FILTER_ROUTINGS, 4);
    ImGui::SameLine();
    DrawKnob(ui, "Cutoff", 39, 20.0f, 20000.0f, "%.0fHz", 50.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Resonance", 40, 0.0f, 1.0f, "%.2f", 50.0f);
    
    Separator();
    ImGui::Separator();
    Separator();
    
    // Filter 2
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::AccentHover);
    ImGui::Text("FILTER 2");
    ImGui::PopStyleColor();
    ImGui::Separator();
    Separator();
    
    ImGui::Text("Type");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(90, 0));
    ImGui::SameLine();
    ImGui::Text("Routing");
    
    DrawCombo(ui, "##Type2", 41, FILTER_TYPES, 7);
    ImGui::SameLine();
    DrawCombo(ui, "##Routing2", 42, FILTER_ROUTINGS, 4);
    ImGui::SameLine();
    DrawKnob(ui, "Cutoff F2", 43, 20.0f, 20000.0f, "%.0fHz", 50.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Resonance F2", 44, 0.0f, 1.0f, "%.2f", 50.0f);
    
    ImGui::Unindent(20.0f);
    ImGui::EndChild();
}

static void RenderReverbTab(MutatedUI* ui) {
    ImGui::BeginChild("Reverb", ImVec2(0, 0), false);
    ImGui::Indent(20.0f);
    
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::AccentHover);
    ImGui::Text("NEPENTHE REVERB");
    ImGui::PopStyleColor();
    ImGui::Separator();
    Separator();
    
    DrawKnob(ui, "Time", 52, 0.5f, 8.0f, "%.1fs", 48.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Mix", 56, 0.0f, 1.0f, "%.2f", 48.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Gain", 61, 0.0f, 4.0f, "%.2f", 48.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Size", 57, 0.0f, 1.0f, "%.2f", 48.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Diffusion", 58, 0.0f, 1.0f, "%.2f", 48.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Modulation", 59, 0.0f, 1.0f, "%.2f", 48.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Width", 60, 0.0f, 1.0f, "%.2f", 48.0f);
    
    DrawKnob(ui, "Pre-Delay", 53, 0.0f, 200.0f, "%.0fms", 48.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Bass", 54, -1.0f, 1.0f, "%.2f", 48.0f);
    ImGui::SameLine();
    DrawKnob(ui, "Treble", 55, 0.0f, 1.0f, "%.2f", 48.0f);
    
    Separator();
    ImGui::Text("Routing");
    DrawCombo(ui, "##Routing", 51, REVERB_ROUTINGS, 5);
    
    ImGui::Unindent(20.0f);
    ImGui::EndChild();
}

static void Render(MutatedUI* ui) {
    // Start ImGui frame
    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();
    
    // Main window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(ui->width, ui->height));
    ImGui::Begin("Mutated Instruments", nullptr, 
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    
    // Tab bar
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Oscillators")) {
            ui->current_tab = TAB_OSCILLATORS;
            RenderOscillatorsTab(ui);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Modulation")) {
            ui->current_tab = TAB_MODULATION;
            RenderModulationTab(ui);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Filters")) {
            ui->current_tab = TAB_FILTER1;
            RenderFiltersTab(ui);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Reverb")) {
            ui->current_tab = TAB_REVERB;
            RenderReverbTab(ui);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    
    ImGui::End();
    
    // Render
    ImGui::Render();
    glViewport(0, 0, ui->width, ui->height);
    glClearColor(Colors::Background.x, Colors::Background.y, Colors::Background.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

// X11 event handling for ImGui
static void HandleX11Event(MutatedUI* ui, XEvent* event) {
    ImGuiIO& io = ImGui::GetIO();
    
    switch (event->type) {
        case ButtonPress:
        case ButtonRelease: {
            bool down = (event->type == ButtonPress);
            if (event->xbutton.button >= 1 && event->xbutton.button <= 3) {
                io.AddMouseButtonEvent(event->xbutton.button - 1, down);
            }
            break;
        }
        case MotionNotify:
            io.AddMousePosEvent(event->xmotion.x, event->xmotion.y);
            break;
        case KeyPress:
        case KeyRelease: {
            bool down = (event->type == KeyPress);
            KeySym keysym = XLookupKeysym(&event->xkey, 0);
            
            // Map common keys
            if (keysym == XK_Tab) io.AddKeyEvent(ImGuiKey_Tab, down);
            else if (keysym == XK_Left) io.AddKeyEvent(ImGuiKey_LeftArrow, down);
            else if (keysym == XK_Right) io.AddKeyEvent(ImGuiKey_RightArrow, down);
            else if (keysym == XK_Up) io.AddKeyEvent(ImGuiKey_UpArrow, down);
            else if (keysym == XK_Down) io.AddKeyEvent(ImGuiKey_DownArrow, down);
            else if (keysym == XK_Return) io.AddKeyEvent(ImGuiKey_Enter, down);
            else if (keysym == XK_Escape) io.AddKeyEvent(ImGuiKey_Escape, down);
            break;
        }
    }
}

// LV2 UI interface
static LV2UI_Handle
instantiate(const LV2UI_Descriptor* descriptor,
            const char* plugin_uri,
            const char* bundle_path,
            LV2UI_Write_Function write_function,
            LV2UI_Controller controller,
            LV2UI_Widget* widget,
            const LV2_Feature* const* features)
{
    fprintf(stderr, "Mutated UI: instantiate called\n");
    
    MutatedUI* ui = new MutatedUI();
    ui->write = write_function;
    ui->controller = controller;
    ui->width = 840;
    ui->height = 850;
    ui->current_tab = TAB_OSCILLATORS;
    ui->display = nullptr;
    ui->parent = 0;
    ui->window = 0;
    ui->gl_context = nullptr;
    ui->imgui_context = nullptr;
    ui->imgui_initialized = false;
    
    // Initialize parameter values with defaults from TTL
    memset(ui->param_values, 0, sizeof(ui->param_values));
    
    // Braids defaults
    ui->param_values[1] = 0.5f;    // Level
    ui->param_values[2] = 0.0f;    // Shape
    ui->param_values[3] = 0.0f;    // Coarse
    ui->param_values[4] = 0.0f;    // Fine
    ui->param_values[5] = 0.0f;    // FM
    ui->param_values[6] = 0.5f;    // Timbre
    ui->param_values[7] = 0.5f;    // Color
    ui->param_values[8] = 0.1f;    // Attack
    ui->param_values[9] = 0.3f;    // Decay
    ui->param_values[10] = 0.7f;   // Sustain
    ui->param_values[11] = 0.5f;   // Release
    ui->param_values[45] = 0.5f;   // Pan
    ui->param_values[46] = 0.0f;   // Glide
    ui->param_values[49] = 0.0f;   // Detune
    
    // Plaits defaults
    ui->param_values[12] = 0.5f;   // Level
    ui->param_values[13] = 0.0f;   // Engine
    ui->param_values[14] = 0.0f;   // Coarse
    ui->param_values[15] = 0.0f;   // Fine
    ui->param_values[16] = 0.5f;   // Harmonics
    ui->param_values[17] = 0.5f;   // Timbre
    ui->param_values[18] = 0.5f;   // Morph
    ui->param_values[19] = 0.5f;   // LPG Decay
    ui->param_values[20] = 0.5f;   // LPG Color
    ui->param_values[21] = 0.1f;   // Attack
    ui->param_values[22] = 0.3f;   // Decay
    ui->param_values[23] = 0.5f;   // Sustain
    ui->param_values[24] = 0.5f;   // Release
    ui->param_values[47] = 0.5f;   // Pan
    ui->param_values[48] = 0.0f;   // Glide
    ui->param_values[50] = 0.0f;   // Detune
    
    // Modulation defaults (all 0)
    // Filter 1 defaults
    ui->param_values[37] = 0.0f;   // Type
    ui->param_values[38] = 2.0f;   // Routing
    ui->param_values[39] = 1.0f;   // Cutoff
    ui->param_values[40] = 0.0f;   // Resonance
    
    // Filter 2 defaults
    ui->param_values[41] = 0.0f;   // Type
    ui->param_values[42] = 2.0f;   // Routing
    ui->param_values[43] = 1.0f;   // Cutoff
    ui->param_values[44] = 0.0f;   // Resonance
    
    // Reverb defaults
    ui->param_values[51] = 0.0f;   // Routing
    ui->param_values[52] = 2.2f;   // Time
    ui->param_values[53] = 30.0f;  // Pre-Delay
    ui->param_values[54] = 0.0f;   // Bass
    ui->param_values[55] = 0.5f;   // Treble
    ui->param_values[56] = 0.3f;   // Mix
    ui->param_values[57] = 0.5f;   // Size
    ui->param_values[58] = 0.7f;   // Diffusion
    ui->param_values[59] = 0.5f;   // Modulation
    ui->param_values[60] = 1.0f;   // Width
    ui->param_values[61] = 1.5f;   // Gain
    
    // Find parent window from features
    for (int i = 0; features[i]; i++) {
        if (!strcmp(features[i]->URI, LV2_UI__parent)) {
            ui->parent = (Window)features[i]->data;
            fprintf(stderr, "Mutated UI: parent window = %lu\n", (unsigned long)ui->parent);
        }
    }
    
    if (!ui->parent) {
        fprintf(stderr, "Mutated UI: ERROR - no parent window found!\n");
        delete ui;
        return nullptr;
    }
    
    // Open X11 display
    ui->display = XOpenDisplay(nullptr);
    if (!ui->display) {
        fprintf(stderr, "Mutated UI: ERROR - failed to open X11 display\n");
        delete ui;
        return nullptr;
    }
    
    // Create X11 window as child of parent
    XSetWindowAttributes attr;
    attr.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | 
                     PointerMotionMask | KeyPressMask | KeyReleaseMask;
    attr.background_pixel = 0;
    attr.border_pixel = 0;
    
    ui->window = XCreateWindow(
        ui->display, ui->parent,
        0, 0, ui->width, ui->height,
        0, CopyFromParent, InputOutput, CopyFromParent,
        CWEventMask | CWBackPixel | CWBorderPixel, &attr
    );
    
    if (!ui->window) {
        fprintf(stderr, "Mutated UI: ERROR - failed to create X11 window\n");
        XCloseDisplay(ui->display);
        delete ui;
        return nullptr;
    }
    
    // Create OpenGL context
    int visual_attribs[] = {
        GLX_RGBA,
        GLX_DEPTH_SIZE, 24,
        GLX_DOUBLEBUFFER,
        None
    };
    
    XVisualInfo* vi = glXChooseVisual(ui->display, DefaultScreen(ui->display), visual_attribs);
    if (!vi) {
        XDestroyWindow(ui->display, ui->window);
        XCloseDisplay(ui->display);
        delete ui;
        return nullptr;
    }
    
    ui->gl_context = glXCreateContext(ui->display, vi, nullptr, GL_TRUE);
    XFree(vi);
    
    if (!ui->gl_context) {
        XDestroyWindow(ui->display, ui->window);
        XCloseDisplay(ui->display);
        delete ui;
        return nullptr;
    }
    
    glXMakeCurrent(ui->display, ui->window, ui->gl_context);
    XMapWindow(ui->display, ui->window);
    XFlush(ui->display);
    
    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ui->imgui_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(ui->imgui_context);
    
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(ui->width, ui->height);
    io.IniFilename = nullptr;  // Disable imgui.ini
    
    SetupImGuiStyle();
    ImGui_ImplOpenGL2_Init();
    
    ui->imgui_initialized = true;
    
    fprintf(stderr, "Mutated UI: ImGui initialized successfully\n");
    
    *widget = (LV2UI_Widget)(uintptr_t)ui->window;
    return ui;
}

static void
cleanup(LV2UI_Handle handle)
{
    MutatedUI* ui = (MutatedUI*)handle;
    
    if (ui->imgui_initialized) {
        ImGui::SetCurrentContext(ui->imgui_context);
        ImGui_ImplOpenGL2_Shutdown();
        ImGui::DestroyContext(ui->imgui_context);
    }
    
    if (ui->gl_context) {
        glXMakeCurrent(ui->display, None, nullptr);
        glXDestroyContext(ui->display, ui->gl_context);
    }
    if (ui->window) {
        XDestroyWindow(ui->display, ui->window);
    }
    if (ui->display) {
        XCloseDisplay(ui->display);
    }
    delete ui;
}

static void
port_event(LV2UI_Handle handle,
           uint32_t port_index,
           uint32_t buffer_size,
           uint32_t format,
           const void* buffer)
{
    MutatedUI* ui = (MutatedUI*)handle;
    
    if (format == 0 && port_index < 66) {  // Float
        ui->param_values[port_index] = *(const float*)buffer;
    }
}

static int
idle(LV2UI_Handle handle)
{
    MutatedUI* ui = (MutatedUI*)handle;
    
    if (!ui->imgui_initialized) return 0;
    
    ImGui::SetCurrentContext(ui->imgui_context);
    
    // Process X11 events
    while (XPending(ui->display)) {
        XEvent event;
        XNextEvent(ui->display, &event);
        HandleX11Event(ui, &event);
    }
    
    // Render
    glXMakeCurrent(ui->display, ui->window, ui->gl_context);
    Render(ui);
    glXSwapBuffers(ui->display, ui->window);
    
    return 0;
}

static const LV2UI_Idle_Interface idle_interface = {idle};

static const void*
extension_data(const char* uri)
{
    if (!strcmp(uri, LV2_UI__idleInterface)) {
        return &idle_interface;
    }
    return nullptr;
}

static const LV2UI_Descriptor descriptor = {
    MUTATED_UI_URI,
    instantiate,
    cleanup,
    port_event,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}
