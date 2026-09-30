#pragma once
#include "LatencyTest.hpp"

#include <QThread>
#include <atomic>
#include <curl/curl.h>
#include <mutex>
#include <unordered_set>

namespace uvw
{
    class Loop;
    class TimerHandle;
} // namespace uvw
namespace Qv2ray::components::latency
{
    class LatencyTestThread : public QThread
    {
        Q_OBJECT
      public:
        explicit LatencyTestThread(QObject *parent = nullptr);
        void stopLatencyTest()
        {
            isStop.store(true, std::memory_order_release);
        }
        void prepareLatencyTest()
        {
            isStop.store(false, std::memory_order_release);
        }
        void pushRequest(const QList<ConnectionId> &ids, int totalTestCount, Qv2rayLatencyTestingMethod method);
        void pushRequest(const ConnectionId &id, int totalTestCount, Qv2rayLatencyTestingMethod method);

      protected:
        void run() override;

      private:
        bool shouldStop() const
        {
            return isStop.load(std::memory_order_acquire);
        }

        struct CURLGlobal
        {
            CURLGlobal()
            {
                curl_global_init(CURL_GLOBAL_ALL);
            }
            ~CURLGlobal()
            {
                curl_global_cleanup();
            }
        };
        std::shared_ptr<uvw::Loop> loop;
        CURLGlobal curlGlobal;
        std::atomic_bool isStop{ false };
        std::shared_ptr<uvw::TimerHandle> stopTimer;
        std::vector<LatencyTestRequest> requests;
        std::mutex m;

        // static LatencyTestResult TestLatency_p(const ConnectionId &id, const int count);
    };

} // namespace Qv2ray::components::latency
