#pragma once

#include <QImage>
#include <QMetaType>

#include <memory>

using SharedImage = std::shared_ptr<const QImage>;
Q_DECLARE_METATYPE(SharedImage)

