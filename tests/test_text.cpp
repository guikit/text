#include "guikit/text.hpp"
#include "guikit/core/assets.hpp"
#include "keel/keel.h"
#include "keel/math.h"

#if defined(FT_FREETYPE_H) || defined(FT2BUILD_H_)
#error "guikit/text.hpp must not leak FreeType headers"
#endif

static keel::i32 g_failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            keel::eprintln("[FAIL] {} ({}:{})", #cond, __FILE__, __LINE__);          \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        if (keel::abs((a) - (b)) > (eps)) {                                           \
            keel::eprintln("[FAIL] {} ({}) != {} ({}) [{}:{}]",                      \
                           #a, (a), #b, (b), __FILE__, __LINE__);                    \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

int main() {
    keel::println("[RUN] Testing GuiKit Text module (backend: {})...", guikit::TextEngine::BackendName());
#if defined(GUIKIT_HAS_TEXT_ENGINE) && GUIKIT_HAS_TEXT_ENGINE
    using namespace guikit;

    TextEngine engine;
    CHECK(engine.IsAvailable());
#if defined(GUIKIT_HAS_FREETYPE) && GUIKIT_HAS_FREETYPE
    CHECK(TextEngine::BackendName() == "freetype");
#else
    CHECK(TextEngine::BackendName() == "keel::font");
#endif
    CHECK(engine.LoadDefaultEmbeddedFonts());
    CHECK(engine.HasFontForStyle(FontStyle::Regular));
    CHECK(engine.HasFontForStyle(FontStyle::Bold));
    CHECK(engine.HasFontForStyle(FontStyle::Italic));
    CHECK(engine.HasFontForStyle(FontStyle::BoldItalic));
    CHECK(engine.HasFontForStyle(FontStyle::Monospace));

    // 1. Font metrics
    TextEngine::FontMetrics fm{};
    CHECK(engine.GetFontMetrics(16.0f, FontStyle::Regular, FontHinting::Light, fm));
    CHECK(fm.ascentPx > 8.0f && fm.lineHeightPx > 14.0f && fm.capHeightPx > 6.0f);

    // 2. Proportional vs Monospace advance widths
    const Vec2 propNarrow = engine.MeasureText("iiii", 16.0f, FontStyle::Regular, FontHinting::Light);
    const Vec2 propWide = engine.MeasureText("WWWW", 16.0f, FontStyle::Regular, FontHinting::Light);
    CHECK(propWide.x > propNarrow.x * 1.5f);

    const Vec2 monoNarrow = engine.MeasureText("iiii", 16.0f, FontStyle::Monospace, FontHinting::Light);
    const Vec2 monoWide = engine.MeasureText("WWWW", 16.0f, FontStyle::Monospace, FontHinting::Light);
    CHECK(monoNarrow.x > 10.0f);
    CHECK_NEAR(monoNarrow.x, monoWide.x, 0.1f);

    // 3. GPU-atlas glyph rasterization (Grayscale 1 Bpp & LCD Subpixel 3 Bpp)
    TextEngine::RasterizedGlyph grayGlyph{};
    CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('A'), 16.0f, FontStyle::Regular, FontHinting::Light, false, 0u, grayGlyph));
    CHECK(!grayGlyph.empty && grayGlyph.bitmap != nullptr);
    CHECK(!grayGlyph.isSubpixel);
    CHECK(grayGlyph.width > 4u && grayGlyph.height > 6u);
    CHECK(grayGlyph.strideBytes == grayGlyph.width);
    CHECK(grayGlyph.advancePx > 4.0f);

    TextEngine::RasterizedGlyph lcdGlyph{};
    CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('A'), 16.0f, FontStyle::Regular, FontHinting::Light, true, 0u, lcdGlyph));
    CHECK(!lcdGlyph.empty && lcdGlyph.bitmap != nullptr);
    CHECK(lcdGlyph.isSubpixel);
    CHECK(lcdGlyph.strideBytes == lcdGlyph.width * 3u);

    // 4. Composite glyphs (Cyrillic 'й' U+0439 & Latin 'é' U+00E9) + Synthetic Bold & Italic
    TextEngine::RasterizedGlyph cyrGlyph{};
    CHECK(engine.RasterizeCodepoint(0x0439u, 18.0f, FontStyle::Regular, FontHinting::Light, false, 0u, cyrGlyph));
    CHECK(!cyrGlyph.empty && cyrGlyph.width > 4u && cyrGlyph.height > 8u);

    TextEngine::RasterizedGlyph boldGlyph{};
    CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('H'), 16.0f, FontStyle::Bold, FontHinting::Light, false, 0u, boldGlyph));
    CHECK(!boldGlyph.empty && boldGlyph.width >= grayGlyph.width / 2u);

    TextEngine::RasterizedGlyph italicGlyph{};
    CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('H'), 16.0f, FontStyle::Italic, FontHinting::Light, false, 0u, italicGlyph));
    CHECK(!italicGlyph.empty && italicGlyph.width > 4u);

    // 5. OpenType GPOS kerning check on built-in backend ('A' + 'V' in Roboto)
