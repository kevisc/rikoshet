#pragma once
#include <rack.hpp>
#include <string>

using namespace rack;

extern Plugin* pluginInstance;
extern Model* modelGate;
extern Model* modelPingPong;
extern Model* modelMultiTap;
extern Model* modelBlend;
// Takt + Otzvuk moved to SHLabs-Mashina; no longer registered here.

// Rikoshet family light — pink, matching the panel accent (#e85e93).
template <typename TBase = GrayModuleLightWidget>
struct TRkLight : TBase {
    TRkLight() { this->addBaseColor(nvgRGB(0xe8, 0x5e, 0x93)); }
};
using RkLight = TRkLight<>;

// RIKOSHET title strip — SHLabs steel grammar, pink family accent.
// No glow halo, no bloom — just muted green text on the dark body.
struct RkModuleTitle : Widget {
    std::string text;
    float panelW;

    RkModuleTitle(const std::string& t, float w) : text(t), panelW(w) {
        box.pos = math::Vec(0, 0);
        box.size = math::Vec(w, 36);
    }

    void draw(const DrawArgs& args) override {
        Widget::draw(args);
        NVGcontext* vg = args.vg;
        if (!APP->window->uiFont) return;
        nvgFontFaceId(vg, APP->window->uiFont->handle);

        // Universal SHLabs maker's mark — bottom-centre, identical on every module.
        // Drawn before the narrow/wide branch (which returns early) so every
        // panel width carries it.
        nvgFontSize(vg, 7.f);
        nvgFillColor(vg, nvgRGB(0x6e, 0x72, 0x7c));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
        nvgTextLetterSpacing(vg, 3.f);
        nvgText(vg, panelW * 0.5f, 375.f, "SHLABS", nullptr);

        // Muted phosphor — desaturated, slightly oxidized.
        const NVGcolor cName     = nvgRGB(0xc8, 0xcc, 0xd4);   // module name
        const NVGcolor cWordmark = nvgRGB(0x88, 0x8c, 0x94);   // dim wordmark
        const NVGcolor cBar      = nvgRGB(0xe8, 0x5e, 0x93);   // accent bar

        bool narrow = panelW <= 180.f;

        if (narrow) {
            // Stacked: RIKOSHET wordmark over the module name, so the narrow
            // panels carry the family identity just like the wide ones.
            nvgFontSize(vg, 7.f);
            nvgFillColor(vg, cWordmark);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
            nvgTextLetterSpacing(vg, 2.5f);
            nvgText(vg, panelW / 2.f, 13.f, "RIKOSHET", nullptr);

            nvgBeginPath(vg);
            nvgRect(vg, panelW / 2.f - 9.f, 16.f, 18.f, 1.5f);
            nvgFillColor(vg, cBar);
            nvgFill(vg);

            nvgFontSize(vg, 12.f);
            nvgFillColor(vg, cName);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
            nvgTextLetterSpacing(vg, 2.5f);
            nvgText(vg, panelW / 2.f, 30.f, text.c_str(), nullptr);
            return;
        }

        // Wide layout: small RIKOSHET wordmark + module name.
        nvgFontSize(vg, 8.5f);
        nvgFillColor(vg, cWordmark);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgTextLetterSpacing(vg, 3.f);
        nvgText(vg, 14.f, 18.f, "RIKOSHET", nullptr);

        // Thin accent bar
        nvgBeginPath(vg);
        nvgRect(vg, 90.f, 11.f, 1.f, 14.f);
        nvgFillColor(vg, cBar);
        nvgFill(vg);

        nvgFontSize(vg, 13.f);
        nvgFillColor(vg, cName);
        nvgTextLetterSpacing(vg, 2.8f);
        nvgText(vg, 100.f, 18.f, text.c_str(), nullptr);
    }
};

// Shared sync-rate table for Gate/PingPong/MultiTap.
namespace rk {
    constexpr int kNumRates = 14;
    inline const char* const kRateLabels[kNumRates] = {
        "2 bars", "1 bar", "1/2",  "1/2D", "1/2T",
        "1/4",    "1/4D",  "1/4T", "1/8",  "1/8D",
        "1/8T",   "1/16",  "1/16T","1/32"
    };
    inline const float kRateBeats[kNumRates] = {
        8.0f, 4.0f, 2.0f, 3.0f, 2.0f * 2.0f / 3.0f,
        1.0f, 1.5f, 1.0f * 2.0f / 3.0f,
        0.5f, 0.75f, 0.5f * 2.0f / 3.0f,
        0.25f, 0.25f * 2.0f / 3.0f, 0.125f
    };
}
