#include "guikit/backend/text_engine.hpp"
#include <cstring>
#include "guikit/backend/text_compositor.hpp"
#include "guikit/core/assets.hpp"
#include "keel/font.h"
#include "keel/math.h"
#include "keel/utf8.h"

namespace guikit {

namespace {

struct KeelFontBackendState {
    keel::font::RasterScratch scratch;
};

[[nodiscard]] inline KeelFontBackendState* AsBackendState(void* ptr) noexcept {
    return static_cast<KeelFontBackendState*>(ptr);
}

[[nodiscard]] inline keel::font::FontFace* AsFontFace(void* ptr) noexcept {
    return static_cast<keel::font::FontFace*>(ptr);
}

[[nodiscard]] inline const keel::font::FontFace* AsFontFaceConst(const void* ptr) noexcept {
    return static_cast<const keel::font::FontFace*>(ptr);
}

[[nodiscard]] constexpr keel::font::Hinting ToKeelHinting(FontHinting h) noexcept {
    const FontHinting resolved = TextCompositor::ResolveEffectiveHinting(h, 1.0f);
    if (resolved == FontHinting::None) return keel::font::Hinting::None;
    if (resolved == FontHinting::Normal) return keel::font::Hinting::Normal;
    return keel::font::Hinting::Light;
}

[[nodiscard]] constexpr keel::usize StyleToSlot(FontStyle style) noexcept {
    const auto idx = keel::as<keel::usize>(style);
    return (idx < 5u) ? idx : 0u;
}

[[nodiscard]] constexpr keel::u16 QuantizeSizeQ4(keel::f32 scaledFontSizePx) noexcept {
    return keel::as<keel::u16>(keel::clamp(keel::round_i32(scaledFontSizePx * 4.0f), 4, 2048));
}

[[nodiscard]] constexpr keel::u8 PackGlyphFlags(
    FontHinting hinting,
    bool subpixel,
    keel::u8 phase,
    bool synthBold,
    bool synthItalic
) noexcept {
    keel::u8 f = keel::as<keel::u8>(keel::as<keel::u8>(hinting) & 0x03u);
    if (subpixel) f |= (1u << 2);
    f |= keel::as<keel::u8>((phase & 0x03u) << 3);
    if (synthBold) f |= (1u << 5);
    if (synthItalic) f |= (1u << 6);
    return f;
}

[[nodiscard]] constexpr bool UsesSubpixelPhase(FontHinting hinting) noexcept {
    return TextCompositor::ResolveEffectiveHinting(hinting, 1.0f) != FontHinting::Normal;
}

} // namespace

keel::StringRef TextEngine::BackendName() noexcept {
    return "keel::font";
}

TextEngine::TextEngine() noexcept {
    m_backend = new KeelFontBackendState();
}

TextEngine::~TextEngine() noexcept {
    TrimCaches();
    ClearFallbackFonts();
    for (auto& slot : m_faces) {
        if (slot.face != nullptr) {
            delete AsFontFace(slot.face);
            slot.face = nullptr;
        }
        slot.ownedData = keel::Array<keel::u8>{};
        slot.staticData = nullptr;
        slot.staticSize = 0u;
    }
    if (m_backend != nullptr) {
        delete AsBackendState(m_backend);
        m_backend = nullptr;
    }
}

bool TextEngine::AddFallbackFontMemory(keel::ArrayRef<keel::u8> data, bool copyData) {
    if (!m_backend || data.empty() || m_fallbackFaces.size() >= kMaxFallbackFonts) {
        return false;
    }
    keel::Array<keel::u8> newOwned;
    const keel::u8* bufPtr = data.data();
    const keel::usize bufSize = data.size();
    if (copyData) {
        newOwned.resize(bufSize);
        keel::mem_copy(newOwned.data(), bufPtr, bufSize);
        bufPtr = newOwned.data();
    }
    auto* newFace = new keel::font::FontFace();
    if (!newFace->load(bufPtr, bufSize)) {
        delete newFace;
        return false;
    }
    FaceSlot slot{};
    slot.face = newFace;
    slot.ownedData = keel::move(newOwned);
    slot.staticData = copyData ? nullptr : data.data();
    slot.staticSize = copyData ? 0u : bufSize;
    slot.currentSizeQ4 = 0u;
    m_fallbackFaces.push_back(keel::move(slot));
    TrimCaches();
    return true;
}

void TextEngine::ClearFallbackFonts() noexcept {
    if (m_fallbackFaces.empty()) {
        return;
    }
    for (auto& slot : m_fallbackFaces) {
        if (slot.face != nullptr) {
            delete AsFontFace(slot.face);
            slot.face = nullptr;
        }
        slot.ownedData = keel::Array<keel::u8>{};
        slot.staticData = nullptr;
        slot.staticSize = 0u;
    }
    m_fallbackFaces.clear();
    TrimCaches();
}

bool TextEngine::ResolveCodepointFace(
    keel::u32 codepoint,
    const ResolvedFace& primary,
    ResolvedFace& outFace,
    keel::u32& outGlyphIndex
) noexcept {
    outFace = primary;
    outGlyphIndex = 0u;
    if (!primary.slot || !primary.slot->face) {
        return false;
    }
    const keel::u32 primaryIdx = AsFontFaceConst(primary.slot->face)->glyph_index(codepoint);
    if (primaryIdx != 0u) {
        outGlyphIndex = primaryIdx;
        return true;
    }
    for (keel::usize i = 0; i < m_fallbackFaces.size(); ++i) {
        FaceSlot& fb = m_fallbackFaces[i];
        if (!fb.face) continue;
        const keel::u32 fbIdx = AsFontFaceConst(fb.face)->glyph_index(codepoint);
        if (fbIdx != 0u) {
            outFace.slot = &fb;
            outFace.slotIndex = keel::as<keel::u8>(kFontStyleCount + i);
            outFace.synthBold = primary.synthBold;
            outFace.synthItalic = primary.synthItalic;
            outGlyphIndex = fbIdx;
            return true;
        }
    }
    return false;
}

bool TextEngine::LoadFontMemory(keel::ArrayRef<keel::u8> data, FontStyle style, bool copyData) {
    if (!m_backend || data.empty()) {
        return false;
    }

    const keel::usize idx = StyleToSlot(style);
    FaceSlot& slot = m_faces[idx];

    keel::Array<keel::u8> newOwned;
    const keel::u8* bufPtr = data.data();
    const keel::usize bufSize = data.size();
    if (copyData) {
        newOwned.resize(bufSize);
        keel::mem_copy(newOwned.data(), bufPtr, bufSize);
        bufPtr = newOwned.data();
    }

    auto* newFace = new keel::font::FontFace();
    if (!newFace->load(bufPtr, bufSize)) {
        delete newFace;
        return false;
    }

    if (slot.face != nullptr) {
        delete AsFontFace(slot.face);
        slot.face = nullptr;
    }
    slot.face = newFace;
    slot.ownedData = keel::move(newOwned);
    slot.staticData = copyData ? nullptr : data.data();
    slot.staticSize = copyData ? 0u : bufSize;
    slot.currentSizeQ4 = 0u;

    TrimCaches();
    return true;
}

bool TextEngine::LoadDefaultEmbeddedFonts() {
    if (!m_backend) {
        return false;
    }
    struct DefaultFontSlot {
        keel::StringRef path;
        FontStyle style;
    };
    static constexpr DefaultFontSlot kDefaults[] = {
        {"assets/fonts/Roboto-Regular.ttf", FontStyle::Regular},
        {"assets/fonts/Roboto-Bold.ttf", FontStyle::Bold},
        {"assets/fonts/Roboto-Italic.ttf", FontStyle::Italic},
        {"assets/fonts/Roboto-BoldItalic.ttf", FontStyle::BoldItalic},
        {"assets/fonts/JetBrainsMono-Regular.ttf", FontStyle::Monospace},
    };
    bool loadedRegular = false;
    for (const auto& entry : kDefaults) {
        if (const auto asset = Assets::Find(entry.path); asset && !asset->Empty()) {
            const bool ok = LoadFontMemory(asset->Bytes(), entry.style, /*copyData=*/false);
            if (ok && entry.style == FontStyle::Regular) {
                loadedRegular = true;
            }
        }
    }
    return loadedRegular || HasFontForStyle(FontStyle::Regular);
}

bool TextEngine::GetFontMetrics(
    keel::f32 scaledFontSizePx,
    FontStyle style,
    FontHinting hinting,
    FontMetrics& outMetrics
) {
    outMetrics = FontMetrics{};
    if (scaledFontSizePx <= 0.0f) {
        return false;
    }
    const ResolvedFace rf = ResolveFace(style);
    if (!rf.slot) {
        return false;
    }
    const keel::u16 sizeQ4 = QuantizeSizeQ4(scaledFontSizePx);
    if (!EnsureFaceSize(*rf.slot, sizeQ4)) {
        return false;
    }

    const keel::font::FontFace* face = AsFontFaceConst(rf.slot->face);
    const keel::u16 upem = face->units_per_em();
    const keel::i16 asc = face->ascender();
    const keel::i16 desc = face->descender();
    if (upem > 0u && asc > desc) {
        const keel::f32 scale = scaledFontSizePx / keel::as<keel::f32>(upem);
        outMetrics.ascentPx = keel::as<keel::f32>(asc) * scale;
        outMetrics.descentPx = keel::as<keel::f32>(-desc) * scale;
        outMetrics.lineHeightPx = keel::as<keel::f32>(asc - desc) * scale;
    } else {
        outMetrics.ascentPx = scaledFontSizePx * 0.85f;
        outMetrics.descentPx = scaledFontSizePx * 0.25f;
        outMetrics.lineHeightPx = scaledFontSizePx * 1.25f;
    }

    const keel::u32 capIdx = face->glyph_index(keel::as<keel::u32>('H'));
    if (capIdx != 0u) {
        const GlyphEntry& capG = ResolveGlyph(rf, capIdx, sizeQ4, hinting, false, 0u);
        outMetrics.capHeightPx = (capG.bearingY > 0) ? keel::as<keel::f32>(capG.bearingY) : (scaledFontSizePx * 0.72f);
    } else {
        outMetrics.capHeightPx = scaledFontSizePx * 0.72f;
    }
    return true;
}

keel::f32 TextEngine::GetKerningPx(
    keel::u32 prevCodepoint,
    keel::u32 codepoint,
    keel::f32 scaledFontSizePx,
    FontStyle style,
    FontHinting hinting
) {
    if (prevCodepoint == 0u || codepoint == 0u || scaledFontSizePx <= 0.0f) {
        return 0.0f;
    }
    const ResolvedFace rf = ResolveFace(style);
    if (!rf.slot) {
        return 0.0f;
    }
    const keel::font::FontFace* face = AsFontFaceConst(rf.slot->face);
    if (!face || !face->has_kerning() || face->units_per_em() == 0u) {
        return 0.0f;
    }
    const keel::u32 g1 = face->glyph_index(prevCodepoint);
    const keel::u32 g2 = face->glyph_index(codepoint);
    if (g1 == 0u || g2 == 0u) {
        return 0.0f;
    }
    const keel::i32 kernUnits = face->kerning_units(g1, g2);
    if (kernUnits == 0) {
        return 0.0f;
    }
    const keel::f32 px = keel::as<keel::f32>(kernUnits) * (scaledFontSizePx / keel::as<keel::f32>(face->units_per_em()));
    return UsesSubpixelPhase(hinting) ? px : keel::round(px);
}

bool TextEngine::RasterizeCodepoint(
    keel::u32 codepoint,
    keel::f32 scaledFontSizePx,
    FontStyle style,
    FontHinting hinting,
    bool subpixel,
    keel::u8 phase,
    RasterizedGlyph& outGlyph
) {
    outGlyph = RasterizedGlyph{};
    if (scaledFontSizePx <= 0.0f) {
        return false;
    }
    const ResolvedFace rf = ResolveFace(style);
    if (!rf.slot) {
        return false;
    }
    const keel::u16 sizeQ4 = QuantizeSizeQ4(scaledFontSizePx);
    ResolvedFace glyphFace{};
    keel::u32 glyphIdx = 0u;
    (void)ResolveCodepointFace(codepoint, rf, glyphFace, glyphIdx);
    if (!glyphFace.slot || !EnsureFaceSize(*glyphFace.slot, sizeQ4)) {
        return false;
    }
    const GlyphEntry& g = ResolveGlyph(glyphFace, glyphIdx, sizeQ4, hinting, subpixel, phase);
    outGlyph.bitmap = g.bitmap.empty() ? nullptr : g.bitmap.data();
    outGlyph.width = g.width;
    outGlyph.height = g.height;
    outGlyph.strideBytes = g.strideBytes;
    outGlyph.bearingX = g.bearingX;
    outGlyph.bearingY = g.bearingY;
    outGlyph.advancePx = g.advancePx;
    outGlyph.isSubpixel = g.isSubpixel;
    outGlyph.empty = g.empty;
    return true;
}

bool TextEngine::HasFontForStyle(FontStyle style) const noexcept {
    if (!m_backend) return false;
    const keel::usize idx = StyleToSlot(style);
    if (m_faces[idx].face != nullptr) return true;
    if (style == FontStyle::BoldItalic) {
        if (m_faces[StyleToSlot(FontStyle::Bold)].face != nullptr ||
            m_faces[StyleToSlot(FontStyle::Italic)].face != nullptr) {
            return true;
        }
    }
    return m_faces[0].face != nullptr;
}

TextEngine::ResolvedFace TextEngine::ResolveFace(FontStyle style) noexcept {
    const keel::usize idx = StyleToSlot(style);
    if (m_faces[idx].face != nullptr) {
        return ResolvedFace{
            .slot = &m_faces[idx],
            .slotIndex = keel::as<keel::u8>(idx),
            .synthBold = false,
            .synthItalic = false
        };
    }

    if (style == FontStyle::BoldItalic) {
        const keel::usize boldIdx = StyleToSlot(FontStyle::Bold);
        if (m_faces[boldIdx].face != nullptr) {
            return ResolvedFace{
                .slot = &m_faces[boldIdx],
                .slotIndex = keel::as<keel::u8>(boldIdx),
                .synthBold = false,
                .synthItalic = true
            };
        }
        const keel::usize italIdx = StyleToSlot(FontStyle::Italic);
        if (m_faces[italIdx].face != nullptr) {
            return ResolvedFace{
                .slot = &m_faces[italIdx],
                .slotIndex = keel::as<keel::u8>(italIdx),
                .synthBold = true,
                .synthItalic = false
            };
        }
    }

    if (m_faces[0].face != nullptr) {
        return ResolvedFace{
            .slot = &m_faces[0],
            .slotIndex = 0u,
            .synthBold = (style == FontStyle::Bold || style == FontStyle::BoldItalic),
            .synthItalic = (style == FontStyle::Italic || style == FontStyle::BoldItalic)
        };
    }

    return ResolvedFace{};
}

bool TextEngine::EnsureFaceSize(FaceSlot& slot, keel::u16 sizeQ4) noexcept {
    if (!slot.face || sizeQ4 == 0u) return false;
    slot.currentSizeQ4 = sizeQ4;
    return true;
}

const TextEngine::GlyphEntry& TextEngine::ResolveGlyph(
    const ResolvedFace& rf,
    keel::u32 glyphIndex,
    keel::u16 sizeQ4,
    FontHinting hinting,
    bool subpixel,
    keel::u8 phase
) {
    const keel::u8 effPhase = UsesSubpixelPhase(hinting) ? keel::as<keel::u8>(phase & 0x03u) : 0u;
    const GlyphKey key{
        .glyphIndex = glyphIndex,
        .sizeQ4 = sizeQ4,
        .slotIndex = rf.slotIndex,
        .flags = PackGlyphFlags(hinting, subpixel, effPhase, rf.synthBold, rf.synthItalic)
    };

    if (const keel::u32* idxPtr = m_glyphMap.get(key)) {
        const keel::u32 idx = *idxPtr;
        MoveToFront(idx);
        return m_glyphNodes[idx].entry;
    }

    GlyphEntry entry{};
    entry.isSubpixel = subpixel;
    entry.empty = true;

    if (rf.slot && EnsureFaceSize(*rf.slot, sizeQ4)) {
        const keel::font::FontFace* face = AsFontFaceConst(rf.slot->face);
        const keel::f32 fontSizePx = keel::as<keel::f32>(sizeQ4) * 0.25f;
        const keel::font::RasterOptions rOpts{
            .hinting = ToKeelHinting(hinting),
            .subpixel = subpixel,
            .subpixelPhase = effPhase,
            .syntheticBold = rf.synthBold,
            .syntheticItalic = rf.synthItalic
        };
        keel::font::GlyphBitmap bm{};
        keel::font::RasterScratch* sc = m_backend ? &AsBackendState(m_backend)->scratch : nullptr;
        if (face->rasterize(glyphIndex, fontSizePx, rOpts, bm, sc)) {
            entry.bitmap = keel::move(bm.pixels);
            entry.width = bm.width;
            entry.height = bm.height;
            entry.strideBytes = bm.strideBytes;
            entry.bearingX = bm.bearingX;
            entry.bearingY = bm.bearingY;
            entry.advancePx = bm.advancePx;
            entry.isSubpixel = bm.isSubpixel;
            entry.empty = bm.empty;
        }
    }

    const keel::usize newBytes = entry.bitmap.capacity();
    while (!m_glyphMap.empty() &&
           (m_glyphMap.size() >= kMaxGlyphEntries ||
            (newBytes > 0u && m_totalGlyphBytes + newBytes > kMaxGlyphBytes))) {
        EvictTail();
    }

    keel::u32 slotIdx = kNil;
    if (m_freeHead != kNil) {
        slotIdx = m_freeHead;
        m_freeHead = m_glyphNodes[slotIdx].next;
        m_glyphNodes[slotIdx].key = key;
        m_glyphNodes[slotIdx].entry = keel::move(entry);
        m_glyphNodes[slotIdx].prev = kNil;
        m_glyphNodes[slotIdx].next = kNil;
    } else {
        if (m_glyphNodes.capacity() == 0u) {
            m_glyphNodes.reserve(512u);
        }
        slotIdx = keel::as<keel::u32>(m_glyphNodes.size());
        m_glyphNodes.push_back(GlyphNode{
            .key = key,
            .entry = keel::move(entry),
            .prev = kNil,
            .next = kNil
        });
    }

    m_totalGlyphBytes += newBytes;
    AttachFront(slotIdx);
    m_glyphMap.put(key, slotIdx);
    return m_glyphNodes[slotIdx].entry;
}

Vec2 TextEngine::MeasureText(
    keel::StringRef text,
    keel::f32 scaledFontSizePx,
    FontStyle style,
    FontHinting hinting
) {
    if (text.empty() || scaledFontSizePx <= 0.0f) {
        return {0.0f, 0.0f};
    }

    const ResolvedFace rf = ResolveFace(style);
    if (!rf.slot) {
        return {0.0f, 0.0f};
    }

    const keel::u16 sizeQ4 = QuantizeSizeQ4(scaledFontSizePx);
    if (!EnsureFaceSize(*rf.slot, sizeQ4)) {
        return {0.0f, 0.0f};
    }

    keel::u64 cacheHash = 0u;
    keel::usize cacheSlot = 0u;
    Vec2 cachedSize{};
    if (LookupMeasureCache(text, scaledFontSizePx, sizeQ4, style, hinting, cacheHash, cacheSlot, cachedSize)) {
        return cachedSize;
    }

    const keel::font::FontFace* face = AsFontFaceConst(rf.slot->face);
    const keel::f32 qSizePx = keel::as<keel::f32>(sizeQ4) * 0.25f;
    const keel::font::Hinting kHint = ToKeelHinting(hinting);

    keel::f32 totalAdvance = 0.0f;
    keel::u32 prevGlyph = 0u;
    keel::u8 prevSlotIndex = 0xFFu;
    keel::usize offset = 0u;
    while (offset < text.size()) {
        const keel::u32 cp = keel::utf8_decode_next(text, offset);
        if (cp == '\n' || cp == '\r') continue;
        if (cp == '\t') {
            const keel::u32 spIdx = face->glyph_index(keel::as<keel::u32>(' '));
            const keel::f32 spAdv = face->advance_width_px(spIdx, qSizePx, kHint, rf.synthBold);
            totalAdvance += (spAdv > 0.0f) ? (spAdv * 4.0f) : (scaledFontSizePx * 2.4f);
            prevGlyph = 0u;
            prevSlotIndex = 0xFFu;
            continue;
        }
        ResolvedFace glyphFace{};
        keel::u32 glyphIdx = 0u;
        (void)ResolveCodepointFace(cp, rf, glyphFace, glyphIdx);
        const keel::font::FontFace* activeFace = AsFontFaceConst(glyphFace.slot->face);
        if (glyphFace.slotIndex == prevSlotIndex && activeFace->has_kerning() && prevGlyph != 0u && glyphIdx != 0u) {
            totalAdvance += activeFace->kerning_px(prevGlyph, glyphIdx, scaledFontSizePx, kHint);
        }
        totalAdvance += activeFace->advance_width_px(glyphIdx, qSizePx, kHint, glyphFace.synthBold);
        prevGlyph = glyphIdx;
        prevSlotIndex = glyphFace.slotIndex;
    }

    const keel::f32 glyphBoxH = (face->units_per_em() > 0u && face->ascender() > face->descender())
        ? (scaledFontSizePx * keel::as<keel::f32>(face->ascender() - face->descender()) / keel::as<keel::f32>(face->units_per_em()))
        : (scaledFontSizePx * 1.25f);
    const Vec2 result{totalAdvance, glyphBoxH};
    StoreMeasureCache(text, scaledFontSizePx, style, hinting, cacheHash, cacheSlot, result);
    return result;
}

bool TextEngine::DrawText(
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
) {
    if (!dstBuffer || text.empty() || scaledFontSizePx <= 0.0f || color.a == 0u) {
        return true;
    }

    const ResolvedFace rf = ResolveFace(style);
    if (!rf.slot) {
        return false;
    }

    const keel::u16 sizeQ4 = QuantizeSizeQ4(scaledFontSizePx);
    if (!EnsureFaceSize(*rf.slot, sizeQ4)) {
        return false;
    }

    const keel::font::FontFace* face = AsFontFaceConst(rf.slot->face);
    const keel::f32 scale = (face->units_per_em() > 0u)
        ? (scaledFontSizePx / keel::as<keel::f32>(face->units_per_em()))
        : 0.0f;
    const keel::f32 ascPx = (face->units_per_em() > 0u && face->ascender() > 0)
        ? (keel::as<keel::f32>(face->ascender()) * scale)
        : (scaledFontSizePx * 0.85f);
    const keel::i32 baselineY = keel::round_i32(posY + ascPx);

    const FontSmoothing effSmooth = TextCompositor::ResolveEffectiveSmoothing(settings.smoothing, canvasScale);
    const FontHinting effHint = TextCompositor::ResolveEffectiveHinting(settings.hinting, canvasScale);
    const bool useSubpixel = (effSmooth == FontSmoothing::SubpixelRGB || effSmooth == FontSmoothing::SubpixelBGR);
    const bool bgrOrder = (effSmooth == FontSmoothing::SubpixelBGR);
    const bool subPhase = UsesSubpixelPhase(effHint);

    keel::f32 penX = posX;
    keel::u32 prevGlyph = 0u;
    keel::u8 prevSlotIndex = 0xFFu;
    keel::usize offset = 0u;

    while (offset < text.size()) {
        const keel::u32 cp = keel::utf8_decode_next(text, offset);
        if (cp == '\n' || cp == '\r') continue;
        if (cp == '\t') {
            const keel::u32 spIdx = face->glyph_index(keel::as<keel::u32>(' '));
            const GlyphEntry& sp = ResolveGlyph(rf, spIdx, sizeQ4, effHint, false, 0u);
            penX += (sp.advancePx > 0.0f) ? (sp.advancePx * 4.0f) : (scaledFontSizePx * 2.4f);
            prevGlyph = 0u;
            prevSlotIndex = 0xFFu;
            continue;
        }

        ResolvedFace glyphFace{};
        keel::u32 glyphIdx = 0u;
        (void)ResolveCodepointFace(cp, rf, glyphFace, glyphIdx);
        (void)EnsureFaceSize(*glyphFace.slot, sizeQ4);
        const keel::font::FontFace* activeFace = AsFontFaceConst(glyphFace.slot->face);
        if (glyphFace.slotIndex == prevSlotIndex && activeFace->has_kerning() && prevGlyph != 0u && glyphIdx != 0u) {
            const keel::i32 kernUnits = activeFace->kerning_units(prevGlyph, glyphIdx);
            if (kernUnits != 0 && activeFace->units_per_em() > 0u) {
                const keel::f32 kPx = keel::as<keel::f32>(kernUnits) * (scaledFontSizePx / keel::as<keel::f32>(activeFace->units_per_em()));
                penX += subPhase ? kPx : keel::round(kPx);
            }
        }

        keel::i32 cellX = 0;
        keel::u8 phase = 0u;
        if (subPhase) {
            if (useSubpixel) {
                // Exact 1/3-pixel quantization aligned to physical R/G/B LCD subpixels
                const keel::i32 qx = keel::round_i32(penX * 3.0f);
                keel::i32 rem = qx % 3;
                if (rem < 0) rem += 3;
                cellX = (qx - rem) / 3;
                phase = keel::as<keel::u8>(rem);
            } else {
                const keel::i32 qx = keel::round_i32(penX * 4.0f);
                cellX = qx >> 2;
                phase = keel::as<keel::u8>(qx & 3);
            }
        } else {
            cellX = keel::round_i32(penX);
        }

        const GlyphEntry& g = ResolveGlyph(glyphFace, glyphIdx, sizeQ4, effHint, useSubpixel, phase);
        if (!g.empty && !g.bitmap.empty()) {
            const keel::i32 gx = cellX + g.bearingX;
            const keel::i32 gy = baselineY - g.bearingY;
            if (g.isSubpixel) {
                TextCompositor::BlitSubpixelMask(
                    dstBuffer, dstW, dstH, dstStride,
                    g.bitmap.data(), g.width, g.height, g.strideBytes,
                    gx, gy, clipPx, color, settings, scaledFontSizePx, bgrOrder
                );
            } else {
                TextCompositor::BlitGrayscaleMask(
                    dstBuffer, dstW, dstH, dstStride,
                    g.bitmap.data(), g.width, g.height, g.strideBytes,
                    gx, gy, clipPx, color, settings, scaledFontSizePx
                );
            }
        }

        penX += g.advancePx;
        prevGlyph = glyphIdx;
        prevSlotIndex = glyphFace.slotIndex;
    }

    return true;
}

bool TextEngine::DrawTextBox(
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
) {
    if (!dstBuffer || text.empty() || scaledFontSizePx <= 0.0f || color.a == 0u) {
        return true;
    }

    const ResolvedFace rf = ResolveFace(style);
    if (!rf.slot) {
        return false;
    }

    const keel::u16 sizeQ4 = QuantizeSizeQ4(scaledFontSizePx);
    if (!EnsureFaceSize(*rf.slot, sizeQ4)) {
        return false;
    }

    const keel::font::FontFace* face = AsFontFaceConst(rf.slot->face);
    const keel::f32 ascPx = (face->units_per_em() > 0u && face->ascender() > 0)
        ? (scaledFontSizePx * keel::as<keel::f32>(face->ascender()) / keel::as<keel::f32>(face->units_per_em()))
        : (scaledFontSizePx * 0.85f);

    const FontHinting effHint = TextCompositor::ResolveEffectiveHinting(settings.hinting, canvasScale);
    const keel::u32 capIdx = face->glyph_index(keel::as<keel::u32>('H'));
    const GlyphEntry& capGlyph = ResolveGlyph(rf, capIdx, sizeQ4, effHint, false, 0u);
    const keel::f32 capH = (!capGlyph.empty && capGlyph.height > 0u)
        ? keel::as<keel::f32>(capGlyph.height)
        : (scaledFontSizePx * 0.72f);
    const keel::f32 capBearingY = (!capGlyph.empty && capGlyph.bearingY > 0)
        ? keel::as<keel::f32>(capGlyph.bearingY)
        : capH;

    const keel::f32 totalW = MeasureText(text, scaledFontSizePx, style, effHint).x;
    const keel::f32 startX = boundsPx.x + (boundsPx.w - totalW) * alignX;
    const keel::f32 baselineY = boundsPx.y + (boundsPx.h - capH) * alignY + capBearingY;
    const keel::f32 topY = baselineY - ascPx;

    return DrawText(
        dstBuffer, dstW, dstH, dstStride,
        text, startX, topY,
        scaledFontSizePx, color, style, clipPx, settings, canvasScale
    );
}

void TextEngine::TrimCaches() noexcept {
    m_glyphMap = {};
    m_glyphNodes = {};
    m_measureCache = {};
    m_totalGlyphBytes = 0u;
    m_head = kNil;
    m_tail = kNil;
    m_freeHead = kNil;
    if (m_backend != nullptr) {
        AsBackendState(m_backend)->scratch.release();
    }
}

void TextEngine::EvictTail() noexcept {
    if (m_tail == kNil) {
        return;
    }
    const keel::u32 slot = m_tail;
    GlyphNode& victim = m_glyphNodes[slot];
    const keel::usize oldBytes = victim.entry.bitmap.capacity();
    m_totalGlyphBytes = (m_totalGlyphBytes >= oldBytes) ? (m_totalGlyphBytes - oldBytes) : 0u;
    (void)m_glyphMap.remove(victim.key);
    Detach(slot);
    victim.entry.bitmap = keel::Array<keel::u8>{};
    victim.next = m_freeHead;
    m_freeHead = slot;
}

void TextEngine::Detach(keel::u32 idx) noexcept {
    GlyphNode& n = m_glyphNodes[idx];
    if (n.prev != kNil) {
        m_glyphNodes[n.prev].next = n.next;
    } else {
        m_head = n.next;
    }
    if (n.next != kNil) {
        m_glyphNodes[n.next].prev = n.prev;
    } else {
        m_tail = n.prev;
    }
    n.prev = kNil;
    n.next = kNil;
}

void TextEngine::AttachFront(keel::u32 idx) noexcept {
    GlyphNode& n = m_glyphNodes[idx];
    n.prev = kNil;
    n.next = m_head;
    if (m_head != kNil) {
        m_glyphNodes[m_head].prev = idx;
    } else {
        m_tail = idx;
    }
    m_head = idx;
}

void TextEngine::MoveToFront(keel::u32 idx) noexcept {
    if (idx == m_head) {
        return;
    }
    Detach(idx);
    AttachFront(idx);
}

} // namespace guikit
