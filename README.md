# GuiKit Text (`guikit::text`)

Standalone hinted glyph rasterization, typographic metrics, and LRU glyph caching module for the **GuiKit** C++20 ecosystem.

Shared by both CPU vector backends (`guikit::sdl` / `ThorCanvas`) and hardware GPU backends (`guikit::native` / OpenGL ES3 glyph atlas) without leaking FreeType or STL types into public headers.

## CMake Usage

```cmake
target_link_libraries(my_app PRIVATE guikit::text)
```

```cpp
#include "guikit/text.hpp"
```

## License

GuiKit Text wrapper code is licensed under **0BSD** (`LICENSE`). Bundled `third_party/freetype` is licensed under the FreeType License (FTL) / GPLv2.
