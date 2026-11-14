extern std::mutex& get_allbuttons_mutex();
namespace
{
struct AllButtonsLock final
{
    explicit AllButtonsLock(std::mutex& mutex) : lock_(mutex) {}
    AllButtonsLock(const AllButtonsLock&) = delete;
    AllButtonsLock& operator=(const AllButtonsLock&) = delete;
private:
    std::lock_guard<std::mutex> lock_;
};
} // namespace