#if !defined(GUIKIT_HAS_FREETYPE) || !GUIKIT_HAS_FREETYPE
    const keel::f32 avKern = engine.GetKerningPx(keel::as<keel::u32>('A'), keel::as<keel::u32>('V'), 24.0f, FontStyle::Regular, FontHinting::Light);
    CHECK(avKern < -0.2f);
#endif

    // 6. Direct CPU surface rasterization (Grayscale vs SubpixelRGB vs SubpixelBGR)
    constexpr keel::u32 W = 180;
    constexpr keel::u32 H = 48;
    keel::Array<keel::u32> fbGray(W * H, 0xFF202020u);
    keel::Array<keel::u32> fbRgb(W * H, 0xFF202020u);
    keel::Array<keel::u32> fbBgr(W * H, 0xFF202020u);

    const TextRenderSettings sGray{.smoothing = FontSmoothing::Grayscale, .hinting = FontHinting::Light};
    const TextRenderSettings sRgb{.smoothing = FontSmoothing::SubpixelRGB, .hinting = FontHinting::Light};
    const TextRenderSettings sBgr{.smoothing = FontSmoothing::SubpixelBGR, .hinting = FontHinting::Light};
    const Rect clip(0.0f, 0.0f, keel::as<keel::f32>(W), keel::as<keel::f32>(H));
    const Color white(255, 255, 255, 255);

    CHECK(engine.DrawText(fbGray.data(), W, H, W, "Hello 123 Привет", 8.0f, 10.0f, 14.0f, white, FontStyle::Regular, clip, sGray, 1.0f));
    CHECK(engine.DrawText(fbRgb.data(), W, H, W, "Hello 123 Привет", 8.0f, 10.0f, 14.0f, white, FontStyle::Regular, clip, sRgb, 1.0f));
    CHECK(engine.DrawText(fbBgr.data(), W, H, W, "Hello 123 Привет", 8.0f, 10.0f, 14.0f, white, FontStyle::Regular, clip, sBgr, 1.0f));

    keel::usize grayLit = 0, grayChromatic = 0, rgbChromatic = 0, rgbVsBgrDiff = 0;
    for (keel::usize i = 0; i < W * H; ++i) {
        const keel::u32 gPx = fbGray[i];
        const keel::u32 gr = gPx & 0xFFu, gg = (gPx >> 8) & 0xFFu, gb = (gPx >> 16) & 0xFFu;
        if (gr > 0x20u) ++grayLit;
        if (gr != gg || gg != gb) ++grayChromatic;

        const keel::u32 rPx = fbRgb[i];
        const keel::u32 rr = rPx & 0xFFu, rg = (rPx >> 8) & 0xFFu, rb = (rPx >> 16) & 0xFFu;
        if (rr != rg || rg != rb) ++rgbChromatic;
        if (rPx != fbBgr[i]) ++rgbVsBgrDiff;
    }
    CHECK(grayLit > 50u);
    CHECK(grayChromatic == 0u);
    CHECK(rgbChromatic > 20u);
    CHECK(rgbVsBgrDiff > 20u);

    // 7. Vertical stem autohinting (Normal) + LCD phase balance regression check
    {
        TextEngine::RasterizedGlyph hNormGray{};
        CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('H'), 14.0f, FontStyle::Regular, FontHinting::Normal, false, 0u, hNormGray));
        CHECK(!hNormGray.empty && hNormGray.bitmap != nullptr && hNormGray.height > 6u);
        // Middle row above crossbar should have two crisp full-coverage vertical stems (>= 240)
        const keel::u32 probeRow = hNormGray.height / 4u;
        keel::u32 crispCols = 0u;
        for (keel::u32 x = 0u; x < hNormGray.width; ++x) {
            if (hNormGray.bitmap[probeRow * hNormGray.strideBytes + x] >= 240u) {
                ++crispCols;
            }
        }
        CHECK(crispCols >= 2u);

        // Normal + LCD on vertical stem 'H' should have crisp, left/right symmetric R/B subpixels (zero stem hue shift)
        TextEngine::RasterizedGlyph hNormLcd{};
        CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('H'), 14.0f, FontStyle::Regular, FontHinting::Normal, true, 0u, hNormLcd));
        CHECK(!hNormLcd.empty && hNormLcd.bitmap != nullptr && hNormLcd.isSubpixel);
        const keel::u32 lcdProbeRow = hNormLcd.height / 4u;
        keel::u32 balancedStemPx = 0u;
        for (keel::u32 x = 0u; x < hNormLcd.width; ++x) {
            const keel::u8* px = hNormLcd.bitmap + lcdProbeRow * hNormLcd.strideBytes + x * 3u;
            const keel::i32 r = keel::as<keel::i32>(px[0]);
            const keel::i32 g = keel::as<keel::i32>(px[1]);
            const keel::i32 b = keel::as<keel::i32>(px[2]);
            if (g >= 220 && r >= 160 && b >= 160 && keel::abs(r - b) <= 8) {
                ++balancedStemPx;
            }
        }
        CHECK(balancedStemPx >= 2u);

        // FontHinting::Auto resolves cleanly at both 1x and 2x DPI
        keel::Array<keel::u32> fbAuto1x(W * H, 0xFFF8FAFCu);
        keel::Array<keel::u32> fbAuto2x(W * H, 0xFFF8FAFCu);
        const TextRenderSettings sAuto{};
        CHECK(sAuto.hinting == FontHinting::Auto);
        CHECK(engine.DrawText(fbAuto1x.data(), W, H, W, "Hello Привет", 8.0f, 10.0f, 14.0f, Color(15, 23, 42, 255), FontStyle::Regular, clip, sAuto, 1.0f));
        CHECK(engine.DrawText(fbAuto2x.data(), W, H, W, "Hello Привет", 8.0f, 10.0f, 28.0f, Color(15, 23, 42, 255), FontStyle::Regular, clip, sAuto, 2.0f));
        keel::usize inkedLight1x = 0u;
        for (keel::usize i = 0; i < W * H; ++i) {
            if ((fbAuto1x[i] & 0xFFu) < 0xB0u) ++inkedLight1x;
        }
        CHECK(inkedLight1x > 50u);

        // Regression: Light hinting must NOT shave rounded glyph tops ('e','S') or crush 'T' top bar at 125% (16.25px)
        TextEngine::RasterizedGlyph eLight{}, eNone{};
        CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('e'), 16.25f, FontStyle::Monospace, FontHinting::Light, false, 0u, eLight));
        CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('e'), 16.25f, FontStyle::Monospace, FontHinting::None, false, 0u, eNone));
        CHECK(eLight.height + 1u >= eNone.height); // Rounded arch preserved (no 2px overshoot chop)

        TextEngine::RasterizedGlyph tLight{};
        CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('T'), 16.25f, FontStyle::Monospace, FontHinting::Light, false, 0u, tLight));
        CHECK(!tLight.empty && tLight.height >= 12u && tLight.width >= 8u);
        // Row 1 (just beneath the top bar edge) must keep natural lower AA coverage (> 40 on crossbar columns)
        keel::u32 barRow1Hits = 0u;
        for (keel::u32 x = 1u; x + 1u < tLight.width; ++x) {
            if (tLight.bitmap[1u * tLight.strideBytes + x] >= 40u) {
                ++barRow1Hits;
            }
        }
        CHECK(barRow1Hits >= 5u);

        // Regression: small-size synthetic Bold ('E' at 12px) must NOT smear upward halo above cap-height
        // and must keep open counter rows between horizontal bars.
        TextEngine::RasterizedGlyph eReg12{}, eBold12{};
        CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('E'), 12.0f, FontStyle::Regular, FontHinting::Light, false, 0u, eReg12));
        CHECK(engine.RasterizeCodepoint(keel::as<keel::u32>('E'), 12.0f, FontStyle::Bold, FontHinting::Light, false, 0u, eBold12));
        CHECK(eBold12.height == eReg12.height);
        CHECK(eBold12.bearingY == eReg12.bearingY);
        CHECK(eBold12.advancePx > eReg12.advancePx + 0.35f);
    }

    // 8. Multi-face font fallback chain
    {
        TextEngine fbEngine;
        CHECK(fbEngine.IsAvailable());
        const auto robotoAsset = Assets::Find("assets/fonts/Roboto-Regular.ttf");
        const auto monoAsset = Assets::Find("assets/fonts/JetBrainsMono-Regular.ttf");
        CHECK(robotoAsset.has_value() && !robotoAsset->Empty());
        CHECK(monoAsset.has_value() && !monoAsset->Empty());
        CHECK(fbEngine.LoadFontMemory(robotoAsset->Bytes(), FontStyle::Regular, false));
        CHECK(fbEngine.GetFallbackFontCount() == 0u);

        // Register JetBrainsMono as a fallback face behind Roboto-Regular
        CHECK(fbEngine.AddFallbackFontMemory(monoAsset->Bytes(), false));
        CHECK(fbEngine.GetFallbackFontCount() == 1u);

        // Probe a box-drawing glyph (U+2500 '─') present in JetBrainsMono and missing in Roboto-Regular
        TextEngine::RasterizedGlyph boxGlyph{};
        CHECK(fbEngine.RasterizeCodepoint(0x2500u, 16.0f, FontStyle::Regular, FontHinting::Light, false, 0u, boxGlyph));
        CHECK(!boxGlyph.empty && boxGlyph.width >= 6u && boxGlyph.advancePx > 4.0f);

        // Measure & draw a mixed string containing ASCII (primary) + box-drawing (fallback)
        const Vec2 mixedSz = fbEngine.MeasureText("A─B", 16.0f, FontStyle::Regular, FontHinting::Light);
        CHECK(mixedSz.x > 18.0f && mixedSz.y > 10.0f);

        keel::Array<keel::u32> fbMix(W * H, 0xFF101010u);
        CHECK(fbEngine.DrawText(fbMix.data(), W, H, W, "A─B", 4.0f, 8.0f, 16.0f, white, FontStyle::Regular, clip, sGray, 1.0f));
        keel::usize mixLit = 0u;
        for (keel::usize i = 0u; i < W * H; ++i) {
            if ((fbMix[i] & 0xFFu) > 0x10u) ++mixLit;
        }
        CHECK(mixLit > 20u);

        // Clearing fallback faces drops fallback count back to zero
        fbEngine.ClearFallbackFonts();
        CHECK(fbEngine.GetFallbackFontCount() == 0u);
    }

    // 9. Cache trimming
    CHECK(engine.GetCachedGlyphCount() > 0u);
    engine.TrimCaches();
    CHECK(engine.GetCachedGlyphCount() == 0u);
#endif

    if (g_failures == 0) {
        keel::println("[PASS] All GuiKit Text tests passed successfully!");
        return 0;
    }
    keel::eprintln("[FAIL] Total failures: {}", g_failures);
    return 1;
}
