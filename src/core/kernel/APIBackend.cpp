#include "APIBackend.hpp"

#include "v2ray_api.pb.h"

#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <mutex>

using namespace v2ray::core::app::stats::command;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

#define QV_MODULE_NAME "gRPCBackend"

namespace
{
    constexpr int API_POLL_INTERVAL_MS = 1000;
    constexpr int API_CALL_TIMEOUT_MS = 1500;
    constexpr int API_THREAD_SHUTDOWN_TIMEOUT_MS = 5000;
} // namespace

namespace Qv2ray::core::kernel
{
    constexpr auto Qv2ray_GRPC_ERROR_RETCODE = -1;
    static QvAPIDataTypeConfig DefaultInboundAPIConfig{ { API_INBOUND, { "dokodemo-door", "http", "socks" } } };
    static QvAPIDataTypeConfig DefaultOutboundAPIConfig{ { API_OUTBOUND_PROXY,
                                                           { "dns", "http", "mtproto", "shadowsocks", "socks", "vmess", "vless", "trojan" } },
                                                         { API_OUTBOUND_DIRECT, { "freedom" } },
                                                         { API_OUTBOUND_BLACKHOLE, { "blackhole" } } };

    namespace
    {
        QvAPITagProtocolConfig BuildTagProtocolConfig(const QMap<bool, QMap<QString, QString>> &tagProtocolPair)
        {
            QvAPITagProtocolConfig result;
            for (const auto &key : tagProtocolPair.keys())
            {
                const auto config = key ? DefaultOutboundAPIConfig : DefaultInboundAPIConfig;
                for (const auto &[tag, protocol] : tagProtocolPair[key].toStdMap())
                {
                    for (const auto &[type, protocols] : config)
                    {
                        if (protocols.contains(protocol))
                            result[tag] = { protocol, type };
                    }
                }
            }
            return result;
        }
    } // namespace

    class APIWorkerBackend final : public QObject
    {
      public:
        explicit APIWorkerBackend(APIWorker *facade) : facade(facade)
        {
            pollTimer = new QTimer(this);
            pollTimer->setInterval(API_POLL_INTERVAL_MS);
            connect(pollTimer, &QTimer::timeout, this, [this]() { Poll(); });
        }

        void Start(QvAPITagProtocolConfig config, int statsPort)
        {
            Q_ASSERT(QThread::currentThread() == thread());

            pollTimer->stop();
            tagProtocolConfig = std::move(config);
            apiFailCounter = 0;

            const auto channelAddress = "127.0.0.1:" + QString::number(statsPort);
            LOG("gRPC Version: " + QString::fromStdString(grpc::Version()));
            grpcChannel = grpc::CreateChannel(channelAddress.toStdString(), grpc::InsecureChannelCredentials());
            v2ray::core::app::stats::command::StatsService service;
            statsServiceStub = service.NewStub(grpcChannel);

            stopRequested.store(false, std::memory_order_release);
            pollTimer->start();
        }

        void RequestStop()
        {
            stopRequested.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> lock(activeContextMutex);
            if (activeContext != nullptr)
                activeContext->TryCancel();
        }

        void Stop()
        {
            Q_ASSERT(QThread::currentThread() == thread());
            RequestStop();
            pollTimer->stop();
            tagProtocolConfig.clear();
            statsServiceStub.reset();
            grpcChannel.reset();
            apiFailCounter = 0;
        }

        void Shutdown()
        {
            Stop();
            QThread::currentThread()->quit();
        }

        void DetachFacade()
        {
            std::lock_guard<std::mutex> lock(facadeMutex);
            facade = nullptr;
        }

      private:
        qint64 CallStatsAPIByName(const QString &name)
        {
            if (stopRequested.load(std::memory_order_acquire) || !statsServiceStub)
                return Qv2ray_GRPC_ERROR_RETCODE;

            ClientContext context;
            context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(API_CALL_TIMEOUT_MS));

            {
                std::lock_guard<std::mutex> lock(activeContextMutex);
                if (stopRequested.load(std::memory_order_acquire))
                    return Qv2ray_GRPC_ERROR_RETCODE;
                activeContext = &context;
            }

            GetStatsRequest request;
            GetStatsResponse response;
            request.set_name(name.toStdString());
            request.set_reset(true);
            const auto status = statsServiceStub->GetStats(&context, request, &response);

            {
                std::lock_guard<std::mutex> lock(activeContextMutex);
                if (activeContext == &context)
                    activeContext = nullptr;
            }

            if (stopRequested.load(std::memory_order_acquire))
                return Qv2ray_GRPC_ERROR_RETCODE;

            if (!status.ok())
            {
                LOG("API call returns: " + QSTRN(status.error_code()) + " (" + QString::fromStdString(status.error_message()) + ")");
                return Qv2ray_GRPC_ERROR_RETCODE;
            }
            return response.stat().value();
        }

