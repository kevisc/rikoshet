#include "plugin.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

// ============================================================================
// Rikoshet Gate — tempo-synced rhythmic amplitude gate.
//
//   Phase accumulator clocked either by the BPM knob (free) or by counting
//   beats off a CLOCK input (tap-period estimation).
//
//   Modulator shape morphs hard square (PW duty) to raised cosine; an
//   exponential slew on the output envelope keeps edges click-free.
// ============================================================================

struct RkGate : Module {
    enum ParamId {
        RATE_PARAM, BPM_PARAM,
        SHAPE_PARAM, DEPTH_PARAM, PW_PARAM, STEREO_PARAM,
        FREERUN_PARAM,
        NUM_PARAMS
    };
    enum InputId {
        L_INPUT, R_INPUT,
        CLOCK_INPUT,
        RATE_CV, SHAPE_CV, DEPTH_CV, PW_CV,
        NUM_INPUTS
    };
    enum OutputId {
        L_OUTPUT, R_OUTPUT,
        GATE_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightId {
        GATE_LIGHT,
        NUM_LIGHTS
    };

    double phaseL = 0.0;
    double phaseR = 0.0;
    float  smoothedL = 1.f;
    float  smoothedR = 1.f;

    dsp::SchmittTrigger clockTrig;
    float clockPeriod = 0.5f;
    float clockTimeAccum = 0.f;
    float clockTimeoutAccum = 0.f;
    bool  haveClock = false;

    RkGate() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(RATE_PARAM,   0.f, (float)(rk::kNumRates - 1), 8.f, "Rate");
        paramQuantities[RATE_PARAM]->snapEnabled = true;
        configParam(BPM_PARAM,    30.f, 300.f, 120.f, "BPM", " BPM");
        configParam(SHAPE_PARAM,  0.f, 1.f, 0.f, "Shape (square → sine)", "%", 0.f, 100.f);
        configParam(DEPTH_PARAM,  0.f, 1.f, 1.f, "Depth", "%", 0.f, 100.f);
        configParam(PW_PARAM,     0.05f, 0.95f, 0.5f, "Pulse width", "%", 0.f, 100.f);
        configParam(STEREO_PARAM, 0.f, 1.f, 0.f, "Stereo offset", "%", 0.f, 100.f);
        configSwitch(FREERUN_PARAM, 0.f, 1.f, 0.f, "Free run", {"Sync", "Free"});

        configInput(L_INPUT,   "Audio L");
        configInput(R_INPUT,   "Audio R (normalled to L)");
        configInput(CLOCK_INPUT,"Clock (assumes quarter-note pulses)");
        configInput(RATE_CV,   "Rate CV (±10 V = full range)");
        configInput(SHAPE_CV,  "Shape CV (±5 V)");
        configInput(DEPTH_CV,  "Depth CV (±5 V)");
        configInput(PW_CV,     "PW CV (±5 V)");

        configOutput(L_OUTPUT,    "Audio L");
        configOutput(R_OUTPUT,    "Audio R");
        configOutput(GATE_OUTPUT, "Gate envelope (0–10 V)");

        // Bypass passes the dry L/R through instead of muting (insert-effect convention).
        configBypass(L_INPUT, L_OUTPUT);
        configBypass(R_INPUT, R_OUTPUT);
    }

    void onReset() override {
        phaseL = phaseR = 0.0;
        smoothedL = smoothedR = 1.f;
        haveClock = false;
        clockPeriod = 0.5f;
    }

    static float gateShape(float phase01, float pw, float shape) {
        float square = (phase01 < pw) ? 1.f : 0.f;
        float sine   = 0.5f - 0.5f * std::cos(phase01 * 2.f * (float)M_PI);
        return square + (sine - square) * shape;
    }

    void process(const ProcessArgs& args) override {
        bool freeRun = params[FREERUN_PARAM].getValue() > 0.5f;
        clockTimeAccum += args.sampleTime;
        clockTimeoutAccum += args.sampleTime;
        if (inputs[CLOCK_INPUT].isConnected()) {
            if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
                if (haveClock) clockPeriod = clamp(clockTimeAccum, 0.01f, 4.f);
                haveClock = true;
                clockTimeAccum = 0.f;
                clockTimeoutAccum = 0.f;
            }
            if (clockTimeoutAccum > 2.f) haveClock = false;
        } else {
            haveClock = false;
        }

        float bpm = (!freeRun && haveClock) ? (60.f / clockPeriod)
                                            : params[BPM_PARAM].getValue();
        bpm = clamp(bpm, 10.f, 600.f);

