#include "plugin.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// ============================================================================
// Rikoshet MultiTap — 8-step pattern delay over a tempo-synced window.
// ============================================================================

namespace {
    inline float lagrange3(const std::vector<float>& buf, double readPos) {
        const int N = (int)buf.size();
        double frac = readPos - std::floor(readPos);
        int    i1   = (int)std::floor(readPos);
        int    i0   = i1 - 1; int i2 = i1 + 1; int i3 = i1 + 2;
        auto wrap = [&](int i) {
            i %= N; if (i < 0) i += N; return i;
        };
        float y0 = buf[wrap(i0)];
        float y1 = buf[wrap(i1)];
        float y2 = buf[wrap(i2)];
        float y3 = buf[wrap(i3)];
        double f = frac;
        double c0 = -f * (f - 1.0) * (f - 2.0) / 6.0;
        double c1 =  (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
        double c2 = -(f + 1.0) * f * (f - 2.0) / 2.0;
        double c3 =  (f + 1.0) * f * (f - 1.0) / 6.0;
        return (float)(c0 * y0 + c1 * y1 + c2 * y2 + c3 * y3);
    }
}

struct RkMultiTap : Module {
    static constexpr int   kTaps = 8;
    static constexpr float kMaxDelaySec = 4.f;

    enum ParamId {
        WINDOW_PARAM,
        PAN_PARAM, DECAY_PARAM, FB_PARAM, HICUT_PARAM, MIX_PARAM,
        SYNC_PARAM, BPM_PARAM,
        TAP_PARAMS,
        NUM_PARAMS = TAP_PARAMS + kTaps
    };
    enum InputId {
        L_INPUT, R_INPUT,
        CLOCK_INPUT,
        WINDOW_CV, DECAY_CV, FB_CV, MIX_CV,
        NUM_INPUTS
    };
    enum OutputId {
        L_OUTPUT, R_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightId {
        SYNC_LIGHT,
        TAP_LIGHTS,
        NUM_LIGHTS = TAP_LIGHTS + kTaps
    };

    std::vector<float> bufL, bufR;
    int writeIdxL = 0, writeIdxR = 0;
    float lpL = 0.f, lpR = 0.f;
    float sampleRate = 44100.f;

    dsp::SchmittTrigger clockTrig;
    float clockPeriod = 0.5f;
    float clockTimeAccum = 0.f;
    float clockTimeoutAccum = 0.f;
    bool  haveClock = false;
    float syncFlash = 0.f;

    float windowPhase = 0.f;

    RkMultiTap() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(WINDOW_PARAM, 0.f, 1.f, 0.13f, "Window");
        configParam(PAN_PARAM,    0.f, 1.f, 0.7f, "Pan spread", "%", 0.f, 100.f);
        configParam(DECAY_PARAM,  0.f, 1.f, 0.3f, "Level decay", "%", 0.f, 100.f);
        configParam(FB_PARAM,     0.f, 0.9f, 0.25f, "Feedback", "%", 0.f, 100.f);
        configParam(HICUT_PARAM,  500.f, 20000.f, 8000.f, "High cut (FB path)", " Hz");
        configParam(MIX_PARAM,    0.f, 1.f, 0.4f, "Mix", "%", 0.f, 100.f);
        configSwitch(SYNC_PARAM,  0.f, 1.f, 1.f, "Time mode", {"Free (ms)", "Sync"});
        configParam(BPM_PARAM,    30.f, 300.f, 120.f, "BPM", " BPM");

        for (int i = 0; i < kTaps; ++i) {
            configParam(TAP_PARAMS + i, 0.f, 1.f, (i % 2 == 0) ? 1.f : 0.f,
                        std::string("Tap ") + std::to_string(i + 1) + " level",
                        "%", 0.f, 100.f);
        }

        configInput(L_INPUT,    "Audio L");
        configInput(R_INPUT,    "Audio R (normalled to L)");
        configInput(CLOCK_INPUT,"Clock");
        configInput(WINDOW_CV,  "Window CV (±5 V)");
        configInput(DECAY_CV,   "Decay CV (±5 V)");
        configInput(FB_CV,      "Feedback CV (±5 V)");
        configInput(MIX_CV,     "Mix CV (±5 V)");

        configOutput(L_OUTPUT,  "Audio L");
        configOutput(R_OUTPUT,  "Audio R");
    }

