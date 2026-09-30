#include "src/core/kernel/KernelProcessLifecycle.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QThread>

#include <cstdio>

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

namespace
{
    QString fixtureExecutable;

    class ScopedEnvironment
    {
      public:
        ScopedEnvironment(const char *name, const QByteArray &value) : name(name), wasSet(qEnvironmentVariableIsSet(name)), previous(qgetenv(name))
        {
            qputenv(name, value);
        }

        ~ScopedEnvironment()
        {
            if (wasSet)
                qputenv(name.constData(), previous);
            else
                qunsetenv(name.constData());
        }

      private:
        QByteArray name;
        bool wasSet;
        QByteArray previous;
    };
} // namespace

int main(int argc, char *argv[])
{
    const auto fixtureMode = qgetenv("QV2RAY_PROCESS_FIXTURE_MODE");
    if (!fixtureMode.isEmpty())
    {
        if (fixtureMode == "short")
        {
            QThread::msleep(100);
            return 0;
        }
        if (fixtureMode == "sleep")
        {
            QThread::sleep(30);
            return 0;
        }
        if (fixtureMode == "stderr-sleep")
        {
            std::fputs("fixture stderr\n", stderr);
            std::fflush(stderr);
            QThread::sleep(30);
            return 0;
        }
        return 9;
    }

    QCoreApplication app(argc, argv);
    fixtureExecutable = QCoreApplication::applicationFilePath();
    return Catch::Session().run(argc, argv);
}

TEST_CASE("Kernel process lifecycle is bounded")
{
    using namespace Qv2ray::core::kernel;

    SECTION("missing executable is rejected without leaving a process behind")
    {
        QProcess process;
        process.setProgram(QDir::temp().filePath("qv2ray-z-definitely-missing-kernel"));
        process.start();

        QString error;
        REQUIRE_FALSE(StartProcessBounded(process, 250, &error));
        REQUIRE(process.state() == QProcess::NotRunning);
        REQUIRE_FALSE(error.isEmpty());
    }

    SECTION("a normally starting child can finish within the bound")
    {
        ScopedEnvironment environment("QV2RAY_PROCESS_FIXTURE_MODE", "short");
        QProcess process;
        process.setProgram(fixtureExecutable);
        process.start();

        QString error;
        REQUIRE(StartProcessBounded(process, 2000, &error));
        REQUIRE(WaitForProcessFinishedBounded(process, 2000, 1000, &error));
        REQUIRE(process.state() == QProcess::NotRunning);
        REQUIRE(process.exitStatus() == QProcess::NormalExit);
        REQUIRE(process.exitCode() == 0);
    }

    SECTION("finish timeout kills a stuck child and reports the timeout")
    {
        ScopedEnvironment environment("QV2RAY_PROCESS_FIXTURE_MODE", "stderr-sleep");
        QProcess process;
        process.setProgram(fixtureExecutable);
        process.start();

        QString error;
        REQUIRE(StartProcessBounded(process, 2000, &error));
        REQUIRE_FALSE(WaitForProcessFinishedBounded(process, 25, 2000, &error));
        REQUIRE(process.state() == QProcess::NotRunning);
        REQUIRE(error.contains("timed out", Qt::CaseInsensitive));
        REQUIRE(error.contains("fixture stderr", Qt::CaseInsensitive));
    }

    SECTION("stop never waits indefinitely for a long-running child")
    {
        ScopedEnvironment environment("QV2RAY_PROCESS_FIXTURE_MODE", "sleep");
        QProcess process;
        process.setProgram(fixtureExecutable);
        process.start();

        QString error;
        REQUIRE(StartProcessBounded(process, 2000, &error));
        const auto result = StopProcessBounded(process, 0, 2000, &error);
        REQUIRE(result != ProcessStopResult::Failed);
        REQUIRE(process.state() == QProcess::NotRunning);
    }
}