        int rateIdx = (int)std::round(params[RATE_PARAM].getValue());
        if (inputs[RATE_CV].isConnected()) {
            rateIdx += (int)std::round(inputs[RATE_CV].getVoltage() * (rk::kNumRates - 1) / 10.f);
        }
        rateIdx = clamp(rateIdx, 0, rk::kNumRates - 1);

        float shape = clamp(params[SHAPE_PARAM].getValue() + inputs[SHAPE_CV].getVoltage() / 10.f, 0.f, 1.f);
        float depth = clamp(params[DEPTH_PARAM].getValue() + inputs[DEPTH_CV].getVoltage() / 10.f, 0.f, 1.f);
        float pw    = clamp(params[PW_PARAM].getValue()    + inputs[PW_CV].getVoltage() / 10.f, 0.05f, 0.95f);
        float stereo = clamp(params[STEREO_PARAM].getValue(), 0.f, 1.f);

        float beatsPerCycle = rk::kRateBeats[rateIdx];
        float cycleSeconds  = beatsPerCycle * 60.f / bpm;
        double phaseInc = 1.0 / ((double)cycleSeconds * (double)args.sampleRate);

        phaseL += phaseInc;
        if (phaseL >= 1.0) phaseL -= 1.0;
        phaseR = phaseL + (double)stereo;
        if (phaseR >= 1.0) phaseR -= 1.0;

        float gL = gateShape((float)phaseL, pw, shape);
        float gR = gateShape((float)phaseR, pw, shape);

        float floorAmp = 1.f - depth;
        float targetL = floorAmp + (1.f - floorAmp) * gL;
        float targetR = floorAmp + (1.f - floorAmp) * gR;

        float slew = 1.f - std::exp(-args.sampleTime / 0.002f);
        smoothedL += slew * (targetL - smoothedL);
        smoothedR += slew * (targetR - smoothedR);

        float inL = inputs[L_INPUT].getVoltage();
        float inR = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltage() : inL;
        outputs[L_OUTPUT].setVoltage(inL * smoothedL);
        outputs[R_OUTPUT].setVoltage(inR * smoothedR);
        outputs[GATE_OUTPUT].setVoltage(smoothedL * 10.f);

        lights[GATE_LIGHT].setSmoothBrightness(smoothedL, args.sampleTime);
    }
};

// ----- Panel labels -----
// Layout grammar: label baseline → knob center distance is at least
//   radius + 8 px, giving roughly 14 px from text ascender to knob outline.

struct RkGatePanelText : Widget {
    RkGate* module = nullptr;
    static constexpr float W = 150.f;

    RkGatePanelText() {
        box.pos = math::Vec(0, 0);
        box.size = math::Vec(W, 380);
    }

