#include "hexengine/engine/win32_engine_factory.hpp"

namespace hexengine::engine {

Win32EngineFactory::Win32EngineFactory(
    backends::win32::Win32ProcessBackend::AccessMask access) noexcept
    : access_(access) {
}

std::unique_ptr<EngineSession> Win32EngineFactory::open(core::ProcessId pid) const {
    return std::make_unique<EngineSession>(
        backends::win32::Win32ProcessBackend::open(pid, access_));
}

}  // namespace hexengine::engine
