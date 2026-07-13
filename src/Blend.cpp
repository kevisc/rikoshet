#include "plugin.hpp"
#include <algorithm>
#include <cmath>

// ============================================================================
// Rikoshet Blend — 6HP stereo A/B crossfader with input drive.
//   MIX=0 → A only, MIX=1 → B only.
//   Drive boosts both inputs through a tanh soft clip.
// ============================================================================

struct RkBlend : Module {
    enum ParamId {
        MIX_PARAM, DRIVE_PARAM, CURVE_PARAM,
        NUM_PARAMS
    };
    enum InputId {
        A_L, A_R, B_L, B_R,
        MIX_CV, DRIVE_CV,
        NUM_INPUTS
    };
    enum OutputId {
        OUT_L, OUT_R,
        NUM_OUTPUTS
    };

    RkBlend() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configParam(MIX_PARAM,   0.f, 1.f, 0.5f, "Mix (A ↔ B)", "%", 0.f, 100.f);
        configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
        configSwitch(CURVE_PARAM, 0.f, 1.f, 1.f, "Curve", {"Linear", "Equal-power"});

        configInput(A_L, "A L");
        configInput(A_R, "A R (normalled to A L)");
        configInput(B_L, "B L");
        configInput(B_R, "B R (normalled to B L)");
        configInput(MIX_CV,   "Mix CV (±5 V)");
        configInput(DRIVE_CV, "Drive CV (0–10 V)");

        configOutput(OUT_L, "Mix L");
        configOutput(OUT_R, "Mix R");

        // Bypass passes the A pair straight to the output (crossfader "off" = A).
        configBypass(A_L, OUT_L);
        configBypass(A_R, OUT_R);
    }

    void process(const ProcessArgs&) override {
        float aL = inputs[A_L].getVoltage() / 5.f;
        float aR = inputs[A_R].isConnected() ? inputs[A_R].getVoltage() / 5.f : aL;
        float bL = inputs[B_L].getVoltage() / 5.f;
        float bR = inputs[B_R].isConnected() ? inputs[B_R].getVoltage() / 5.f : bL;

        float drive = clamp(params[DRIVE_PARAM].getValue()
                            + inputs[DRIVE_CV].getVoltage() / 10.f, 0.f, 1.f);
        // Crossfade dry -> tanh-saturated by the drive amount, so the transfer
        // curve is continuous at drive = 0 (exactly transparent). A hard on/off
        // gate would step the gain and click when Drive is swept or CV-modulated
        // through zero on loud material.
        float gain = 1.f + drive * 3.f;
        aL += drive * (std::tanh(aL * gain) - aL);
        aR += drive * (std::tanh(aR * gain) - aR);
        bL += drive * (std::tanh(bL * gain) - bL);
        bR += drive * (std::tanh(bR * gain) - bR);

        float mix = clamp(params[MIX_PARAM].getValue()
                          + inputs[MIX_CV].getVoltage() / 10.f, 0.f, 1.f);
        bool eqPow = params[CURVE_PARAM].getValue() > 0.5f;

        float gA, gB;
        if (eqPow) {
            gA = std::cos(mix * (float)M_PI_2);
            gB = std::sin(mix * (float)M_PI_2);
        } else {
            gA = 1.f - mix;
            gB = mix;
        }

        outputs[OUT_L].setVoltage((aL * gA + bL * gB) * 5.f);
        outputs[OUT_R].setVoltage((aR * gA + bR * gB) * 5.f);
    }
};

struct RkBlendPanelText : Widget {
    RkBlend* module = nullptr;
    static constexpr float W = 90.f;

    RkBlendPanelText() {
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

        // MIX knob (large) at y=72; label baseline 46
        txt(W/2.f, 46, "MIX", 8.f, cLabel, C, 1.5f);

        // DRIVE knob (small) at y=130; label baseline 108
        txt(W/2.f, 108, "DRIVE", 8.f, cLabel, C, 1.5f);

        // CURVE switch (CKSS) at y=178; label baseline 160
        txt(W/2.f, 160, "CURVE", 7.f, cLabel, C, 1.f);
        txt(33, 182, "LIN", 5.5f, cSub, R, 0.f);
        txt(57, 182, "EQ",  5.5f, cSub, L, 0.f);

        // CV row (jacks at y=232); label baseline 214
        txt(W/2.f, 200, "CV", 7.f, cSub, C, 1.5f);
        txt(28, 214, "MIX",   6.5f, cSub, C, 0.8f);
        txt(62, 214, "DRV",   6.5f, cSub, C, 0.8f);

        // A pair (jacks at y=276); label baseline 258
        txt(W/2.f, 248, "A", 8.f, cLabel, C, 1.5f);
        txt(28, 258, "L", 6.5f, cSub, C, 0.8f);
        txt(62, 258, "R", 6.5f, cSub, C, 0.8f);

        // B pair (jacks at y=314); label baseline 296
        txt(W/2.f, 290, "B", 8.f, cLabel, C, 1.5f);
        txt(28, 296, "L", 6.5f, cSub, C, 0.8f);
        txt(62, 296, "R", 6.5f, cSub, C, 0.8f);

        // Output (jacks at y=350); label baseline 332
        txt(W/2.f, 328, "OUT", 7.f, cLabel, C, 1.f);
        txt(28, 336, "L", 6.5f, cSub, C, 0.8f);
        txt(62, 336, "R", 6.5f, cSub, C, 0.8f);
    }
};

struct RkBlendWidget : ModuleWidget {
    RkBlendWidget(RkBlend* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Blend.svg")));
        addChild(new RkModuleTitle("BLEND", 90.f));
        auto* labels = new RkBlendPanelText;
        labels->module = module;
        addChild(labels);

        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(75, 365)));

        // MIX knob (large)
        addParam(createParamCentered<RoundLargeBlackKnob>(
            Vec(45, 72), module, RkBlend::MIX_PARAM));

        // DRIVE knob (small)
        addParam(createParamCentered<RoundSmallBlackKnob>(
            Vec(45, 130), module, RkBlend::DRIVE_PARAM));

        // CURVE switch
        addParam(createParamCentered<CKSS>(
            Vec(45, 178), module, RkBlend::CURVE_PARAM));

        // CV jacks (y=232)
        addInput(createInputCentered<PJ301MPort>(
            Vec(28, 232), module, RkBlend::MIX_CV));
        addInput(createInputCentered<PJ301MPort>(
            Vec(62, 232), module, RkBlend::DRIVE_CV));

        // A pair (y=276)
        addInput(createInputCentered<PJ301MPort>(
            Vec(28, 276), module, RkBlend::A_L));
        addInput(createInputCentered<PJ301MPort>(
            Vec(62, 276), module, RkBlend::A_R));

        // B pair (y=314)
        addInput(createInputCentered<PJ301MPort>(
            Vec(28, 314), module, RkBlend::B_L));
        addInput(createInputCentered<PJ301MPort>(
            Vec(62, 314), module, RkBlend::B_R));

        // Output pair (y=350)
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(28, 350), module, RkBlend::OUT_L));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(62, 350), module, RkBlend::OUT_R));
    }
};

Model* modelBlend = createModel<RkBlend, RkBlendWidget>("Blend");
