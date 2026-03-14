#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

#include "hexengine/backend/process_backend.hpp"
#include "hexengine/engine/symbol_repository.hpp"

namespace hexengine::engine {

class PointerExpressionParser;

class PointerResolver {
public:
    PointerResolver(const backend::IProcessBackend& process, const SymbolRepository& symbols);

    [[nodiscard]] core::Address resolve(core::Address base, std::span<const std::ptrdiff_t> offsets) const;
    [[nodiscard]] core::Address resolve(std::string_view expression) const;

    template <typename... Offsets>
    [[nodiscard]] core::Address resolve(core::Address base, Offsets... offsets) const {
        static_assert((std::is_integral_v<Offsets> && ...), "Pointer offsets must be integral");
        const std::array<std::ptrdiff_t, sizeof...(Offsets)> chain{
            static_cast<std::ptrdiff_t>(offsets)...,
        };
        return resolve(base, std::span<const std::ptrdiff_t>(chain));
    }

    template <typename T, typename... Offsets>
    [[nodiscard]] T read(core::Address base, Offsets... offsets) const {
        return process_.readValue<T>(resolve(base, offsets...));
    }

    template <typename T>
    [[nodiscard]] T read(std::string_view expression) const {
        return process_.readValue<T>(resolve(expression));
    }

    template <typename T, typename... Offsets>
    void write(core::Address base, const T& value, Offsets... offsets) const {
        process_.writeValue<T>(resolve(base, offsets...), value);
    }

    template <typename T>
    void write(std::string_view expression, const T& value) const {
        process_.writeValue<T>(resolve(expression), value);
    }

private:
    friend class PointerExpressionParser;

    [[nodiscard]] core::Address readPointer(core::Address address) const;
    [[nodiscard]] core::Address resolveToken(std::string_view token) const;

    const backend::IProcessBackend& process_;
    const SymbolRepository& symbols_;
};

}  // namespace hexengine::engine
