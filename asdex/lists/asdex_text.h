#pragma once

#include <QColor>
#include <QString>

#include <vector>

namespace asdex {

struct TextFragment {
    QString text;
    QColor foreground;
    QColor background = Qt::transparent;
    bool underlined = false;
    bool newLine = true;
};

struct TextBlock {
    std::vector<TextFragment> fragments;
    int lineSpacing = 0;
};

} // namespace asdex