    void onSampleRateChange() override {
        sampleRate = APP->engine->getSampleRate();
        int n = (int)(kMaxDelaySec * sampleRate) + 8;
        bufL.assign(n, 0.f);
        bufR.assign(n, 0.f);
        writeIdxL = writeIdxR = 0;
    }

    void onReset() override {
        std::fill(bufL.begin(), bufL.end(), 0.f);
        std::fill(bufR.begin(), bufR.end(), 0.f);
        lpL = lpR = 0.f;
        haveClock = false;
        clockPeriod = 0.5f;
        windowPhase = 0.f;
    }

    static float coefFromHz(float hz, float sr) {
        return 1.f - std::exp(-2.f * (float)M_PI * std::max(1.f, hz) / sr);
    }

    void process(const ProcessArgs& args) override {
        if (bufL.empty()) onSampleRateChange();
        sampleRate = args.sampleRate;

        clockTimeAccum += args.sampleTime;
        clockTimeoutAccum += args.sampleTime;
        if (inputs[CLOCK_INPUT].isConnected()) {
            if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
                if (haveClock) clockPeriod = clamp(clockTimeAccum, 0.01f, 4.f);
                haveClock = true;
                clockTimeAccum = 0.f;
                clockTimeoutAccum = 0.f;
                syncFlash = 1.f;
            }
            if (clockTimeoutAccum > 2.f) haveClock = false;
        } else {
            haveClock = false;
        }
        syncFlash = std::max(0.f, syncFlash - args.sampleTime * 6.f);
        lights[SYNC_LIGHT].setBrightness(syncFlash);

        bool synced = params[SYNC_PARAM].getValue() > 0.5f;
        float bpm = (haveClock && synced) ? (60.f / clockPeriod)
                                          : params[BPM_PARAM].getValue();
        bpm = clamp(bpm, 10.f, 600.f);

        float wKnob = clamp(params[WINDOW_PARAM].getValue()
                            + inputs[WINDOW_CV].getVoltage() / 10.f, 0.f, 1.f);
        float windowSec;
        if (synced) {
            int idx = clamp((int)std::round(wKnob * (rk::kNumRates - 1)), 0, rk::kNumRates - 1);
            windowSec = rk::kRateBeats[idx] * 60.f / bpm;
        } else {
            windowSec = 0.05f * std::pow(80.f, wKnob);
        }
        windowSec = clamp(windowSec, 0.05f, kMaxDelaySec);
        float windowSamps = windowSec * sampleRate;

        float decay = clamp(params[DECAY_PARAM].getValue()
                            + inputs[DECAY_CV].getVoltage() / 10.f, 0.f, 1.f);
        float fb    = clamp(params[FB_PARAM].getValue()
                            + inputs[FB_CV].getVoltage() / 10.f * 0.9f, 0.f, 0.9f);
        float mix   = clamp(params[MIX_PARAM].getValue()
                            + inputs[MIX_CV].getVoltage() / 10.f, 0.f, 1.f);
        float panSpread = clamp(params[PAN_PARAM].getValue(), 0.f, 1.f);
        float lpC = coefFromHz(params[HICUT_PARAM].getValue(), sampleRate);

        windowPhase += args.sampleTime / windowSec;
        if (windowPhase >= 1.f) windowPhase -= 1.f;

        int N = (int)bufL.size();
        float wetL = 0.f, wetR = 0.f;
        for (int i = 0; i < kTaps; ++i) {
            float lvl = clamp(params[TAP_PARAMS + i].getValue(), 0.f, 1.f);
            float tapPhase01 = (float)(i + 1) / (float)kTaps;
            float diff = tapPhase01 - windowPhase;
            if (diff < 0.f) diff += 1.f;
            float ledBright = lvl * std::max(0.f, 1.f - diff * 8.f);
            lights[TAP_LIGHTS + i].setBrightness(ledBright);

            if (lvl < 0.0001f) continue;

            float envelope = 1.f - decay * tapPhase01;
            float panRaw = (i % 2 == 0) ? -panSpread : panSpread;
            float gainL = std::cos((panRaw * 0.5f + 0.5f) * (float)M_PI_2);
            float gainR = std::sin((panRaw * 0.5f + 0.5f) * (float)M_PI_2);

            float tapDelay = clamp(windowSamps * tapPhase01, 1.f,
                                   sampleRate * kMaxDelaySec - 4.f);
            double rL = (double)writeIdxL - (double)tapDelay - 1.0;
            double rR = (double)writeIdxR - (double)tapDelay - 1.0;
            while (rL < 0) rL += N;
            while (rR < 0) rR += N;

            float tL = lagrange3(bufL, rL);
            float tR = lagrange3(bufR, rR);

            float w = lvl * envelope;
            wetL += tL * w * gainL;
            wetR += tR * w * gainR;
        }

