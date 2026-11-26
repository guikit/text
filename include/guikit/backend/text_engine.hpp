#pragma once

#include "guikit/backend/i_canvas.hpp"
#include "keel/array.h"
#include "keel/core.h"
#include "keel/hash_map.h"
#include "keel/memory.h"
#include "keel/ref.h"

namespace guikit {

/// Standalone hinted glyph rasterizer and per-glyph LRU cache.
///
/// Decoupled from ThorVG so both CPU (`ThorCanvas`) and GPU (`GlEs3Canvas` glyph atlas)
/// backends share identical font loading, kerning, hinting, and subpixel LCD rasterization.
///
/// Supports two compile-time backends selected via `GUIKIT_TEXT_USE_FREETYPE`:
/// - `OFF` (default): `keel::font` zero-dependency C++20 TrueType/OpenType (TTF/TTC/OTF) rasterizer.
/// - `ON`: Optional FreeType backend (`guikit::text_freetype`).
/// Never leaks third-party headers into public `include/guikit/` headers.
class TextEngine {
public:
    /// Vertical typographic metrics for a resolved font face at a specific pixel size.
    struct FontMetrics {
        keel::f32 ascentPx{0.0f};
        keel::f32 descentPx{0.0f};
        keel::f32 lineHeightPx{0.0f};
        keel::f32 capHeightPx{0.0f};
    };

    /// Non-owning view into a cached rasterized glyph bitmap (1 byte/px Grayscale or 3 bytes/px LCD RGB).
    /// Pointer remains valid until the next mutating call (`RasterizeCodepoint`, `DrawText`, `TrimCaches`, `LoadFontMemory`).
    struct RasterizedGlyph {
        const keel::u8* bitmap{nullptr};
        keel::u16 width{0};
        keel::u16 height{0};
        keel::u16 strideBytes{0};
        keel::i16 bearingX{0};
        keel::i16 bearingY{0};
        keel::f32 advancePx{0.0f};
        bool isSubpixel{false};
        bool empty{true};
    };

    TextEngine() noexcept;
    ~TextEngine() noexcept;

    TextEngine(const TextEngine&) = delete;
    TextEngine& operator=(const TextEngine&) = delete;

    [[nodiscard]] bool IsAvailable() const noexcept { return m_backend != nullptr; }

    /// Returns active text rasterizer backend identifier (`"keel::font"` or `"freetype"`).
    [[nodiscard]] static keel::StringRef BackendName() noexcept;

    /// Loads a TrueType/OpenType font (`.ttf`, `.ttc`, `.otf`) from memory into `style` slot.
    /// If `copyData` is false, `data` must point to static/embedded lifetime storage.
    bool LoadFontMemory(keel::ArrayRef<keel::u8> data, FontStyle style, bool copyData);

    /// Appends a fallback font face queried in registration order whenever the primary face lacks a glyph
    /// (e.g. CJK, Cyrillic, Greek, Emoji, or symbol fonts). Supports up to `kMaxFallbackFonts` faces.
    bool AddFallbackFontMemory(keel::ArrayRef<keel::u8> data, bool copyData);

    /// Releases all registered fallback font faces and invalidates the glyph cache.
    void ClearFallbackFonts() noexcept;

    /// Returns the number of registered fallback font faces.
    [[nodiscard]] keel::usize GetFallbackFontCount() const noexcept { return m_fallbackFaces.size(); }

    /// Loads GuiKit's embedded default Roboto & JetBrainsMono font family from `guikit::Assets`.
    /// Returns true if at least the Regular font face was loaded.
    bool LoadDefaultEmbeddedFonts();

    /// Returns true if a valid face can be resolved for `style` (directly or via Regular fallback).
    [[nodiscard]] bool HasFontForStyle(FontStyle style) const noexcept;

    /// Queries scaled typographic metrics (ascent, descent, lineHeight, capHeight) for a font style.
    [[nodiscard]] bool GetFontMetrics(
        keel::f32 scaledFontSizePx,
        FontStyle style,
        FontHinting hinting,
        FontMetrics& outMetrics
    );

    /// Returns horizontal kerning adjustment in pixels between two Unicode codepoints.
    [[nodiscard]] keel::f32 GetKerningPx(
        keel::u32 prevCodepoint,
        keel::u32 codepoint,
        keel::f32 scaledFontSizePx,
        FontStyle style,
        FontHinting hinting
    );

