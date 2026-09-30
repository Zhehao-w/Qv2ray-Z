#include "src/components/latency/LatencyTestThread.hpp"
#include "src/core/kernel/APIBackend.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("API worker shutdown stays bounded")
{
    QElapsedTimer elapsed;
    elapsed.start();

    {
        Qv2ray::core::kernel::APIWorker worker;
        QMap<bool, QMap<QString, QString>> tags;
        tags[false]["threading-test"] = "http";
        worker.StartAPI(tags, 65535);
        QThread::msleep(25);
        worker.StopAPI();
    }

    REQUIRE(elapsed.elapsed() < 2000);
}

TEST_CASE("Latency worker can stop and restart without a stale stop request")
{
    using Qv2ray::components::latency::LatencyTestThread;

    LatencyTestThread worker;
    worker.prepareLatencyTest();
    worker.start();
    worker.stopLatencyTest();
    REQUIRE(worker.wait(2000));

    worker.prepareLatencyTest();
    worker.start();
    QThread::msleep(750);
    REQUIRE(worker.isRunning());

    worker.stopLatencyTest();
    REQUIRE(worker.wait(2000));
}