        lpL += lpC * (wetL - lpL);
        lpR += lpC * (wetR - lpR);

        float vL = inputs[L_INPUT].getVoltage() / 5.f;
        float vR = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltage() / 5.f : vL;

        bufL[writeIdxL] = vL + lpL * fb;
        bufR[writeIdxR] = vR + lpR * fb;
        writeIdxL = (writeIdxL + 1) % N;
        writeIdxR = (writeIdxR + 1) % N;

        outputs[L_OUTPUT].setVoltage((vL * (1.f - mix) + wetL * mix) * 5.f);
        outputs[R_OUTPUT].setVoltage((vR * (1.f - mix) + wetR * mix) * 5.f);
    }
};

// ----- Panel labels -----

struct RkMultiTapPanelText : Widget {
    RkMultiTap* module = nullptr;
    static constexpr float W = 300.f;

    RkMultiTapPanelText() {
        box.pos = math::Vec(0, 0);
        box.size = math::Vec(W, 380);
    }

    void draw(const DrawArgs& args) override {
        Widget::draw(args);
        NVGcontext* vg = args.vg;
        if (!APP->window->uiFont) return;
        nvgFontFaceId(vg, APP->window->uiFont->handle);

        const NVGcolor cLabel = nvgRGB(0xb4, 0xb8, 0xc0);
        const NVGcolor cSub   = nvgRGB(0x78, 0x7c, 0x84);
        const NVGcolor cFaint = nvgRGB(0x3e, 0x42, 0x4a);

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

        // === Tap pattern section ===
        txt(W/2.f, 46, "TAP PATTERN", 8.f, cLabel, C, 1.5f);
        for (int i = 0; i < RkMultiTap::kTaps; ++i) {
            float x = 30.f + i * 35.f;
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%d", i + 1);
            txt(x, 70, buf, 7.f, cSub, C, 0.f);
        }

        // === Window section ===
        txt(W/2.f, 122, "WINDOW", 8.f, cLabel, C, 1.5f);
        if (module) {
            bool synced = module->params[RkMultiTap::SYNC_PARAM].getValue() > 0.5f;
            float bpm = (module->haveClock && synced) ? (60.f / module->clockPeriod)
                                                       : module->params[RkMultiTap::BPM_PARAM].getValue();
            char buf[40];
            if (synced) {
                int idx = clamp((int)std::round(module->params[RkMultiTap::WINDOW_PARAM].getValue()
                                                * (rk::kNumRates - 1)),
                                 0, rk::kNumRates - 1);
                std::snprintf(buf, sizeof(buf), "%s", rk::kRateLabels[idx]);
            } else {
                float sec = 0.05f * std::pow(80.f, module->params[RkMultiTap::WINDOW_PARAM].getValue());
                std::snprintf(buf, sizeof(buf), "%.0f ms", sec * 1000.f);
            }
            nvgFontSize(vg, 14.f);
            nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
            nvgTextAlign(vg, C);
            nvgTextLetterSpacing(vg, 1.f);
            nvgText(vg, W/2.f, 142, buf, nullptr);

            char buf2[40];
            std::snprintf(buf2, sizeof(buf2), "%.1f BPM  %s",
                          bpm, (module->haveClock && synced) ? "CLK"
                                                              : (synced ? "KNOB" : "FREE"));
            txt(W/2.f, 160, buf2, 7.f, cSub, C, 1.f);
        }

        // === Control knob row labels (knobs at y=246) ===
        txt( 21, 224, "PAN",    7.f, cLabel, C, 1.f);
        txt( 64, 224, "DECAY",  7.f, cLabel, C, 1.f);
        txt(107, 224, "FB",     7.f, cLabel, C, 1.f);
        txt(150, 224, "HI CUT", 7.f, cLabel, C, 1.f);
        txt(193, 224, "MIX",    7.f, cLabel, C, 1.f);
        txt(236, 224, "MODE",   7.f, cLabel, C, 1.f);
        txt(224, 250, "F", 5.5f, cSub, R, 0.f);
        txt(248, 250, "S", 5.5f, cSub, L, 0.f);
        txt(279, 224, "BPM",    7.f, cLabel, C, 1.f);

        // === CV row (jacks at y=302) ===
        txt(W/2.f, 274, "CV", 7.f, cSub, C, 1.5f);
        txt( 30, 284, "WIN",    6.5f, cSub, C, 0.8f);
        txt( 90, 284, "DECAY",  6.5f, cSub, C, 0.8f);
        txt(150, 284, "FB",     6.5f, cSub, C, 0.8f);
        txt(210, 284, "MIX",    6.5f, cSub, C, 0.8f);
        txt(270, 284, "CLK",    6.5f, cSub, C, 0.8f);

        // === Audio I/O (jacks at y=344) ===
        txt(W/2.f, 316, "AUDIO", 7.f, cSub, C, 1.5f);
        txt( 60, 326, "L IN",  6.5f, cLabel, C, 0.8f);
        txt(120, 326, "R IN",  6.5f, cLabel, C, 0.8f);
        txt(180, 326, "L OUT", 6.5f, cLabel, C, 0.8f);
        txt(240, 326, "R OUT", 6.5f, cLabel, C, 0.8f);

        txt(W - 4, 374, "MULTI-TAP · RIKOSHET", 6.f, cFaint, R, 1.f);
    }
};

