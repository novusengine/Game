#include <Game-Lib/Rendering/NsightGPUTrace.h>

#include <catch2/catch2.hpp>

TEST_CASE("Nsight GPU Trace remains unavailable until explicitly initialized")
{
    NsightGPUTrace capture;

    CHECK_FALSE(capture.IsAvailable());
    CHECK_FALSE(capture.IsReady());
    CHECK(capture.GetState() == NsightGPUTrace::State::Unavailable);
}

TEST_CASE("Nsight GPU Trace exposes stable automation state names")
{
    CHECK(std::string(NsightGPUTrace::StateName(NsightGPUTrace::State::Unavailable)) == "unavailable");
    CHECK(std::string(NsightGPUTrace::StateName(NsightGPUTrace::State::Inactive)) == "inactive");
    CHECK(std::string(NsightGPUTrace::StateName(NsightGPUTrace::State::Ready)) == "ready");
    CHECK(std::string(NsightGPUTrace::StateName(NsightGPUTrace::State::Capturing)) == "capturing");
    CHECK(std::string(NsightGPUTrace::StateName(NsightGPUTrace::State::Draining)) == "draining");
    CHECK(std::string(NsightGPUTrace::StateName(NsightGPUTrace::State::Errored)) == "errored");
}
