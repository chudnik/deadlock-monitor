#include "monitor.hpp"
#include "resource_utils.hpp"

#include <cassert>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <thread>

namespace
{
    bool throwsInvalidConstructor()
    {
        try
        {
            ResourceMonitor monitor({1}, {{2}});
        }
        catch (const std::invalid_argument &)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }

        return false;
    }

    bool throwsFinishBeforeNeedIsZero()
    {
        try
        {
            ResourceMonitor monitor({3}, {{2}});
            monitor.request(0, {1});
            monitor.finish(0);
        }
        catch (const std::logic_error &)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }

        return false;
    }

    void testRequestReleaseAndFinish()
    {
        ResourceMonitor monitor({3, 2}, {{2, 1}});

        assert(monitor.checkInvariants());
        assert(monitor.request(0, {1, 1}));

        StateSnapshot snapshot = monitor.snapshot();
        assert((snapshot.available == ResourceVector{2, 1}));
        assert((snapshot.allocation[0] == ResourceVector{1, 1}));
        assert((snapshot.need[0] == ResourceVector{1, 0}));
        assert(monitor.checkInvariants());

        monitor.release(0, {1, 0});
        snapshot = monitor.snapshot();
        assert((snapshot.available == ResourceVector{3, 1}));
        assert((snapshot.allocation[0] == ResourceVector{0, 1}));
        assert((snapshot.need[0] == ResourceVector{2, 0}));
        assert(monitor.checkInvariants());

        assert(monitor.request(0, {2, 0}));
        monitor.finish(0);
        snapshot = monitor.snapshot();
        assert((snapshot.available == ResourceVector{3, 2}));
        assert(resource_utils::isZeroVector(snapshot.allocation[0]));
        assert(resource_utils::isZeroVector(snapshot.need[0]));
        assert(snapshot.finished[0] != 0);
        assert(monitor.checkInvariants());
    }

    void testShutdownWakesWaitingThread()
    {
        ResourceMonitor monitor({1}, {{1}, {1}});
        assert(monitor.request(0, {1}));

        bool result = true;
        std::thread waiting_thread([&]
                                   { result = monitor.request(1, {1}); });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        monitor.shutdown();
        waiting_thread.join();

        assert(!result);
        assert(monitor.checkInvariants());
    }
}

int main()
{
    assert(throwsInvalidConstructor());
    assert(throwsFinishBeforeNeedIsZero());
    testRequestReleaseAndFinish();
    testShutdownWakesWaitingThread();
    return 0;
}
