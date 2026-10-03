#include "base/Qv2rayBaseApplication.hpp"
using namespace Qv2ray;
inline int fakeArgc = 1;
inline char fakeArgv0[] = "qv2ray-test";
inline char *fakeArgv[]{ fakeArgv0, nullptr };

class QvTestApplication
    : public QCoreApplication
    , public Qv2rayApplicationInterface
{
  public:
    explicit QvTestApplication() : QCoreApplication(fakeArgc, fakeArgv), Qv2rayApplicationInterface(){};
    virtual void MessageBoxWarn(QWidget *, const QString &, const QString &) override{};
    virtual void MessageBoxInfo(QWidget *, const QString &, const QString &) override{};
    virtual MessageOpt MessageBoxAsk(QWidget *, const QString &, const QString &, const QList<MessageOpt> &) override
    {
        return {};
    }
    virtual void OpenURL(const QString &) override{};
};
