#include <catch2/catch2.hpp>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/ConsoleSink.h>

#include <utility>

namespace
{
    void FlushTestLogs()
    {
        for (auto* logger : quill::Frontend::get_all_loggers())
        {
            logger->flush_log();
        }
    }

    [[maybe_unused]] const bool TestLoggerReady = []
    {
        quill::Backend::start();
        auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("server_tests_console");
        quill::Frontend::create_or_get_logger("root", std::move(sink),
            "%(time:<16) LOG_%(log_level:<11) %(message)", "%H:%M:%S.%Qms",
            quill::Timezone::LocalTime, quill::ClockSourceType::System);
        return true;
    }();

    class QuillFlushListener final : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testCasePartialEnded(const Catch::TestCaseStats&, uint64_t) override
        {
            FlushTestLogs();
        }

        void testCaseEnded(const Catch::TestCaseStats&) override
        {
            FlushTestLogs();
        }

        void testRunEnded(const Catch::TestRunStats&) override
        {
            FlushTestLogs();
        }
    };
}

CATCH_REGISTER_LISTENER(QuillFlushListener)
