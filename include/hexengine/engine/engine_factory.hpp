#pragma once

#include <memory>

#include "hexengine/engine/engine_session.hpp"

namespace hexengine::engine {

class IEngineFactory {
public:
    virtual ~IEngineFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<EngineSession> open(core::ProcessId pid) const = 0;
};

}  // namespace hexengine::engine