    void draw(const DrawArgs& args) override {
        Widget::draw(args);
        NVGcontext* vg = args.vg;
        if (!APP->window->uiFont) return;
        nvgFontFaceId(vg, APP->window->uiFont->handle);

        // RIKOSHET quiet-phosphor palette — desaturated, low-key.
        const NVGcolor cLabel = nvgRGB(0xb4, 0xb8, 0xc0);   // muted phosphor main
        const NVGcolor cSub   = nvgRGB(0x78, 0x7c, 0x84);   // dim phosphor

        auto txt = [&](float x, float y, const char* s, float sz, NVGcolor col,
                       int align, float spacing) {
            nvgFontSize(vg, sz);
            nvgTextAlign(vg, align);
            nvgFillColor(vg, col);
            nvgTextLetterSpacing(vg, spacing);
            nvgText(vg, x, y, s, nullptr);
        };
        const int C = NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE;
        const int L = NVG_ALIGN_LEFT   | NVG_ALIGN_BASELINE;
        const int R = NVG_ALIGN_RIGHT  | NVG_ALIGN_BASELINE;

        // === Rate readout ===
        txt(W/2.f, 44, "RATE", 8.f, cLabel, C, 1.5f);
        if (module) {
            int idx = clamp((int)std::round(module->params[RkGate::RATE_PARAM].getValue()),
                            0, rk::kNumRates - 1);
            nvgFontSize(vg, 14.f);
            nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
            nvgTextAlign(vg, C);
            nvgTextLetterSpacing(vg, 1.f);
            nvgText(vg, W/2.f, 66, rk::kRateLabels[idx], nullptr);
        }

        // BPM / source status (between readout and rate knob)
        if (module) {
            char buf[32];
            float bpm;
            const char* src;
            bool freeRun = module->params[RkGate::FREERUN_PARAM].getValue() > 0.5f;
            if (!freeRun && module->haveClock) {
                bpm = clamp(60.f / module->clockPeriod, 10.f, 600.f);
                src = "CLK";
            } else {
                bpm = module->params[RkGate::BPM_PARAM].getValue();
                src = freeRun ? "FREE" : "KNOB";
            }
            std::snprintf(buf, sizeof(buf), "%.1f BPM  %s", bpm, src);
            txt(W/2.f, 84, buf, 7.f, cSub, C, 1.f);
        }

        // === Modulation knob row labels (knobs at y=170) ===
        txt( 30, 148, "SHAPE", 7.5f, cLabel, C, 1.2f);
        txt( 75, 148, "DEPTH", 7.5f, cLabel, C, 1.2f);
        txt(120, 148, "PW",    7.5f, cLabel, C, 1.2f);

        // === Trimpot row labels (trimpots at y=226) ===
        txt( 30, 208, "STEREO", 7.f, cLabel, C, 1.f);
        txt( 75, 208, "BPM",    7.f, cLabel, C, 1.f);
        txt(120, 208, "MODE",   7.f, cLabel, C, 1.f);
        // EXP/LIN-style mini-legend for the MODE switch
        txt(108, 230, "S", 5.5f, cSub, R, 0.f);
        txt(132, 230, "F", 5.5f, cSub, L, 0.f);

        // === CV row (jacks at y=282) ===
        txt(W/2.f, 254, "CV", 7.f, cSub, C, 1.5f);
        txt( 22, 264, "RATE",  6.5f, cSub, C, 0.8f);
        txt( 50, 264, "SHAPE", 6.5f, cSub, C, 0.8f);
        txt( 78, 264, "DEPTH", 6.5f, cSub, C, 0.8f);
        txt(106, 264, "PW",    6.5f, cSub, C, 0.8f);
        txt(134, 264, "CLK",   6.5f, cSub, C, 0.8f);

        // === Audio I/O row (jacks at y=334) ===
        txt(W/2.f, 304, "AUDIO", 7.f, cSub, C, 1.5f);
        txt( 22, 316, "L IN",  6.5f, cLabel, C, 0.8f);
        txt( 50, 316, "R IN",  6.5f, cLabel, C, 0.8f);
        txt( 78, 316, "L OUT", 6.5f, cLabel, C, 0.8f);
        txt(106, 316, "R OUT", 6.5f, cLabel, C, 0.8f);
        txt(134, 316, "GATE",  6.5f, cLabel, C, 0.8f);
    }
};

// ----- Widget -----

struct RkGateWidget : ModuleWidget {
    RkGateWidget(RkGate* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Gate.svg")));
        addChild(new RkModuleTitle("GATE", 150.f));
        auto* labels = new RkGatePanelText;
        labels->module = module;
        addChild(labels);

        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(135, 0)));
        addChild(createWidget<ScrewSilver>(Vec(0, 365)));
        addChild(createWidget<ScrewSilver>(Vec(135, 365)));

        // RATE knob (large) — readout backplate is ABOVE it now, no overlap
        addParam(createParamCentered<RoundLargeBlackKnob>(
            Vec(75, 110), module, RkGate::RATE_PARAM));

        // Gate-state LED (header strip, clear of the screw column)
        addChild(createLightCentered<MediumLight<RkLight>>(
            Vec(140, 26), module, RkGate::GATE_LIGHT));

        // Modulation knob row: SHAPE, DEPTH, PW (y=170)
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec( 30, 170), module, RkGate::SHAPE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec( 75, 170), module, RkGate::DEPTH_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(120, 170), module, RkGate::PW_PARAM));

        // Trimpot row: STEREO, BPM, MODE (y=226)
        addParam(createParamCentered<Trimpot>(
            Vec( 30, 226), module, RkGate::STEREO_PARAM));
        addParam(createParamCentered<Trimpot>(
            Vec( 75, 226), module, RkGate::BPM_PARAM));
        addParam(createParamCentered<CKSS>(
            Vec(120, 226), module, RkGate::FREERUN_PARAM));

        // CV jacks (y=282)
        addInput(createInputCentered<PJ301MPort>(
            Vec( 22, 282), module, RkGate::RATE_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec( 50, 282), module, RkGate::SHAPE_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec( 78, 282), module, RkGate::DEPTH_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec(106, 282), module, RkGate::PW_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec(134, 282), module, RkGate::CLOCK_INPUT));

        // Audio I/O (y=334)
        addInput(createInputCentered<PJ301MPort>(
            Vec( 22, 334), module, RkGate::L_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec( 50, 334), module, RkGate::R_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec( 78, 334), module, RkGate::L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(106, 334), module, RkGate::R_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(134, 334), module, RkGate::GATE_OUTPUT));
    }
};

Model* modelGate = createModel<RkGate, RkGateWidget>("Gate");
