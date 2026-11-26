# GuiKit Text (`guikit::text`)

Standalone glyph rasterization, typographic metrics, and LRU glyph caching module for the **GuiKit** C++20 ecosystem.

Uses the built-in zero-dependency C++20 **`keel::font`** TrueType/OpenType rasterizer by default, with an **optional FreeType** backend (`-DGUIKIT_TEXT_USE_FREETYPE=ON`).

Shared by both GuiKit's own GPU backend (`guikit::native` / OpenGL ES 3.0 / WebGL2 glyph atlas) and the optional CPU vector backend (`guikit::sdl` / `ThorCanvas`) without leaking FreeType or STL types into public headers.

## CMake Usage

```cmake
# Default: zero-dependency keel::font TrueType/OpenType rasterizer
target_link_libraries(my_app PRIVATE guikit::text)

# Optional: enable FreeType rasterizer backend
# cmake -DGUIKIT_TEXT_USE_FREETYPE=ON ...
```

```cpp
#include "guikit/text.hpp"
```

## License

GuiKit Text wrapper code is licensed under **0BSD** (`LICENSE`). Built-in `keel::font` is licensed under **0BSD**. Optional `third_party/freetype` (when enabled) is licensed under the FreeType License (FTL) / GPLv2.
