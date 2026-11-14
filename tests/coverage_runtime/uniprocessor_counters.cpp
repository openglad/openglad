// The one-CPU runtime of a coverage build whose instrumented objects were
// assembled with -Wa,-momit-lock-prefix=yes (CMakeLists.txt,
// og_instrument_for_coverage; scripts/coverage/README.md, "Counter races").
//
// This file is compiled WITHOUT instrumentation and WITH its lock prefixes.
//
// A stripped counter update is one `addq $1, counter(%rip)`. An instruction is
// never interrupted halfway, so on one CPU no thread can observe or overwrite
// a half-finished update and every count is exact. Across two CPUs the same
// instruction loses updates, and it would also break every std::atomic the
// stripped objects compile. So the process is confined to one CPU here, before
// main and before any second thread can exist, and nothing in the process may
// widen that set afterwards: the three libc entry points that could are
// replaced below with definitions that end the process.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

extern "C" {
// The symbol every stripped object refers to (uniprocessor_counters.h).
extern const char og_stripped_lock_prefixes_need_the_one_cpu_runtime;
const char og_stripped_lock_prefixes_need_the_one_cpu_runtime = 1;

// The CPU this process was confined to; read by tests/unit/test_coverage_counters.cpp.
int og_coverage_one_cpu(void);
}

namespace
{
int g_confined_cpu = -1;

[[noreturn]] void refuse(const char* what)
{
    std::fprintf(stderr,
                 "coverage one-CPU confinement: %s. This build counts coverage "
                 "with lock-free updates that are exact on one CPU only "
                 "(scripts/coverage/README.md, \"Counter races\").\n",
                 what);
    std::fflush(stderr);
    std::abort();
}

int live_thread_count()
{
    DIR* tasks = opendir("/proc/self/task");
    if (tasks == nullptr)
        refuse("cannot read /proc/self/task to count threads");
    int count = 0;
    while (const dirent* entry = readdir(tasks))
        if (entry->d_name[0] != '.')
            ++count;
    closedir(tasks);
    return count;
}

// The kernel calls directly: the libc wrappers for the setters are the ones
// this file replaces.
long kernel_get_affinity(cpu_set_t* set)
{
    return syscall(SYS_sched_getaffinity, 0, sizeof(*set), set);
}

long kernel_set_affinity(const cpu_set_t* set)
{
    return syscall(SYS_sched_setaffinity, 0, sizeof(*set), set);
}

// 101 is the first priority a program may use; it runs before every ordinary
// static initializer, so before anything of ours can start a thread.
__attribute__((constructor(101))) void confine_the_process_to_one_cpu()
{
    if (live_thread_count() != 1)
        refuse("a second thread existed before the process was confined");

    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (kernel_get_affinity(&allowed) < 0)
        refuse("cannot read the allowed CPU set");

    // The CPU the process is on now if it may stay there (CTest's
    // PROCESSOR_AFFINITY hands each test its own), else the first allowed.
    const std::size_t limit = static_cast<std::size_t>(CPU_SETSIZE);
    std::size_t cpu = limit;
    const int current = sched_getcpu();
    if (current >= 0 && static_cast<std::size_t>(current) < limit
        && CPU_ISSET(static_cast<std::size_t>(current), &allowed))
        cpu = static_cast<std::size_t>(current);
    for (std::size_t candidate = 0; cpu == limit && candidate < limit; ++candidate)
        if (CPU_ISSET(candidate, &allowed))
            cpu = candidate;
    if (cpu == limit)
        refuse("the allowed CPU set is empty");

    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET(cpu, &one);
    if (kernel_set_affinity(&one) != 0)
        refuse("cannot narrow the allowed CPU set to one CPU");

    cpu_set_t now;
    CPU_ZERO(&now);
    if (kernel_get_affinity(&now) < 0 || CPU_COUNT(&now) != 1 || !CPU_ISSET(cpu, &now))
        refuse("the allowed CPU set is not one CPU after narrowing it");
    g_confined_cpu = static_cast<int>(cpu);
}
} // namespace

extern "C" {

int og_coverage_one_cpu(void)
{
    return g_confined_cpu;
}

// Defined in the executable, these win over libc's for the executable and for
// every shared library it loads. A thread created later inherits the one-CPU
// set; these are the only ways it could leave it.
int sched_setaffinity(pid_t, size_t, const cpu_set_t*) noexcept
{
    refuse("sched_setaffinity was called");
}

int pthread_setaffinity_np(pthread_t, size_t, const cpu_set_t*) noexcept
{
    refuse("pthread_setaffinity_np was called");
}

int pthread_attr_setaffinity_np(pthread_attr_t*, size_t, const cpu_set_t*) noexcept
{
    refuse("pthread_attr_setaffinity_np was called");
}

} // extern "C"
