// The counter-update rule of a coverage build, from inside a test process.
//
// Where the build strips the lock prefix from its counter updates
// (OG_COVERAGE_ONE_CPU_COUNTERS, CMakeLists.txt), every process that holds a
// stripped object must run on exactly one CPU for its whole life: the counts
// are exact on one CPU and silently wrong on two. Everywhere else the lock
// prefix is intact, and a process must keep the CPUs it was given: a
// confinement that leaked into another build would only make it slower.
//
// Each test states both halves and asserts the half this build is.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(OG_COVERAGE_ONE_CPU_COUNTERS)
extern "C" int og_coverage_one_cpu(void);
#else
// Absent from every build that keeps its lock prefixes; weak so that the
// absence is something a test can read.
extern "C" int og_coverage_one_cpu(void) __attribute__((weak));
#endif

#if defined(__linux__)
namespace
{
constexpr const char* kProbeFileVariable = "OG_COVERAGE_CPU_PROBE_FILE";
constexpr const char* kConfinementTest =
    "CoverageCounters.cpu_confinement_follows_the_counter_mode";

// The kernel's own answer: in a confined build the libc setters are replaced.
int allowed_cpu_count()
{
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (syscall(SYS_sched_getaffinity, 0, sizeof(allowed), &allowed) < 0)
        return -1;
    return CPU_COUNT(&allowed);
}

#if defined(OG_COVERAGE_ONE_CPU_COUNTERS)
bool the_only_allowed_cpu_is(int cpu)
{
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (syscall(SYS_sched_getaffinity, 0, sizeof(allowed), &allowed) < 0)
        return false;
    if (cpu < 0 || cpu >= CPU_SETSIZE || CPU_COUNT(&allowed) != 1)
        return false;
    const std::size_t index = static_cast<std::size_t>(cpu);
    return CPU_ISSET(index, &allowed);
}
#endif

int read_count(const std::filesystem::path& path)
{
    std::ifstream in(path);
    int value = -1;
    in >> value;
    return in ? value : -1;
}
} // namespace
#endif

TEST(CoverageCounters, cpu_confinement_follows_the_counter_mode)
{
#if defined(OG_COVERAGE_ONE_CPU_COUNTERS)
    const int cpu = og_coverage_one_cpu();
    EXPECT_TRUE(the_only_allowed_cpu_is(cpu))
        << "stripped lock prefixes, but the process may run on "
        << allowed_cpu_count() << " CPU(s); the runtime reports CPU " << cpu;
#else
    EXPECT_EQ(nullptr, &og_coverage_one_cpu)
        << "the one-CPU runtime is linked into a build that keeps its lock prefixes";
#endif

#if defined(__linux__)
    // The child of a_process_started_on_every_cpu_follows_the_counter_mode
    // reports what it was left with.
    if (const char* probe = std::getenv(kProbeFileVariable))
    {
        std::ofstream out(probe);
        out << allowed_cpu_count() << '\n';
        ASSERT_TRUE(out.good()) << probe;
    }
#endif
}

#if defined(__linux__)
TEST(CoverageCounters, a_process_started_on_every_cpu_follows_the_counter_mode)
{
    // Whatever set this process inherited (CTest may already have handed it a
    // single CPU), start the same executable again with EVERY CPU the machine
    // will give it and read back what its own startup left.
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path()
        / ("og-coverage-counters-" + std::to_string(getpid()));
    std::filesystem::create_directories(directory);
    const std::filesystem::path given = directory / "given";
    const std::filesystem::path kept = directory / "kept";

    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0)
    {
        cpu_set_t every;
        CPU_ZERO(&every);
        for (std::size_t cpu = 0; cpu < static_cast<std::size_t>(CPU_SETSIZE); ++cpu)
            CPU_SET(cpu, &every);
        if (syscall(SYS_sched_setaffinity, 0, sizeof(every), &every) != 0)
            _exit(101);
        {
            std::ofstream out(given);
            out << allowed_cpu_count() << '\n';
            if (!out.good())
                _exit(102);
        }
        // The child selects one test by name: it must not inherit a shard.
        unsetenv("GTEST_TOTAL_SHARDS");
        unsetenv("GTEST_SHARD_INDEX");
        unsetenv("GTEST_SHARD_STATUS_FILE");
        setenv(kProbeFileVariable, kept.c_str(), 1);
        const std::string filter = std::string("--gtest_filter=") + kConfinementTest;
        char program[] = "/proc/self/exe";
        std::string argument = filter;
        char* const arguments[] = {program, argument.data(), nullptr};
        execv(program, arguments);
        _exit(103);
    }

    int status = 0;
    ASSERT_EQ(child, waitpid(child, &status, 0));
    ASSERT_TRUE(WIFEXITED(status)) << "status " << status;
    ASSERT_EQ(0, WEXITSTATUS(status))
        << "the child's own confinement test failed (101-103: it never ran)";

    const int given_cpus = read_count(given);
    const int kept_cpus = read_count(kept);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);

#if defined(OG_COVERAGE_ONE_CPU_COUNTERS)
    ASSERT_GE(given_cpus, 2)
        << "this proof needs a machine with two CPUs to hand the child";
    EXPECT_EQ(1, kept_cpus)
        << "a process holding stripped objects started on " << given_cpus
        << " CPUs and stayed on " << kept_cpus;
#else
    ASSERT_GE(given_cpus, 1);
    EXPECT_EQ(given_cpus, kept_cpus)
        << "a build that keeps its lock prefixes narrowed its own CPU set";
#endif
}

TEST(CoverageCounters, widening_the_cpu_set_follows_the_counter_mode)
{
    cpu_set_t current;
    CPU_ZERO(&current);
    ASSERT_EQ(0, sched_getaffinity(0, sizeof(current), &current));
    pthread_attr_t attributes;
    ASSERT_EQ(0, pthread_attr_init(&attributes));

#if defined(OG_COVERAGE_ONE_CPU_COUNTERS)
    // Even a call that would change nothing ends the process: nothing in it
    // may hold the means to leave the one CPU.
    EXPECT_DEATH((void)sched_setaffinity(0, sizeof(current), &current),
                 "sched_setaffinity was called");
    EXPECT_DEATH((void)pthread_setaffinity_np(pthread_self(), sizeof(current), &current),
                 "pthread_setaffinity_np was called");
    EXPECT_DEATH(
        (void)pthread_attr_setaffinity_np(&attributes, sizeof(current), &current),
        "pthread_attr_setaffinity_np was called");
#else
    EXPECT_EQ(0, sched_setaffinity(0, sizeof(current), &current));
    EXPECT_EQ(0, pthread_setaffinity_np(pthread_self(), sizeof(current), &current));
    EXPECT_EQ(0, pthread_attr_setaffinity_np(&attributes, sizeof(current), &current));
#endif
    EXPECT_EQ(0, pthread_attr_destroy(&attributes));
}
#endif // __linux__
