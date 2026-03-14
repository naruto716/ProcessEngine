#pragma once

#include <memory>

#include "hexengine/backends/win32/win32_process_backend.hpp"
#include "hexengine/engine/engine_session.hpp"

namespace hexengine::engine {

class IEngineFactory {
public:
    virtual ~IEngineFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<EngineSession> open(core::ProcessId pid) const = 0;
    [[nodiscard]] virtual std::unique_ptr<EngineSession> attachCurrent() const = 0;
};

class Win32EngineFactory final : public IEngineFactory {
public:
    explicit Win32EngineFactory(
        backends::win32::Win32ProcessBackend::AccessMask access =
            backends::win32::Win32ProcessBackend::defaultAccess()) noexcept;

    [[nodiscard]] std::unique_ptr<EngineSession> open(core::ProcessId pid) const override;
    [[nodiscard]] std::unique_ptr<EngineSession> attachCurrent() const override;

private:
    backends::win32::Win32ProcessBackend::AccessMask access_;
};

}  // namespace hexengine::engine
