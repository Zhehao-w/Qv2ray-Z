#pragma once
#include <QObject>

#define QvMessageBusConnect(CLASSNAME)                                                                                                                \
    connect(&UIMessageBus, &QvMessageBusObject::QvSendMessage, this, [this](const QvMBMessage &msg) {                                                \
        if (msg != RETRANSLATE)                                                                                                                      \
            this->QvMessageBusSlotIdentifier(msg);                                                                                                   \
    })

#define QvMessageBusSlotSig const QvMBMessage &msg
#define QvMessageBusSlotIdentifier on_QvMessageReceived

#define QvMessageBusSlotDecl void QvMessageBusSlotIdentifier(QvMessageBusSlotSig)
#define QvMessageBusSlotImpl(CLASSNAME) void CLASSNAME::QvMessageBusSlotIdentifier(QvMessageBusSlotSig)

#define MBShowDefaultImpl                                                                                                                            \
    case SHOW_WINDOWS:                                                                                                                               \
        this->setWindowOpacity(1);                                                                                                                   \
        break;

#define MBHideDefaultImpl                                                                                                                            \
    case HIDE_WINDOWS:                                                                                                                               \
        this->setWindowOpacity(0);                                                                                                                   \
        break;

// Runtime language switching is retired. This source-compatibility case is a
// no-op, and both the connection boundary and EmitGlobalSignal reject it.
#define MBRetranslateDefaultImpl                                                                                                                     \
    case RETRANSLATE: break;

#define MBUpdateColorSchemeDefaultImpl                                                                                                               \
    case UPDATE_COLORSCHEME: this->updateColorScheme(); break;

namespace Qv2ray::ui::messaging
{
    Q_NAMESPACE
    enum QvMBMessage
    {
        /// Show all windows.
        SHOW_WINDOWS,
        /// Hide all windows.
        HIDE_WINDOWS,
        /// Reserved legacy value. Runtime retranslation is not dispatched.
        RETRANSLATE,
        /// Change Color Scheme
        UPDATE_COLORSCHEME
    };
    Q_ENUM_NS(QvMBMessage)
    //
    class QvMessageBusObject : public QObject
    {
        Q_OBJECT
      public:
        explicit QvMessageBusObject(){};
        void EmitGlobalSignal(const QvMBMessage &msg)
        {
            if (msg == RETRANSLATE)
                return;
            emit QvSendMessage(msg);
        }
      signals:
        void QvSendMessage(const QvMBMessage &msg);
        // private slots:
        //    void on_QvMessageReceived(QvMessage msg);
    };

    inline QvMessageBusObject UIMessageBus = QvMessageBusObject();
} // namespace Qv2ray::ui::messaging

using namespace Qv2ray::ui::messaging;
