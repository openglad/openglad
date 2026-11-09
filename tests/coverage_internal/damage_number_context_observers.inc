std::size_t DamageNumberRenderContext::state_count() const noexcept
{
    std::size_t count = 0u;
    for (const auto& owner_state : state_by_owner_)
        count += owner_state.second.size();
    return count;
}
