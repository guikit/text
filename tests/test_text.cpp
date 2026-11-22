#include "guikit/text.hpp"
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

    // 7. Cache trimming
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
