#pragma once

#include "renderer/text/bitmap_font.h"

#include <QColor>
#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringView>

#include <QtGui/qopengl.h>

#include <vector>

class QOpenGLFunctions_3_3_Core;

namespace renderer {

struct TextStyle {
    int size = 3;
    QColor color = Qt::white;
    QColor background = Qt::transparent;
    bool underlined = false;
};

class BitmapFontRenderer {
public:
    bool initialize(const BitmapFont& font, QString* error = nullptr);
    void deinitialize();

    void beginFrame(const QMatrix4x4& screenProjection);
    void drawTextTopLeft(QStringView text, QPointF position, const TextStyle& style);
    void flush();

    QSize measureText(QStringView text, int fontSize) const;
    int lineHeight(int fontSize) const;

private:
    struct GpuFontSize {
        GLuint texture = 0;
        int atlasWidth = 0;
        int atlasHeight = 0;
    };

    struct FontVertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
        float br = 0.0f;
        float bg = 0.0f;
        float bb = 0.0f;
        float ba = 0.0f;
    };

    bool ensureGpuFontSize(int fontSize, QString* error = nullptr);
    void appendGlyphVertices(std::vector<FontVertex>& vertices,
                             const BitmapFontSize& fontSizeData,
                             const BitmapGlyph& glyph,
                             QPointF topLeft,
                             QColor color,
                             QColor background) const;
    void drawVertices(const std::vector<FontVertex>& vertices, int fontSize);

    const BitmapFont* font_ = nullptr;
    QOpenGLFunctions_3_3_Core* functions_ = nullptr;
    QHash<int, GpuFontSize> gpuSizes_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    int vertexCapacity_ = 0;
    QOpenGLShaderProgram shader_;
    QMatrix4x4 screenProjection_;
    bool frameActive_ = false;
};

} // namespace renderer
