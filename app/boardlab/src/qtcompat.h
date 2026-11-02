#ifndef BOARDLAB_QT_COMPAT_H
#define BOARDLAB_QT_COMPAT_H

#include <QtGlobal>
#include <QString>

namespace boardlab {

#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
constexpr auto kSkipEmptyParts = QString::SkipEmptyParts;
#else
constexpr auto kSkipEmptyParts = Qt::SkipEmptyParts;
#endif

} // namespace boardlab

#endif
