#pragma once

#include <optional>
#include <string_view>

#include "hexengine/backend/process_backend.hpp"
#include "hexengine/engine/symbol_repository.hpp"

namespace hexengine::engine {

class AddressExpressionParser;

class AddressResolver {
public:
    AddressResolver(const backend::IProcessBackend& process, const SymbolRepository& symbols);

    [[nodiscard]] core::Address resolve(std::string_view expression) const;

private:
    friend class AddressExpressionParser;

    [[nodiscard]] core::Address readPointer(core::Address address) const;
    [[nodiscard]] std::optional<core::Address> tryResolveToken(std::string_view token) const;
    [[nodiscard]] core::Address resolveToken(std::string_view token) const;

    const backend::IProcessBackend& process_;
    const SymbolRepository& symbols_;
};

}  // namespace hexengine::engine