        void PublishData(const QMap<StatisticsType, QvStatsSpeed> &data)
        {
            std::lock_guard<std::mutex> lock(facadeMutex);
            if (facade != nullptr)
                emit facade->onAPIDataReady(data);
        }

        void PublishError(const QString &error)
        {
            std::lock_guard<std::mutex> lock(facadeMutex);
            if (facade != nullptr)
                emit facade->OnAPIErrored(error);
        }

        void Poll()
        {
            Q_ASSERT(QThread::currentThread() == thread());
            if (stopRequested.load(std::memory_order_acquire))
                return;

            if (apiFailCounter >= QV2RAY_API_CALL_FAILEDCHECK_THRESHOLD)
            {
                if (apiFailCounter == QV2RAY_API_CALL_FAILEDCHECK_THRESHOLD)
                {
                    LOG("API call failure threshold reached, cancelling further API calls.");
                    PublishError(APIWorker::tr("Failed to get statistics data, please check if V2Ray is running properly"));
                    ++apiFailCounter;
                }
                return;
            }

            QMap<StatisticsType, QvStatsSpeed> statsResult;
            bool hasError = false;
            for (const auto &[tag, config] : tagProtocolConfig)
            {
                if (stopRequested.load(std::memory_order_acquire))
                    return;

                const QString prefix = config.type == API_INBOUND ? "inbound" : "outbound";
                const auto valueUp = CallStatsAPIByName(prefix % ">>>" % tag % ">>>traffic>>>uplink");
                if (stopRequested.load(std::memory_order_acquire))
                    return;

                const auto valueDown = CallStatsAPIByName(prefix % ">>>" % tag % ">>>traffic>>>downlink");
                if (stopRequested.load(std::memory_order_acquire))
                    return;

                hasError = hasError || valueUp == Qv2ray_GRPC_ERROR_RETCODE || valueDown == Qv2ray_GRPC_ERROR_RETCODE;
                statsResult[config.type].first += std::max(valueUp, 0LL);
                statsResult[config.type].second += std::max(valueDown, 0LL);
            }

            apiFailCounter = hasError ? apiFailCounter + 1 : 0;
            PublishData(statsResult);
        }

        APIWorker *facade = nullptr;
        QTimer *pollTimer = nullptr;
        QvAPITagProtocolConfig tagProtocolConfig;
        int apiFailCounter = 0;
        std::atomic_bool stopRequested{ true };
        std::shared_ptr<::grpc::Channel> grpcChannel;
        std::unique_ptr<::v2ray::core::app::stats::command::StatsService::Stub> statsServiceStub;
        std::mutex activeContextMutex;
        ClientContext *activeContext = nullptr;
        std::mutex facadeMutex;
    };

    APIWorker::APIWorker()
    {
        workThread = new QThread();
        backend = new APIWorkerBackend(this);
        backend->moveToThread(workThread);
        connect(workThread, &QThread::finished, backend, &QObject::deleteLater);
        connect(workThread, &QThread::finished, workThread, &QObject::deleteLater);
        connect(workThread, &QThread::finished, [] { LOG("API thread stopped"); });
        workThread->start();
        DEBUG("API Worker initialised.");
    }

    APIWorker::~APIWorker()
    {
        if (backend == nullptr || workThread == nullptr)
            return;

        backend->RequestStop();
        backend->DetachFacade();
        QMetaObject::invokeMethod(backend, [backend = backend]() { backend->Shutdown(); }, Qt::QueuedConnection);

        if (workThread->wait(API_THREAD_SHUTDOWN_TIMEOUT_MS))
        {
            // The finished->deleteLater event targets the current thread and
            // cannot run while this destructor is blocked in wait(). Deleting
            // here removes that pending event safely.
            delete workThread;
        }
        else
        {
            // RequestStop() cancels the active gRPC context and every call has
            // a deadline. Do not reintroduce an unbounded wait here: the
            // queued shutdown remains responsible for finishing the detached
            // backend, and the thread deletes itself via finished->deleteLater.
            LOG("API thread did not stop within the shutdown deadline; detached shutdown will continue without facade callbacks.");
        }

        backend = nullptr;
        workThread = nullptr;
    }

    void APIWorker::StartAPI(const QMap<bool, QMap<QString, QString>> &tagProtocolPair)
    {
        if (backend == nullptr)
            return;

        auto config = BuildTagProtocolConfig(tagProtocolPair);
        const auto statsPort = GlobalConfig.kernelConfig.statsPort;
        QMetaObject::invokeMethod(
            backend, [backend = backend, config = std::move(config), statsPort]() mutable { backend->Start(std::move(config), statsPort); },
            Qt::QueuedConnection);
    }

    void APIWorker::StopAPI()
    {
        if (backend == nullptr)
            return;

        // Cancel an in-flight synchronous gRPC request immediately, then let
        // the worker thread serialize the remaining state cleanup.
        backend->RequestStop();
        QMetaObject::invokeMethod(backend, [backend = backend]() { backend->Stop(); }, Qt::QueuedConnection);
    }
} // namespace Qv2ray::core::kernel
