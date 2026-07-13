#include "plugin.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// ============================================================================
// Rikoshet PingPong — stereo cross-fed ping-pong delay with sync.
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

struct RkPingPong : Module {
    enum ParamId {
        TIME_PARAM, FB_PARAM, MIX_PARAM,
        SPREAD_PARAM, CROSS_PARAM, LOCUT_PARAM, HICUT_PARAM,
        SYNC_PARAM, BPM_PARAM,
        NUM_PARAMS
    };
    enum InputId {
        L_INPUT, R_INPUT,
        CLOCK_INPUT,
        TIME_CV, FB_CV, MIX_CV,
        NUM_INPUTS
    };
    enum OutputId {
        L_OUTPUT, R_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightId {
        SYNC_LIGHT,
        NUM_LIGHTS
    };

    static constexpr float kMaxDelaySec = 4.f;

    std::vector<float> bufL, bufR;
    int writeIdxL = 0, writeIdxR = 0;
    float curSampL = 22050.f, curSampR = 22050.f;
    float fbStateLP_L = 0.f, fbStateLP_R = 0.f;
    float fbStateHP_L = 0.f, fbStateHP_R = 0.f;

    dsp::SchmittTrigger clockTrig;
    float clockPeriod = 0.5f;
    float clockTimeAccum = 0.f;
    float clockTimeoutAccum = 0.f;
    bool  haveClock = false;
    float syncFlash = 0.f;

    float sampleRate = 44100.f;

    RkPingPong() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(TIME_PARAM, 0.f, 1.f, 0.5f, "Time");
        configParam(FB_PARAM,   0.f, 0.95f, 0.45f, "Feedback", "%", 0.f, 100.f);
        configParam(MIX_PARAM,  0.f, 1.f, 0.4f, "Mix", "%", 0.f, 100.f);
        configParam(SPREAD_PARAM, 0.f, 1.f, 0.f, "Spread", "%", 0.f, 100.f);
        configParam(CROSS_PARAM,  0.f, 1.f, 1.f, "Cross", "%", 0.f, 100.f);
        configParam(LOCUT_PARAM,  20.f, 2000.f, 80.f, "Low cut (FB path)", " Hz");
        configParam(HICUT_PARAM,  500.f, 20000.f, 8000.f, "High cut (FB path)", " Hz");
        configSwitch(SYNC_PARAM, 0.f, 1.f, 1.f, "Time mode", {"Free (ms)", "Sync"});
        configParam(BPM_PARAM, 30.f, 300.f, 120.f, "BPM", " BPM");

        configInput(L_INPUT,    "Audio L");
        configInput(R_INPUT,    "Audio R (normalled to L)");
        configInput(CLOCK_INPUT,"Clock (quarter notes)");
        configInput(TIME_CV,    "Time CV (±5 V)");
        configInput(FB_CV,      "Feedback CV (±5 V)");
        configInput(MIX_CV,     "Mix CV (±5 V)");

        configOutput(L_OUTPUT,  "Audio L");
        configOutput(R_OUTPUT,  "Audio R");

        // Bypass passes the dry L/R through instead of muting (insert-effect convention).
        configBypass(L_INPUT, L_OUTPUT);
        configBypass(R_INPUT, R_OUTPUT);
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
        fbStateLP_L = fbStateLP_R = 0.f;
        fbStateHP_L = fbStateHP_R = 0.f;
        haveClock = false;
        clockPeriod = 0.5f;
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

        float t = clamp(params[TIME_PARAM].getValue()
                        + inputs[TIME_CV].getVoltage() / 10.f, 0.f, 1.f);

        float baseSec;
        if (synced) {
            int idx = clamp((int)std::round(t * (rk::kNumRates - 1)), 0, rk::kNumRates - 1);
            baseSec = rk::kRateBeats[idx] * 60.f / bpm;
        } else {
            baseSec = 0.001f * std::pow(4000.f, t);
        }
        baseSec = clamp(baseSec, 0.001f, kMaxDelaySec);

        float spread = clamp(params[SPREAD_PARAM].getValue(), 0.f, 1.f);
        float targetSampL = baseSec * sampleRate;
        float targetSampR = clamp(baseSec * (1.f + spread * 0.5f) * sampleRate,
                                  1.f, sampleRate * kMaxDelaySec - 4.f);

        float slew = 1.f - std::exp(-args.sampleTime / 0.030f);
        curSampL += slew * (targetSampL - curSampL);
        curSampR += slew * (targetSampR - curSampR);

