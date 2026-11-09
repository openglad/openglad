// Deliberate defects verify that each CI sanitizer detects its promised class.
#include <atomic>
#include <climits>
#include <cstring>
#include <thread>

int global_value[1] = {7};
__attribute__((noinline)) int* global_pointer() { return global_value; }

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    volatile int index = argc - 1;
    if (std::strcmp(argv[1], "global") == 0)
        return global_pointer()[index];
    if (std::strcmp(argv[1], "heap") == 0)
    {
        int* values = new int[1]{7};
        const int result = values[index];
        delete[] values;
        return result;
    }
    if (std::strcmp(argv[1], "integer") == 0)
    {
        volatile int maximum = INT_MAX;
        return maximum + index;
    }
    if (std::strcmp(argv[1], "float") == 0)
    {
        volatile double oversized = 1e300;
        return static_cast<int>(oversized);
    }
    if (std::strcmp(argv[1], "race") == 0)
    {
        std::atomic<int> ready{0};
        std::atomic<bool> start{false};
        int shared_value = 0;
        auto writer = [&](int value) {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int iteration = 0; iteration < 10000; ++iteration)
                shared_value = value;
        };
        std::thread first(writer, 1);
        std::thread second(writer, 2);
        while (ready.load(std::memory_order_acquire) != 2)
            std::this_thread::yield();
        start.store(true, std::memory_order_release);
        first.join();
        second.join();
        return shared_value == 0 ? 1 : 0;
    }
    return 3;
}