struct RkMultiTapWidget : ModuleWidget {
    RkMultiTapWidget(RkMultiTap* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/MultiTap.svg")));
        addChild(new RkModuleTitle("MULTITAP", 300.f));
        auto* labels = new RkMultiTapPanelText;
        labels->module = module;
        addChild(labels);

        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(285, 0)));
        addChild(createWidget<ScrewSilver>(Vec(0, 365)));
        addChild(createWidget<ScrewSilver>(Vec(285, 365)));

        // Sync LED in header strip, top-right (clear of the screw column) —
        // matches Gate and PingPong.
        addChild(createLightCentered<MediumLight<RkLight>>(
            Vec(290, 26), module, RkMultiTap::SYNC_LIGHT));

        // === Tap pattern: 8 LEDs + numbers + Trimpots ===
        for (int i = 0; i < RkMultiTap::kTaps; ++i) {
            float x = 30.f + i * 35.f;
            // Firing LED (above the number)
            addChild(createLightCentered<SmallLight<RkLight>>(
                Vec(x, 58), module, RkMultiTap::TAP_LIGHTS + i));
            // Tap level trimpot (below the number)
            addParam(createParamCentered<Trimpot>(
                Vec(x, 90), module, RkMultiTap::TAP_PARAMS + i));
        }

        // === WINDOW knob (large) ===
        addParam(createParamCentered<RoundLargeBlackKnob>(
            Vec(150, 186), module, RkMultiTap::WINDOW_PARAM));

        // === Control knob row (small knobs at y=246) ===
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec( 21, 246), module, RkMultiTap::PAN_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec( 64, 246), module, RkMultiTap::DECAY_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(107, 246), module, RkMultiTap::FB_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(150, 246), module, RkMultiTap::HICUT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(193, 246), module, RkMultiTap::MIX_PARAM));
        addParam(createParamCentered<CKSS>(
            Vec(236, 246), module, RkMultiTap::SYNC_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(279, 246), module, RkMultiTap::BPM_PARAM));

        // === CV row (jacks at y=302) ===
        addInput(createInputCentered<PJ301MPort>(
            Vec( 30, 302), module, RkMultiTap::WINDOW_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec( 90, 302), module, RkMultiTap::DECAY_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec(150, 302), module, RkMultiTap::FB_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec(210, 302), module, RkMultiTap::MIX_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec(270, 302), module, RkMultiTap::CLOCK_INPUT));

        // === Audio I/O (jacks at y=344) ===
        addInput(createInputCentered<PJ301MPort>(
            Vec( 60, 344), module, RkMultiTap::L_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec(120, 344), module, RkMultiTap::R_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(180, 344), module, RkMultiTap::L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(240, 344), module, RkMultiTap::R_OUTPUT));
    }
};

Model* modelMultiTap = createModel<RkMultiTap, RkMultiTapWidget>("MultiTap");