        float fb    = clamp(params[FB_PARAM].getValue()
                            + inputs[FB_CV].getVoltage() / 10.f * 0.95f, 0.f, 0.95f);
        float mix   = clamp(params[MIX_PARAM].getValue()
                            + inputs[MIX_CV].getVoltage() / 10.f, 0.f, 1.f);
        float cross = clamp(params[CROSS_PARAM].getValue(), 0.f, 1.f);
        float lpC   = coefFromHz(params[HICUT_PARAM].getValue(), sampleRate);
        float hpC   = coefFromHz(params[LOCUT_PARAM].getValue(), sampleRate);

        int N = (int)bufL.size();
        double readL = (double)writeIdxL - (double)curSampL - 1.0;
        double readR = (double)writeIdxR - (double)curSampR - 1.0;
        while (readL < 0) readL += N;
        while (readR < 0) readR += N;
        float wetL = lagrange3(bufL, readL);
        float wetR = lagrange3(bufR, readR);

        fbStateLP_L += lpC * (wetL - fbStateLP_L);
        fbStateLP_R += lpC * (wetR - fbStateLP_R);
        float lpL = fbStateLP_L, lpR = fbStateLP_R;
        fbStateHP_L += hpC * (lpL - fbStateHP_L);
        fbStateHP_R += hpC * (lpR - fbStateHP_R);
        float fbL = lpL - fbStateHP_L;
        float fbR = lpR - fbStateHP_R;

        float crossL = fbL + cross * (fbR - fbL);
        float crossR = fbR + cross * (fbL - fbR);

        float vL = inputs[L_INPUT].getVoltage() / 5.f;
        float vR = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltage() / 5.f : vL;

        bufL[writeIdxL] = vL + crossL * fb;
        bufR[writeIdxR] = vR + crossR * fb;
        writeIdxL = (writeIdxL + 1) % N;
        writeIdxR = (writeIdxR + 1) % N;

        float outL = vL * (1.f - mix) + wetL * mix;
        float outR = vR * (1.f - mix) + wetR * mix;
        outputs[L_OUTPUT].setVoltage(outL * 5.f);
        outputs[R_OUTPUT].setVoltage(outR * 5.f);
    }
};

// ----- Panel labels -----

struct RkPingPongPanelText : Widget {
    RkPingPong* module = nullptr;
    static constexpr float W = 180.f;

    RkPingPongPanelText() {
        box.pos = math::Vec(0, 0);
        box.size = math::Vec(W, 380);
    }

    void draw(const DrawArgs& args) override {
        Widget::draw(args);
        NVGcontext* vg = args.vg;
        if (!APP->window->uiFont) return;
        nvgFontFaceId(vg, APP->window->uiFont->handle);

        // RIKOSHET quiet-phosphor palette.
        const NVGcolor cLabel = nvgRGB(0xb4, 0xb8, 0xc0);
        const NVGcolor cSub   = nvgRGB(0x78, 0x7c, 0x84);

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

        // === Time readout block ===
        txt(W/2.f, 44, "TIME", 8.f, cLabel, C, 1.5f);
        if (module) {
            bool synced = module->params[RkPingPong::SYNC_PARAM].getValue() > 0.5f;
            float bpm = (module->haveClock && synced) ? clamp(60.f / module->clockPeriod, 10.f, 600.f)
                                                       : module->params[RkPingPong::BPM_PARAM].getValue();
            char buf[32];
            if (synced) {
                int idx = clamp((int)std::round(module->params[RkPingPong::TIME_PARAM].getValue()
                                                * (rk::kNumRates - 1)),
                                 0, rk::kNumRates - 1);
                std::snprintf(buf, sizeof(buf), "%s", rk::kRateLabels[idx]);
            } else {
                float ms = 0.001f * std::pow(4000.f, module->params[RkPingPong::TIME_PARAM].getValue()) * 1000.f;
                std::snprintf(buf, sizeof(buf), "%.1f ms", ms);
            }
            nvgFontSize(vg, 14.f);
            nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
            nvgTextAlign(vg, C);
            nvgTextLetterSpacing(vg, 1.f);
            nvgText(vg, W/2.f, 66, buf, nullptr);

            char buf2[32];
            std::snprintf(buf2, sizeof(buf2), "%.1f BPM  %s",
                          bpm, (module->haveClock && synced) ? "CLK" : (synced ? "KNOB" : "FREE"));
            txt(W/2.f, 84, buf2, 7.f, cSub, C, 1.f);
        }

        // === Main knob row labels (knobs at y=132) ===
        txt( 30, 110, "TIME", 7.5f, cLabel, C, 1.2f);
        txt( 90, 110, "FB",   7.5f, cLabel, C, 1.2f);
        txt(150, 110, "MIX",  7.5f, cLabel, C, 1.2f);

        // === Secondary row labels (trimpots at y=186) ===
        txt( 22, 168, "SPREAD", 7.f, cLabel, C, 1.f);
        txt( 68, 168, "CROSS",  7.f, cLabel, C, 1.f);
        txt(112, 168, "LO CUT", 7.f, cLabel, C, 1.f);
        txt(158, 168, "HI CUT", 7.f, cLabel, C, 1.f);

        // === Mode/BPM row labels (controls at y=240) ===
        txt( 60, 222, "MODE", 7.f, cLabel, C, 1.f);
        txt(120, 222, "BPM",  7.f, cLabel, C, 1.f);
        // Switch legend
        txt( 48, 244, "F", 5.5f, cSub, R, 0.f);
        txt( 72, 244, "S", 5.5f, cSub, L, 0.f);

        // === CV row (jacks at y=294) ===
        txt(W/2.f, 270, "CV", 7.f, cSub, C, 1.5f);
        txt( 27, 276, "TIME", 6.5f, cSub, C, 0.8f);
        txt( 72, 276, "FB",   6.5f, cSub, C, 0.8f);
        txt(108, 276, "MIX",  6.5f, cSub, C, 0.8f);
        txt(153, 276, "CLK",  6.5f, cSub, C, 0.8f);

        // === Audio I/O row (jacks at y=336) ===
        txt(W/2.f, 312, "AUDIO", 7.f, cSub, C, 1.5f);
        txt( 27, 318, "L IN",  6.5f, cLabel, C, 0.8f);
        txt( 72, 318, "R IN",  6.5f, cLabel, C, 0.8f);
        txt(108, 318, "L OUT", 6.5f, cLabel, C, 0.8f);
        txt(153, 318, "R OUT", 6.5f, cLabel, C, 0.8f);
    }
};

