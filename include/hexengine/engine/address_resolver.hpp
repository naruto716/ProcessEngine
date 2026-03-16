#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "hexengine/backend/process_backend.hpp"

namespace hexengine::engine {

class AddressExpressionParser;

class AddressResolver {
public:
    using NameResolver = std::function<std::optional<core::Address>(std::string_view)>;

    AddressResolver(const backend::IProcessBackend& process, NameResolver nameResolver = {});

    [[nodiscard]] core::Address resolve(std::string_view expression) const;

private:
    friend class AddressExpressionParser;

    [[nodiscard]] core::Address readPointer(core::Address address) const;
    [[nodiscard]] std::optional<core::Address> tryResolveToken(std::string_view token) const;
    [[nodiscard]] core::Address resolveToken(std::string_view token) const;

    const backend::IProcessBackend& process_;
    NameResolver nameResolver_;
};

}  // namespace hexengine::engine
