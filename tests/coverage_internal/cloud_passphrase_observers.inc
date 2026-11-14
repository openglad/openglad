namespace {
std::vector<std::string>& cloud_passphrase_queue_ref()
{
    static std::vector<std::string> queue;
    return queue;
}
} // namespace

void picker_testing_cloud_passphrase_queue_clear()
{
    cloud_passphrase_queue_ref().clear();
}

void picker_testing_cloud_passphrase_queue_push(const char* value)
{
    cloud_passphrase_queue_ref().push_back(
        value != nullptr ? std::string(value) : std::string());
}

bool picker_testing_cloud_passphrase_queue_pop(std::string& out)
{
    auto& queue = cloud_passphrase_queue_ref();
    if (queue.empty())
        return false;
    out = queue.front();
    queue.erase(queue.begin());
    return true;
}
