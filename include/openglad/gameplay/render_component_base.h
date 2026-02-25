#pragma once

namespace og::gameplay {

class IRenderComponent {
public:
    virtual ~IRenderComponent() = default;
    // Gameplay never calls methods on this. It just owns the lifetime.
    // The interface layer downcasts to the concrete type when rendering.
};

} // namespace og::gameplay