    /// Rasterizes (or fetches from the internal LRU cache) a single Unicode codepoint for GPU texture atlas upload.
    [[nodiscard]] bool RasterizeCodepoint(
        keel::u32 codepoint,
        keel::f32 scaledFontSizePx,
        FontStyle style,
        FontHinting hinting,
        bool subpixel,
        keel::u8 phase,
        RasterizedGlyph& outGlyph
    );

    /// Measures single-line UTF-8 text width and line box height at `scaledFontSizePx`.
    [[nodiscard]] Vec2 MeasureText(
        keel::StringRef text,
        keel::f32 scaledFontSizePx,
        FontStyle style,
        FontHinting hinting
    );

    /// Draws single-line UTF-8 text at physical pixel top-left `(posX, posY)`.
    bool DrawText(
        keel::u32* dstBuffer,
        keel::u32 dstW,
        keel::u32 dstH,
        keel::u32 dstStride,
        keel::StringRef text,
        keel::f32 posX,
        keel::f32 posY,
        keel::f32 scaledFontSizePx,
        Color color,
        FontStyle style,
        Rect clipPx,
        const TextRenderSettings& settings,
        keel::f32 canvasScale
    );

    /// Draws single-line UTF-8 text aligned inside `boundsPx` using true cap-height centering.
    bool DrawTextBox(
        keel::u32* dstBuffer,
        keel::u32 dstW,
        keel::u32 dstH,
        keel::u32 dstStride,
        keel::StringRef text,
        Rect boundsPx,
        keel::f32 scaledFontSizePx,
        Color color,
        FontStyle style,
        keel::f32 alignX,
        keel::f32 alignY,
        Rect clipPx,
        const TextRenderSettings& settings,
        keel::f32 canvasScale
    );

    /// Releases cached glyph bitmaps while keeping loaded font faces alive.
    void TrimCaches() noexcept;

    [[nodiscard]] keel::usize GetCachedGlyphCount() const noexcept { return m_glyphMap.size(); }

private:
    static constexpr keel::usize kFontStyleCount = 5u;
    static constexpr keel::usize kMaxFallbackFonts = 16u;
    static constexpr keel::usize kMaxGlyphEntries = 4096u;
    static constexpr keel::usize kMaxGlyphBytes = 4u * 1024u * 1024u;
    static constexpr keel::usize kMeasureCacheSlots = 1024u;
    static constexpr keel::usize kMeasureCacheMaxInlineBytes = 128u;
    static constexpr keel::u32 kNil = 0xFFFFFFFFu;

    struct MeasureCacheEntry {
        keel::u64 hash{0};
        keel::f32 scaledFontSizePx{0.0f};
        keel::f32 width{0.0f};
        keel::f32 height{0.0f};
        keel::u8 style{0};
        keel::u8 hinting{0};
        keel::u8 len{0};
        bool valid{false};
        char text[kMeasureCacheMaxInlineBytes]{};
    };

    struct FaceSlot {
        void* face{nullptr};
        keel::Array<keel::u8> ownedData;
        const keel::u8* staticData{nullptr};
        keel::usize staticSize{0};
        keel::u16 currentSizeQ4{0};
    };

    struct ResolvedFace {
        FaceSlot* slot{nullptr};
        keel::u8 slotIndex{0};
        bool synthBold{false};
        bool synthItalic{false};
    };

    struct GlyphKey {
        keel::u32 glyphIndex{0};
        keel::u16 sizeQ4{0};
        keel::u8 slotIndex{0};
        keel::u8 flags{0}; ///< bits: [0..1] hinting, [2] subpixel, [3..4] phase (0..3), [5] bold, [6] italic

        bool operator==(const GlyphKey& o) const noexcept {
            return glyphIndex == o.glyphIndex &&
                   sizeQ4 == o.sizeQ4 &&
                   slotIndex == o.slotIndex &&
                   flags == o.flags;
        }
    };

    struct GlyphKeyHash {
        keel::u64 operator()(const GlyphKey& k) const noexcept {
            keel::u64 h = keel::as<keel::u64>(k.glyphIndex);
            h ^= (keel::as<keel::u64>(k.sizeQ4) |
                  (keel::as<keel::u64>(k.slotIndex) << 16) |
                  (keel::as<keel::u64>(k.flags) << 24)) * 0x9e3779b97f4a7c15ULL;
            h ^= h >> 33;
            return h * 0xff51afd7ed558ccdULL;
        }
    };

    struct GlyphEntry {
        keel::Array<keel::u8> bitmap; ///< 1 byte/px (Grayscale) or 3 bytes/px RGB (LCD)
        keel::u16 width{0};
        keel::u16 height{0};
        keel::u16 strideBytes{0};
        keel::i16 bearingX{0};
        keel::i16 bearingY{0};
        keel::f32 advancePx{0.0f};
        bool isSubpixel{false};
        bool empty{false};
    };