struct RkPingPongWidget : ModuleWidget {
    RkPingPongWidget(RkPingPong* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/PingPong.svg")));
        addChild(new RkModuleTitle("PINGPONG", 180.f));
        auto* labels = new RkPingPongPanelText;
        labels->module = module;
        addChild(labels);

        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(165, 0)));
        addChild(createWidget<ScrewSilver>(Vec(0, 365)));
        addChild(createWidget<ScrewSilver>(Vec(165, 365)));

        // Sync LED in header strip, clear of the screw column
        addChild(createLightCentered<MediumLight<RkLight>>(
            Vec(170, 26), module, RkPingPong::SYNC_LIGHT));

        // Main knob row: TIME, FB, MIX (small knobs at y=132)
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec( 30, 132), module, RkPingPong::TIME_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec( 90, 132), module, RkPingPong::FB_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(150, 132), module, RkPingPong::MIX_PARAM));

        // Secondary trimpot row: SPREAD, CROSS, LO CUT, HI CUT (y=186)
        addParam(createParamCentered<Trimpot>(
            Vec( 22, 186), module, RkPingPong::SPREAD_PARAM));
        addParam(createParamCentered<Trimpot>(
            Vec( 68, 186), module, RkPingPong::CROSS_PARAM));
        addParam(createParamCentered<Trimpot>(
            Vec(112, 186), module, RkPingPong::LOCUT_PARAM));
        addParam(createParamCentered<Trimpot>(
            Vec(158, 186), module, RkPingPong::HICUT_PARAM));

        // MODE switch + BPM trimpot (y=240)
        addParam(createParamCentered<CKSS>(
            Vec( 60, 240), module, RkPingPong::SYNC_PARAM));
        addParam(createParamCentered<Trimpot>(
            Vec(120, 240), module, RkPingPong::BPM_PARAM));

        // CV jacks (y=294)
        addInput(createInputCentered<PJ301MPort>(
            Vec( 27, 294), module, RkPingPong::TIME_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec( 72, 294), module, RkPingPong::FB_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec(108, 294), module, RkPingPong::MIX_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec(153, 294), module, RkPingPong::CLOCK_INPUT));

        // Audio I/O (y=336)
        addInput(createInputCentered<PJ301MPort>(
            Vec( 27, 336), module, RkPingPong::L_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec( 72, 336), module, RkPingPong::R_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(108, 336), module, RkPingPong::L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(153, 336), module, RkPingPong::R_OUTPUT));
    }
};

Model* modelPingPong = createModel<RkPingPong, RkPingPongWidget>("PingPong");
