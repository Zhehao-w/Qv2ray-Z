#pragma once

#include <QObject>

namespace Qv2ray::components
{
    // Compatibility shim for the legacy MainWindow call site. Qv2ray-Z no
    // longer performs in-app release checks; releases are distributed through
    // the repository packaging workflow.
    class QvUpdateChecker final : public QObject
    {
      public:
        explicit QvUpdateChecker(QObject *parent = nullptr) : QObject(parent) {}
        void CheckUpdate() const {}
    };
} // namespace Qv2ray::components
using namespace Qv2ray::components;