    struct GlyphNode {
        GlyphKey key{};
        GlyphEntry entry{};
        keel::u32 prev{kNil};
        keel::u32 next{kNil};
    };

    [[nodiscard]] ResolvedFace ResolveFace(FontStyle style) noexcept;
    [[nodiscard]] bool ResolveCodepointFace(
        keel::u32 codepoint,
        const ResolvedFace& primary,
        ResolvedFace& outFace,
        keel::u32& outGlyphIndex
    ) noexcept;
    bool EnsureFaceSize(FaceSlot& slot, keel::u16 sizeQ4) noexcept;
    [[nodiscard]] const GlyphEntry& ResolveGlyph(
        const ResolvedFace& rf,
        keel::u32 glyphIndex,
        keel::u16 sizeQ4,
        FontHinting hinting,
        bool subpixel,
        keel::u8 phase
    );

    void EvictTail() noexcept;
    void Detach(keel::u32 idx) noexcept;
    void AttachFront(keel::u32 idx) noexcept;
    void MoveToFront(keel::u32 idx) noexcept;

    [[nodiscard]] static keel::u64 HashMeasureKey(
        keel::StringRef text,
        keel::u16 sizeQ4,
        FontStyle style,
        FontHinting hinting
    ) noexcept {
        keel::u64 h = 14695981039346656037ULL;
        for (keel::usize i = 0u; i < text.size(); ++i) {
            h ^= keel::as<keel::u8>(text[i]);
            h *= 1099511628211ULL;
        }
        const keel::u64 meta = keel::as<keel::u64>(sizeQ4) |
            (keel::as<keel::u64>(style) << 16) |
            (keel::as<keel::u64>(hinting) << 24);
        h ^= meta * 0x9e3779b97f4a7c15ULL;
        h ^= h >> 33;
        return h * 0xff51afd7ed558ccdULL;
    }

    [[nodiscard]] bool LookupMeasureCache(
        keel::StringRef text,
        keel::f32 scaledFontSizePx,
        keel::u16 sizeQ4,
        FontStyle style,
        FontHinting hinting,
        keel::u64& outHash,
        keel::usize& outSlot,
        Vec2& outSize
    ) const noexcept {
        if (text.size() > kMeasureCacheMaxInlineBytes) {
            return false;
        }
        outHash = HashMeasureKey(text, sizeQ4, style, hinting);
        outSlot = keel::as<keel::usize>(outHash & (kMeasureCacheSlots - 1u));
        if (m_measureCache.empty()) {
            return false;
        }
        const MeasureCacheEntry& e = m_measureCache[outSlot];
        if (e.valid && e.hash == outHash && e.len == text.size() &&
            e.style == keel::as<keel::u8>(style) &&
            e.hinting == keel::as<keel::u8>(hinting) &&
            e.scaledFontSizePx == scaledFontSizePx &&
            keel::StringRef(e.text, e.len) == text) {
            outSize = {e.width, e.height};
            return true;
        }
        return false;
    }

    void StoreMeasureCache(
        keel::StringRef text,
        keel::f32 scaledFontSizePx,
        FontStyle style,
        FontHinting hinting,
        keel::u64 hash,
        keel::usize slot,
        Vec2 size
    ) {
        if (text.size() > kMeasureCacheMaxInlineBytes) {
            return;
        }
        if (m_measureCache.empty()) {
            m_measureCache.resize(kMeasureCacheSlots);
        }
        MeasureCacheEntry& e = m_measureCache[slot];
        e.hash = hash;
        e.scaledFontSizePx = scaledFontSizePx;
        e.width = size.x;
        e.height = size.y;
        e.style = keel::as<keel::u8>(style);
        e.hinting = keel::as<keel::u8>(hinting);
        e.len = keel::as<keel::u8>(text.size());
        keel::mem_copy(e.text, text.data(), text.size());
        e.valid = true;
    }

    void* m_backend{nullptr};
    keel::StaticArray<FaceSlot, kFontStyleCount> m_faces{};
    keel::Array<FaceSlot> m_fallbackFaces;

    keel::Array<GlyphNode> m_glyphNodes;
    keel::HashMap<GlyphKey, keel::u32, GlyphKeyHash> m_glyphMap;
    keel::Array<MeasureCacheEntry> m_measureCache;
    keel::usize m_totalGlyphBytes{0};
    keel::u32 m_head{kNil};
    keel::u32 m_tail{kNil};
    keel::u32 m_freeHead{kNil};
};

} // namespace guikit
