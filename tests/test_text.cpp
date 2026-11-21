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
    keel::println("[RUN] Testing GuiKit Text module...");
#if defined(GUIKIT_HAS_FREETYPE) && GUIKIT_HAS_FREETYPE
    using namespace guikit;

    FreeTypeTextEngine ft;
    CHECK(ft.IsAvailable());
    CHECK(ft.LoadDefaultEmbeddedFonts());
    CHECK(ft.HasFontForStyle(FontStyle::Regular));
    CHECK(ft.HasFontForStyle(FontStyle::Bold));
    CHECK(ft.HasFontForStyle(FontStyle::Italic));
    CHECK(ft.HasFontForStyle(FontStyle::BoldItalic));
    CHECK(ft.HasFontForStyle(FontStyle::Monospace));

    // 1. Font metrics
    FreeTypeTextEngine::FontMetrics fm{};
    CHECK(ft.GetFontMetrics(16.0f, FontStyle::Regular, FontHinting::Light, fm));
    CHECK(fm.ascentPx > 8.0f && fm.lineHeightPx > 14.0f && fm.capHeightPx > 6.0f);

    // 2. Proportional vs Monospace advance widths
    const Vec2 propNarrow = ft.MeasureText("iiii", 16.0f, FontStyle::Regular, FontHinting::Light);
    const Vec2 propWide = ft.MeasureText("WWWW", 16.0f, FontStyle::Regular, FontHinting::Light);
    CHECK(propWide.x > propNarrow.x * 1.5f);

    const Vec2 monoNarrow = ft.MeasureText("iiii", 16.0f, FontStyle::Monospace, FontHinting::Light);
    const Vec2 monoWide = ft.MeasureText("WWWW", 16.0f, FontStyle::Monospace, FontHinting::Light);
    CHECK(monoNarrow.x > 10.0f);
    CHECK_NEAR(monoNarrow.x, monoWide.x, 0.1f);

    // 3. GPU-atlas glyph rasterization (Grayscale 1 Bpp & LCD Subpixel 3 Bpp)
    FreeTypeTextEngine::RasterizedGlyph grayGlyph{};
    CHECK(ft.RasterizeCodepoint(static_cast<keel::u32>('A'), 16.0f, FontStyle::Regular, FontHinting::Light, false, 0u, grayGlyph));
    CHECK(!grayGlyph.empty && grayGlyph.bitmap != nullptr);
    CHECK(!grayGlyph.isSubpixel);
    CHECK(grayGlyph.width > 4u && grayGlyph.height > 6u);
    CHECK(grayGlyph.strideBytes == grayGlyph.width);
    CHECK(grayGlyph.advancePx > 4.0f);

    FreeTypeTextEngine::RasterizedGlyph lcdGlyph{};
    CHECK(ft.RasterizeCodepoint(static_cast<keel::u32>('A'), 16.0f, FontStyle::Regular, FontHinting::Light, true, 0u, lcdGlyph));
    CHECK(!lcdGlyph.empty && lcdGlyph.bitmap != nullptr);
    CHECK(lcdGlyph.isSubpixel);
    CHECK(lcdGlyph.strideBytes == lcdGlyph.width * 3u);

    // 4. Direct CPU surface rasterization (Grayscale vs SubpixelRGB vs SubpixelBGR)
    constexpr keel::u32 W = 180;
    constexpr keel::u32 H = 48;
    keel::Array<keel::u32> fbGray(W * H, 0xFF202020u);
    keel::Array<keel::u32> fbRgb(W * H, 0xFF202020u);
    keel::Array<keel::u32> fbBgr(W * H, 0xFF202020u);

    const TextRenderSettings sGray{.smoothing = FontSmoothing::Grayscale, .hinting = FontHinting::Light};
    const TextRenderSettings sRgb{.smoothing = FontSmoothing::SubpixelRGB, .hinting = FontHinting::Light};
    const TextRenderSettings sBgr{.smoothing = FontSmoothing::SubpixelBGR, .hinting = FontHinting::Light};
    const Rect clip(0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H));
    const Color white(255, 255, 255, 255);

    CHECK(ft.DrawText(fbGray.data(), W, H, W, "Hello 123", 8.0f, 10.0f, 14.0f, white, FontStyle::Regular, clip, sGray, 1.0f));
    CHECK(ft.DrawText(fbRgb.data(), W, H, W, "Hello 123", 8.0f, 10.0f, 14.0f, white, FontStyle::Regular, clip, sRgb, 1.0f));
    CHECK(ft.DrawText(fbBgr.data(), W, H, W, "Hello 123", 8.0f, 10.0f, 14.0f, white, FontStyle::Regular, clip, sBgr, 1.0f));

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

    // 5. Cache trimming
    CHECK(ft.GetCachedGlyphCount() > 0u);
    ft.TrimCaches();
    CHECK(ft.GetCachedGlyphCount() == 0u);
#endif

    if (g_failures == 0) {
        keel::println("[PASS] All GuiKit Text tests passed successfully!");
        return 0;
    }
    keel::eprintln("[FAIL] Total failures: {}", g_failures);
    return 1;
}
