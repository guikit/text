#pragma once

#include "guikit/backend/text_engine.hpp"

namespace guikit {

/// Backward-compatible alias for `TextEngine` (backed by built-in rasterizer or optional FreeType).
using FreeTypeTextEngine = TextEngine;

} // namespace guikit
