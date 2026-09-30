#pragma once
#include "base/Qv2rayBase.hpp"
#include "v2ray_api.grpc.pb.h"

#include <grpc++/grpc++.h>

// Check 30 times before telling user that API has failed.
constexpr auto QV2RAY_API_CALL_FAILEDCHECK_THRESHOLD = 30;

namespace Qv2ray::core::kernel
{
    struct APIConfigObject
    {
        QString protocol;
        StatisticsType type;
    };

    typedef std::map<QString, APIConfigObject> QvAPITagProtocolConfig;
    typedef std::map<StatisticsType, QStringList> QvAPIDataTypeConfig;

    class APIWorkerBackend;

    class APIWorker : public QObject
    {
        Q_OBJECT

      public:
        APIWorker();
        ~APIWorker() override;
        void StartAPI(const QMap<bool, QMap<QString, QString>> &tagProtocolPair);
        void StopAPI();

      signals:
        void onAPIDataReady(const QMap<StatisticsType, QvStatsSpeed> &data);
        void OnAPIErrored(const QString &err);

      private:
        QThread *workThread = nullptr;
        APIWorkerBackend *backend = nullptr;
    };
} // namespace Qv2ray::core::kernel

using namespace Qv2ray::core::kernel;
