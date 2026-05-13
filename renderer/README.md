# Renderer Architecture

`renderer/` is the generic drawing layer. Domain code should build draw commands and submit them through `renderer::Renderer`; it should not call OpenGL directly.

The layering is:

1. `renderer::CommandBuffer` stores draw commands for one frame.
2. Builders in `builders.h` translate convenient shapes and text into command-buffer entries.
3. `renderer::Renderer` is the backend interface. The current backend is `renderer/gl/OpenGLRenderer`.

Raw OpenGL state, shaders, and GL object ownership live in `renderer/gl/`. ASDE-X code may use `renderer::Renderer`, builders, bitmap fonts, colors, and geometry helpers, but should not include OpenGL headers or name GL types directly. `asdex/scope.*` remains a Qt `QOpenGLWidget` shell because Qt owns the drawable surface.
