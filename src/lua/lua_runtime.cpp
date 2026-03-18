#include "hexengine/lua/lua_runtime.hpp"

#include <Windows.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "hexengine/core/memory_types.hpp"
#include "hexengine/engine/assembly_script.hpp"
#include "hexengine/engine/engine_session.hpp"
#include "hexengine/engine/script_context.hpp"

namespace hexengine::lua {
namespace {

constexpr std::string_view kGlobalScriptContextId = "__lua.global__";
constexpr char kStringListMetatable[] = "hexengine.lua.StringList";
constexpr char kTimerMetatable[] = "hexengine.lua.Timer";

std::string utf16ToUtf8(std::u16string_view text) {
    if (text.empty()) {
        return {};
    }

    const auto wide = reinterpret_cast<LPCWCH>(text.data());
    const auto length = static_cast<int>(text.size());
    const auto size = WideCharToMultiByte(CP_UTF8, 0, wide, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw std::runtime_error("Failed to convert UTF-16 string to UTF-8");
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide, length, result.data(), size, nullptr, nullptr) != size) {
        throw std::runtime_error("Failed to convert UTF-16 string to UTF-8");
    }

    return result;
}

std::u16string utf8ToUtf16(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const auto size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (size <= 0) {
        throw std::runtime_error("Failed to convert UTF-8 string to UTF-16");
    }

    std::u16string result(static_cast<std::size_t>(size), u'\0');
    auto* output = reinterpret_cast<LPWSTR>(result.data());
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            output,
            size) != size) {
        throw std::runtime_error("Failed to convert UTF-8 string to UTF-16");
    }

    return result;
}

std::string formatHexAddress(core::Address address) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << address;
    return stream.str();
}

std::runtime_error makeLuaError(std::string_view message) {
    return std::runtime_error(std::string("LuaRuntime: ") + std::string(message));
}

}  // namespace

class LuaRuntime::Impl {
public:
    explicit Impl(engine::EngineSession& session);
    ~Impl();

    LuaResult runGlobal(std::string_view source, std::string_view chunkName);
    LuaResult runScript(std::string_view scriptId, std::string_view source, std::string_view chunkName);
    bool destroyScript(std::string_view scriptId);

private:
    struct ScriptEnvironmentRecord {
        int environmentRef = LUA_NOREF;
    };

    struct TimerRecord {
        std::uint64_t id = 0;
        std::optional<std::string> ownerScriptId;
        std::chrono::milliseconds interval{1000};
        std::chrono::steady_clock::time_point nextFire = std::chrono::steady_clock::now();
        bool enabled = true;
        bool oneshot = false;
        int callbackRef = LUA_NOREF;
    };

    struct ExecutionScope {
        explicit ExecutionScope(Impl& impl, std::optional<std::string> scriptId)
            : impl_(impl),
              previous_(impl.currentScriptId_) {
            impl_.currentScriptId_ = std::move(scriptId);
        }

        ~ExecutionScope() {
            impl_.currentScriptId_ = std::move(previous_);
        }

    private:
        Impl& impl_;
        std::optional<std::string> previous_;
    };

    struct StringListHandle {
        Impl* impl = nullptr;
        std::shared_ptr<std::vector<std::string>> values;
        bool destroyed = false;
    };

    struct TimerHandle {
        Impl* impl = nullptr;
        std::uint64_t id = 0;
    };

    template <typename Callback>
    auto submit(Callback&& callback);

    void initialize();
    void workerLoop();
    [[nodiscard]] std::optional<std::pair<std::chrono::steady_clock::time_point, std::uint64_t>> findNextDueTimer() const;
    void fireTimer(std::uint64_t id);
    void destroyTimer(std::uint64_t id);

    void registerGlobals();
    void registerGlobal(const char* name, lua_CFunction function);
    void registerStringListMetatable();
    void registerTimerMetatable();

    [[nodiscard]] int getOrCreateScriptEnv(std::string_view scriptId);
    void pushEnvironment(std::optional<std::string_view> scriptId);
    void pushTraceback();
    [[nodiscard]] LuaResult executeChunk(
        std::optional<std::string> scriptId,
        std::string_view source,
        std::string_view chunkName);

    [[nodiscard]] engine::ScriptContext& currentScriptContext();
    [[nodiscard]] std::string currentScriptContextId() const;
    [[nodiscard]] core::Address resolveAddressArgument(lua_State* lua, int index);
    [[nodiscard]] std::optional<core::Address> tryResolveAddressArgument(lua_State* lua, int index);
    [[nodiscard]] std::vector<std::byte> collectByteArguments(lua_State* lua, int firstIndex);
    [[nodiscard]] LuaValue extractValue(int index);

    static Impl& fromUpvalue(lua_State* lua);
    static StringListHandle& stringListHandle(lua_State* lua, int index);
    static TimerHandle& timerHandle(lua_State* lua, int index);

    void pushStringList(std::vector<core::Address> hits);
    void pushTimerHandle(std::uint64_t id);

    template <typename T>
    int pushReadInteger(lua_State* lua, bool isSigned = false);

    template <typename T>
    int writeValue(lua_State* lua);

    static int luaTraceback(lua_State* lua);
    static int luaGetAddress(lua_State* lua);
    static int luaGetAddressSafe(lua_State* lua);
    static int luaRegisterSymbol(lua_State* lua);
    static int luaUnregisterSymbol(lua_State* lua);
    static int luaAOBScan(lua_State* lua);
    static int luaAOBScanUnique(lua_State* lua);
    static int luaAOBScanModule(lua_State* lua);
    static int luaAOBScanModuleUnique(lua_State* lua);
    static int luaReadBytes(lua_State* lua);
    static int luaWriteBytes(lua_State* lua);
    static int luaReadSmallInteger(lua_State* lua);
    static int luaWriteSmallInteger(lua_State* lua);
    static int luaReadInteger(lua_State* lua);
    static int luaWriteInteger(lua_State* lua);
    static int luaReadQword(lua_State* lua);
    static int luaWriteQword(lua_State* lua);
    static int luaReadPointer(lua_State* lua);
    static int luaReadFloat(lua_State* lua);
    static int luaWriteFloat(lua_State* lua);
    static int luaReadDouble(lua_State* lua);
    static int luaWriteDouble(lua_State* lua);
    static int luaReadString(lua_State* lua);
    static int luaWriteString(lua_State* lua);
    static int luaFullAccess(lua_State* lua);
    static int luaAutoAssemble(lua_State* lua);
    static int luaCreateTimer(lua_State* lua);
    static int luaSleep(lua_State* lua);
    static int luaTargetIs64Bit(lua_State* lua);
    static int luaGetOpenedProcessID(lua_State* lua);

    static int luaStringListIndex(lua_State* lua);
    static int luaStringListLength(lua_State* lua);
    static int luaStringListDestroy(lua_State* lua);
    static int luaStringListGc(lua_State* lua);

    static int luaTimerIndex(lua_State* lua);
    static int luaTimerNewIndex(lua_State* lua);
    static int luaTimerDestroy(lua_State* lua);

    engine::EngineSession& session_;
    lua_State* L_ = nullptr;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
    std::deque<std::function<void()>> tasks_;
    std::unordered_map<std::string, ScriptEnvironmentRecord> scriptEnvironments_;
    std::unordered_map<std::uint64_t, TimerRecord> timers_;
    std::uint64_t nextTimerId_ = 1;
    std::optional<std::string> currentScriptId_;
};

LuaRuntime::Impl::Impl(engine::EngineSession& session)
    : session_(session) {
    const auto startup = std::make_shared<std::promise<void>>();
    auto ready = startup->get_future();
    worker_ = std::thread([this, startup]() {
        try {
            initialize();
            startup->set_value();
            workerLoop();
        } catch (...) {
            startup->set_exception(std::current_exception());
        }
    });
    ready.get();
}

LuaRuntime::Impl::~Impl() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

template <typename Callback>
auto LuaRuntime::Impl::submit(Callback&& callback) {
    using Return = std::invoke_result_t<Callback>;
    auto promise = std::make_shared<std::promise<Return>>();
    auto future = promise->get_future();

    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            throw makeLuaError("runtime is shutting down");
        }

        tasks_.emplace_back([promise, callback = std::forward<Callback>(callback)]() mutable {
            try {
                if constexpr (std::is_void_v<Return>) {
                    callback();
                    promise->set_value();
                } else {
                    promise->set_value(callback());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    }

    cv_.notify_all();
    return future.get();
}

LuaResult LuaRuntime::Impl::runGlobal(std::string_view source, std::string_view chunkName) {
    return submit([this, source = std::string(source), chunkName = std::string(chunkName)]() {
        return executeChunk(std::nullopt, source, chunkName);
    });
}

LuaResult LuaRuntime::Impl::runScript(std::string_view scriptId, std::string_view source, std::string_view chunkName) {
    return submit([this, scriptId = std::string(scriptId), source = std::string(source), chunkName = std::string(chunkName)]() {
        (void)session_.createScriptContext(scriptId);
        (void)getOrCreateScriptEnv(scriptId);
        return executeChunk(scriptId, source, chunkName);
    });
}

bool LuaRuntime::Impl::destroyScript(std::string_view scriptId) {
    return submit([this, scriptId = std::string(scriptId)]() {
        if (scriptId.empty() || scriptId == kGlobalScriptContextId) {
            return false;
        }

        for (auto iterator = timers_.begin(); iterator != timers_.end();) {
            if (iterator->second.ownerScriptId && *iterator->second.ownerScriptId == scriptId) {
                destroyTimer(iterator->first);
                iterator = timers_.begin();
                continue;
            }
            ++iterator;
        }

        if (const auto env = scriptEnvironments_.find(scriptId); env != scriptEnvironments_.end()) {
            luaL_unref(L_, LUA_REGISTRYINDEX, env->second.environmentRef);
            scriptEnvironments_.erase(env);
        }

        if (auto* script = session_.findScriptContext(scriptId)) {
            const auto locals = script->listLocalAllocations();
            for (const auto& allocation : locals) {
                (void)script->dealloc(allocation.name);
            }
        }

        return session_.destroyScriptContext(scriptId);
    });
}

void LuaRuntime::Impl::initialize() {
    L_ = luaL_newstate();
    if (L_ == nullptr) {
        throw makeLuaError("failed to create Lua state");
    }

    luaL_openlibs(L_);
    registerStringListMetatable();
    registerTimerMetatable();
    registerGlobals();

    const auto mainModule = session_.process().mainModule();
    lua_pushstring(L_, mainModule.name.c_str());
    lua_setglobal(L_, "process");
}

void LuaRuntime::Impl::workerLoop() {
    while (true) {
        std::function<void()> task;
        std::optional<std::uint64_t> dueTimer;

        {
            std::unique_lock lock(mutex_);
            while (true) {
                if (stopping_ && tasks_.empty()) {
                    break;
                }

                if (!tasks_.empty()) {
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                    break;
                }

                const auto nextDue = findNextDueTimer();
                if (nextDue && nextDue->first <= std::chrono::steady_clock::now()) {
                    dueTimer = nextDue->second;
                    break;
                }

                if (nextDue) {
                    cv_.wait_until(lock, nextDue->first, [this] {
                        return stopping_ || !tasks_.empty();
                    });
                } else {
                    cv_.wait(lock, [this] {
                        return stopping_ || !tasks_.empty();
                    });
                }
            }

            if (stopping_ && tasks_.empty() && !task && !dueTimer.has_value()) {
                break;
            }
        }

        if (task) {
            task();
            continue;
        }

        if (dueTimer.has_value()) {
            fireTimer(*dueTimer);
        }
    }

    for (auto& [_, timer] : timers_) {
        if (timer.callbackRef != LUA_NOREF) {
            luaL_unref(L_, LUA_REGISTRYINDEX, timer.callbackRef);
        }
    }
    timers_.clear();

    for (auto& [_, env] : scriptEnvironments_) {
        luaL_unref(L_, LUA_REGISTRYINDEX, env.environmentRef);
    }
    scriptEnvironments_.clear();

    lua_close(L_);
    L_ = nullptr;
}

std::optional<std::pair<std::chrono::steady_clock::time_point, std::uint64_t>> LuaRuntime::Impl::findNextDueTimer() const {
    std::optional<std::pair<std::chrono::steady_clock::time_point, std::uint64_t>> best;
    for (const auto& [id, timer] : timers_) {
        if (!timer.enabled) {
            continue;
        }

        if (!best.has_value() || timer.nextFire < best->first) {
            best = std::make_pair(timer.nextFire, id);
        }
    }

    return best;
}

void LuaRuntime::Impl::destroyTimer(std::uint64_t id) {
    const auto iterator = timers_.find(id);
    if (iterator == timers_.end()) {
        return;
    }

    if (iterator->second.callbackRef != LUA_NOREF) {
        luaL_unref(L_, LUA_REGISTRYINDEX, iterator->second.callbackRef);
    }
    timers_.erase(iterator);
}

void LuaRuntime::Impl::fireTimer(std::uint64_t id) {
    auto iterator = timers_.find(id);
    if (iterator == timers_.end() || !iterator->second.enabled) {
        return;
    }

    if (iterator->second.callbackRef == LUA_NOREF) {
        iterator->second.nextFire = std::chrono::steady_clock::now() + iterator->second.interval;
        return;
    }

    const auto ownerScriptId = iterator->second.ownerScriptId;
    const auto baseTop = lua_gettop(L_);
    pushTraceback();
    const auto tracebackIndex = lua_gettop(L_);
    lua_rawgeti(L_, LUA_REGISTRYINDEX, iterator->second.callbackRef);
    pushTimerHandle(id);

    ExecutionScope scope(*this, ownerScriptId);
    if (lua_pcall(L_, 1, 0, tracebackIndex) != LUA_OK) {
        const auto message = lua_tostring(L_, -1);
        std::clog << "hexengine lua timer error: " << (message != nullptr ? message : "(unknown)") << '\n';
        iterator = timers_.find(id);
        if (iterator != timers_.end()) {
            iterator->second.enabled = false;
        }
        lua_settop(L_, baseTop);
        return;
    }

    lua_settop(L_, baseTop);

    iterator = timers_.find(id);
    if (iterator == timers_.end() || !iterator->second.enabled) {
        return;
    }

    if (iterator->second.oneshot) {
        destroyTimer(id);
        return;
    }

    iterator->second.nextFire = std::chrono::steady_clock::now() + iterator->second.interval;
}

void LuaRuntime::Impl::registerGlobals() {
    registerGlobal("getAddress", &Impl::luaGetAddress);
    registerGlobal("getAddressSafe", &Impl::luaGetAddressSafe);
    registerGlobal("registerSymbol", &Impl::luaRegisterSymbol);
    registerGlobal("unregisterSymbol", &Impl::luaUnregisterSymbol);
    registerGlobal("AOBScan", &Impl::luaAOBScan);
    registerGlobal("AOBScanUnique", &Impl::luaAOBScanUnique);
    registerGlobal("AOBScanModule", &Impl::luaAOBScanModule);
    registerGlobal("AOBScanModuleUnique", &Impl::luaAOBScanModuleUnique);
    registerGlobal("readBytes", &Impl::luaReadBytes);
    registerGlobal("writeBytes", &Impl::luaWriteBytes);
    registerGlobal("readSmallInteger", &Impl::luaReadSmallInteger);
    registerGlobal("readWord", &Impl::luaReadSmallInteger);
    registerGlobal("writeSmallInteger", &Impl::luaWriteSmallInteger);
    registerGlobal("writeWord", &Impl::luaWriteSmallInteger);
    registerGlobal("readInteger", &Impl::luaReadInteger);
    registerGlobal("writeInteger", &Impl::luaWriteInteger);
    registerGlobal("readQword", &Impl::luaReadQword);
    registerGlobal("writeQword", &Impl::luaWriteQword);
    registerGlobal("readPointer", &Impl::luaReadPointer);
    registerGlobal("readFloat", &Impl::luaReadFloat);
    registerGlobal("writeFloat", &Impl::luaWriteFloat);
    registerGlobal("readDouble", &Impl::luaReadDouble);
    registerGlobal("writeDouble", &Impl::luaWriteDouble);
    registerGlobal("readString", &Impl::luaReadString);
    registerGlobal("writeString", &Impl::luaWriteString);
    registerGlobal("fullAccess", &Impl::luaFullAccess);
    registerGlobal("autoAssemble", &Impl::luaAutoAssemble);
    registerGlobal("createTimer", &Impl::luaCreateTimer);
    registerGlobal("sleep", &Impl::luaSleep);
    registerGlobal("targetIs64Bit", &Impl::luaTargetIs64Bit);
    registerGlobal("getOpenedProcessID", &Impl::luaGetOpenedProcessID);
}

void LuaRuntime::Impl::registerGlobal(const char* name, lua_CFunction function) {
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, function, 1);
    lua_setglobal(L_, name);
}

void LuaRuntime::Impl::registerStringListMetatable() {
    if (luaL_newmetatable(L_, kStringListMetatable) == 0) {
        lua_pop(L_, 1);
        return;
    }

    lua_pushcfunction(L_, &Impl::luaStringListIndex);
    lua_setfield(L_, -2, "__index");
    lua_pushcfunction(L_, &Impl::luaStringListLength);
    lua_setfield(L_, -2, "__len");
    lua_pushcfunction(L_, &Impl::luaStringListGc);
    lua_setfield(L_, -2, "__gc");
    lua_pop(L_, 1);
}

void LuaRuntime::Impl::registerTimerMetatable() {
    if (luaL_newmetatable(L_, kTimerMetatable) == 0) {
        lua_pop(L_, 1);
        return;
    }

    lua_pushcfunction(L_, &Impl::luaTimerIndex);
    lua_setfield(L_, -2, "__index");
    lua_pushcfunction(L_, &Impl::luaTimerNewIndex);
    lua_setfield(L_, -2, "__newindex");
    lua_pop(L_, 1);
}

int LuaRuntime::Impl::getOrCreateScriptEnv(std::string_view scriptId) {
    if (const auto existing = scriptEnvironments_.find(std::string(scriptId)); existing != scriptEnvironments_.end()) {
        return existing->second.environmentRef;
    }

    lua_newtable(L_);
    lua_newtable(L_);
    lua_pushglobaltable(L_);
    lua_setfield(L_, -2, "__index");
    lua_setmetatable(L_, -2);
    const auto ref = luaL_ref(L_, LUA_REGISTRYINDEX);
    scriptEnvironments_.emplace(std::string(scriptId), ScriptEnvironmentRecord{.environmentRef = ref});
    return ref;
}

void LuaRuntime::Impl::pushEnvironment(std::optional<std::string_view> scriptId) {
    if (scriptId.has_value()) {
        const auto ref = getOrCreateScriptEnv(*scriptId);
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
        return;
    }

    lua_pushglobaltable(L_);
}

void LuaRuntime::Impl::pushTraceback() {
    lua_pushcfunction(L_, &Impl::luaTraceback);
}

LuaResult LuaRuntime::Impl::executeChunk(
    std::optional<std::string> scriptId,
    std::string_view source,
    std::string_view chunkName) {
    const auto baseTop = lua_gettop(L_);
    pushTraceback();
    const auto tracebackIndex = lua_gettop(L_);

    const auto chunkNameString = std::string(chunkName.empty() ? "chunk" : chunkName);
    const auto loadError = luaL_loadbufferx(
        L_,
        source.data(),
        source.size(),
        chunkNameString.c_str(),
        "t");
    if (loadError != LUA_OK) {
        const auto message = lua_tostring(L_, -1);
        std::string text = message != nullptr ? message : "unknown load error";
        lua_settop(L_, baseTop);
        throw std::runtime_error(text);
    }

    pushEnvironment(scriptId ? std::optional<std::string_view>(*scriptId) : std::nullopt);
    if (lua_setupvalue(L_, -2, 1) == nullptr) {
        lua_pop(L_, 1);
    }

    ExecutionScope scope(*this, std::move(scriptId));
    if (lua_pcall(L_, 0, LUA_MULTRET, tracebackIndex) != LUA_OK) {
        const auto message = lua_tostring(L_, -1);
        std::string text = message != nullptr ? message : "unknown runtime error";
        lua_settop(L_, baseTop);
        throw std::runtime_error(text);
    }

    LuaResult result;
    for (int index = tracebackIndex + 1; index <= lua_gettop(L_); ++index) {
        result.values.push_back(extractValue(index));
    }

    lua_settop(L_, baseTop);
    return result;
}

engine::ScriptContext& LuaRuntime::Impl::currentScriptContext() {
    return session_.createScriptContext(currentScriptContextId());
}

std::string LuaRuntime::Impl::currentScriptContextId() const {
    if (currentScriptId_.has_value()) {
        return *currentScriptId_;
    }

    return std::string(kGlobalScriptContextId);
}

core::Address LuaRuntime::Impl::resolveAddressArgument(lua_State* lua, int index) {
    if (lua_isinteger(lua, index)) {
        return static_cast<core::Address>(lua_tointeger(lua, index));
    }

    const auto* expression = luaL_checkstring(lua, index);
    return currentScriptContext().resolveAddress(expression);
}

std::optional<core::Address> LuaRuntime::Impl::tryResolveAddressArgument(lua_State* lua, int index) {
    try {
        return resolveAddressArgument(lua, index);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::byte> LuaRuntime::Impl::collectByteArguments(lua_State* lua, int firstIndex) {
    std::vector<std::byte> bytes;
    const auto argc = lua_gettop(lua);
    if (firstIndex > argc) {
        luaL_error(lua, "expected at least one byte value");
    }

    if (lua_istable(lua, firstIndex)) {
        const auto count = lua_rawlen(lua, firstIndex);
        bytes.reserve(count);
        for (std::size_t index = 1; index <= count; ++index) {
            lua_rawgeti(lua, firstIndex, static_cast<lua_Integer>(index));
            const auto value = luaL_checkinteger(lua, -1);
            lua_pop(lua, 1);
            if (value < 0 || value > 0xFF) {
                luaL_error(lua, "byte value out of range");
            }
            bytes.push_back(static_cast<std::byte>(value));
        }
        return bytes;
    }

    bytes.reserve(static_cast<std::size_t>(argc - firstIndex + 1));
    for (int index = firstIndex; index <= argc; ++index) {
        const auto value = luaL_checkinteger(lua, index);
        if (value < 0 || value > 0xFF) {
            luaL_error(lua, "byte value out of range");
        }
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

LuaValue LuaRuntime::Impl::extractValue(int index) {
    switch (lua_type(L_, index)) {
    case LUA_TNIL:
        return std::monostate{};
    case LUA_TBOOLEAN:
        return lua_toboolean(L_, index) != 0;
    case LUA_TNUMBER:
        if (lua_isinteger(L_, index) != 0) {
            return static_cast<std::int64_t>(lua_tointeger(L_, index));
        }
        return lua_tonumber(L_, index);
    case LUA_TSTRING:
        return std::string(lua_tostring(L_, index));
    default:
        throw makeLuaError("only primitive Lua return values are supported");
    }
}

LuaRuntime::Impl& LuaRuntime::Impl::fromUpvalue(lua_State* lua) {
    auto* impl = static_cast<Impl*>(lua_touserdata(lua, lua_upvalueindex(1)));
    if (impl == nullptr) {
        throw makeLuaError("missing Lua runtime binding");
    }
    return *impl;
}

LuaRuntime::Impl::StringListHandle& LuaRuntime::Impl::stringListHandle(lua_State* lua, int index) {
    return *static_cast<StringListHandle*>(luaL_checkudata(lua, index, kStringListMetatable));
}

LuaRuntime::Impl::TimerHandle& LuaRuntime::Impl::timerHandle(lua_State* lua, int index) {
    return *static_cast<TimerHandle*>(luaL_checkudata(lua, index, kTimerMetatable));
}

void LuaRuntime::Impl::pushStringList(std::vector<core::Address> hits) {
    auto* handle = static_cast<StringListHandle*>(lua_newuserdatauv(L_, sizeof(StringListHandle), 0));
    new (handle) StringListHandle{
        .impl = this,
        .values = std::make_shared<std::vector<std::string>>(),
        .destroyed = false,
    };
    handle->values->reserve(hits.size());
    for (const auto hit : hits) {
        handle->values->push_back(formatHexAddress(hit));
    }
    luaL_setmetatable(L_, kStringListMetatable);
}

void LuaRuntime::Impl::pushTimerHandle(std::uint64_t id) {
    auto* handle = static_cast<TimerHandle*>(lua_newuserdatauv(L_, sizeof(TimerHandle), 0));
    handle->impl = this;
    handle->id = id;
    luaL_setmetatable(L_, kTimerMetatable);
}

template <typename T>
int LuaRuntime::Impl::pushReadInteger(lua_State* lua, bool isSigned) {
    const auto address = resolveAddressArgument(lua, 1);
    if (isSigned) {
        const auto value = session_.process().readValue<std::make_signed_t<T>>(address);
        lua_pushinteger(lua, static_cast<lua_Integer>(value));
        return 1;
    }

    const auto value = session_.process().readValue<T>(address);
    lua_pushinteger(lua, static_cast<lua_Integer>(value));
    return 1;
}

template <typename T>
int LuaRuntime::Impl::writeValue(lua_State* lua) {
    const auto address = resolveAddressArgument(lua, 1);
    const auto value = static_cast<T>(luaL_checkinteger(lua, 2));
    session_.writeBytes(address, std::as_bytes(std::span<const T>(&value, 1)));
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaRuntime::Impl::luaTraceback(lua_State* lua) {
    const auto* message = lua_tostring(lua, 1);
    if (message == nullptr) {
        return 1;
    }

    luaL_traceback(lua, lua, message, 1);
    return 1;
}

int LuaRuntime::Impl::luaGetAddress(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    lua_pushinteger(lua, static_cast<lua_Integer>(impl.resolveAddressArgument(lua, 1)));
    return 1;
}

int LuaRuntime::Impl::luaGetAddressSafe(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    if (const auto address = impl.tryResolveAddressArgument(lua, 1)) {
        lua_pushinteger(lua, static_cast<lua_Integer>(*address));
    } else {
        lua_pushnil(lua);
    }
    return 1;
}

int LuaRuntime::Impl::luaRegisterSymbol(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto* name = luaL_checkstring(lua, 1);
    const auto address = impl.resolveAddressArgument(lua, 2);
    const auto doNotSave = lua_toboolean(lua, 3) != 0;
    (void)impl.session_.registerSymbol(name, address, engine::SymbolKind::UserDefined, !doNotSave);
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaRuntime::Impl::luaUnregisterSymbol(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto* name = luaL_checkstring(lua, 1);
    lua_pushboolean(lua, impl.session_.unregisterSymbol(name));
    return 1;
}

int LuaRuntime::Impl::luaAOBScan(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto* pattern = luaL_checkstring(lua, 1);
    const auto hits = impl.session_.aobScan(pattern);
    if (hits.empty()) {
        lua_pushnil(lua);
        return 1;
    }

    impl.pushStringList(hits);
    return 1;
}

int LuaRuntime::Impl::luaAOBScanUnique(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto* pattern = luaL_checkstring(lua, 1);
    const auto hits = impl.session_.aobScan(pattern);
    if (hits.size() != 1) {
        lua_pushnil(lua);
        return 1;
    }

    lua_pushinteger(lua, static_cast<lua_Integer>(hits.front()));
    return 1;
}

int LuaRuntime::Impl::luaAOBScanModule(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto* moduleName = luaL_checkstring(lua, 1);
    const auto* pattern = luaL_checkstring(lua, 2);
    const auto hits = impl.session_.aobScanModule(moduleName, pattern);
    if (hits.empty()) {
        lua_pushnil(lua);
        return 1;
    }

    impl.pushStringList(hits);
    return 1;
}

int LuaRuntime::Impl::luaAOBScanModuleUnique(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto* moduleName = luaL_checkstring(lua, 1);
    const auto* pattern = luaL_checkstring(lua, 2);
    const auto hits = impl.session_.aobScanModule(moduleName, pattern);
    if (hits.size() != 1) {
        lua_pushnil(lua);
        return 1;
    }

    lua_pushinteger(lua, static_cast<lua_Integer>(hits.front()));
    return 1;
}

int LuaRuntime::Impl::luaReadBytes(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto address = impl.resolveAddressArgument(lua, 1);
    const auto count = luaL_checkinteger(lua, 2);
    if (count < 0) {
        return luaL_error(lua, "readBytes count must be non-negative");
    }

    const auto bytes = impl.session_.process().read(address, static_cast<std::size_t>(count));
    if (lua_toboolean(lua, 3) != 0) {
        lua_createtable(lua, static_cast<int>(bytes.size()), 0);
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            lua_pushinteger(lua, static_cast<lua_Integer>(std::to_integer<unsigned int>(bytes[index])));
            lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
        }
        return 1;
    }

    for (const auto byte : bytes) {
        lua_pushinteger(lua, static_cast<lua_Integer>(std::to_integer<unsigned int>(byte)));
    }
    return static_cast<int>(bytes.size());
}

int LuaRuntime::Impl::luaWriteBytes(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto address = impl.resolveAddressArgument(lua, 1);
    const auto bytes = impl.collectByteArguments(lua, 2);
    impl.session_.writeBytes(address, bytes);
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaRuntime::Impl::luaReadSmallInteger(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    return impl.pushReadInteger<std::uint16_t>(lua, lua_toboolean(lua, 2) != 0);
}

int LuaRuntime::Impl::luaWriteSmallInteger(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    return impl.writeValue<std::uint16_t>(lua);
}

int LuaRuntime::Impl::luaReadInteger(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    return impl.pushReadInteger<std::uint32_t>(lua, lua_toboolean(lua, 2) != 0);
}

int LuaRuntime::Impl::luaWriteInteger(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    return impl.writeValue<std::uint32_t>(lua);
}

int LuaRuntime::Impl::luaReadQword(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    return impl.pushReadInteger<std::uint64_t>(lua, false);
}

int LuaRuntime::Impl::luaWriteQword(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    return impl.writeValue<std::uint64_t>(lua);
}

int LuaRuntime::Impl::luaReadPointer(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    if (impl.session_.process().pointerSize() == 8) {
        return impl.pushReadInteger<std::uint64_t>(lua, false);
    }
    return impl.pushReadInteger<std::uint32_t>(lua, false);
}

int LuaRuntime::Impl::luaReadFloat(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto address = impl.resolveAddressArgument(lua, 1);
    lua_pushnumber(lua, impl.session_.process().readValue<float>(address));
    return 1;
}

int LuaRuntime::Impl::luaWriteFloat(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto address = impl.resolveAddressArgument(lua, 1);
    const auto value = static_cast<float>(luaL_checknumber(lua, 2));
    impl.session_.writeBytes(address, std::as_bytes(std::span<const float>(&value, 1)));
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaRuntime::Impl::luaReadDouble(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto address = impl.resolveAddressArgument(lua, 1);
    lua_pushnumber(lua, impl.session_.process().readValue<double>(address));
    return 1;
}

int LuaRuntime::Impl::luaWriteDouble(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto address = impl.resolveAddressArgument(lua, 1);
    const auto value = luaL_checknumber(lua, 2);
    impl.session_.writeBytes(address, std::as_bytes(std::span<const double>(&value, 1)));
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaRuntime::Impl::luaReadString(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto address = impl.resolveAddressArgument(lua, 1);
    const auto maxLength = luaL_checkinteger(lua, 2);
    const auto wide = lua_toboolean(lua, 3) != 0;
    if (maxLength < 0) {
        return luaL_error(lua, "readString maxLength must be non-negative");
    }

    if (!wide) {
        const auto bytes = impl.session_.process().read(address, static_cast<std::size_t>(maxLength));
        std::string value;
        value.reserve(bytes.size());
        for (const auto byte : bytes) {
            if (byte == std::byte{0x00}) {
                break;
            }
            value.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
        }
        lua_pushlstring(lua, value.data(), value.size());
        return 1;
    }

    const auto bytes = impl.session_.process().read(address, static_cast<std::size_t>(maxLength) * sizeof(char16_t));
    std::u16string value;
    value.reserve(static_cast<std::size_t>(maxLength));
    for (std::size_t index = 0; index + 1 < bytes.size(); index += 2) {
        const auto code = static_cast<char16_t>(
            std::to_integer<unsigned char>(bytes[index]) |
            (std::to_integer<unsigned char>(bytes[index + 1]) << 8));
        if (code == u'\0') {
            break;
        }
        value.push_back(code);
    }

    const auto utf8 = utf16ToUtf8(value);
    lua_pushlstring(lua, utf8.data(), utf8.size());
    return 1;
}

int LuaRuntime::Impl::luaWriteString(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto address = impl.resolveAddressArgument(lua, 1);
    const auto text = std::string_view(luaL_checkstring(lua, 2));
    const auto terminate = lua_isnoneornil(lua, 3) ? true : (lua_toboolean(lua, 3) != 0);
    const auto wide = lua_toboolean(lua, 4) != 0;

    if (!wide) {
        std::vector<std::byte> bytes;
        bytes.reserve(text.size() + (terminate ? 1 : 0));
        for (const auto ch : text) {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
        if (terminate) {
            bytes.push_back(std::byte{0x00});
        }
        impl.session_.writeBytes(address, bytes);
    } else {
        auto utf16 = utf8ToUtf16(text);
        std::vector<std::byte> bytes(utf16.size() * sizeof(char16_t));
        if (!utf16.empty()) {
            std::memcpy(bytes.data(), utf16.data(), bytes.size());
        }
        if (terminate) {
            bytes.push_back(std::byte{0x00});
            bytes.push_back(std::byte{0x00});
        }
        impl.session_.writeBytes(address, bytes);
    }

    lua_pushboolean(lua, 1);
    return 1;
}

int LuaRuntime::Impl::luaFullAccess(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto address = impl.resolveAddressArgument(lua, 1);
    const auto size = luaL_checkinteger(lua, 2);
    if (size <= 0) {
        return luaL_error(lua, "fullAccess size must be greater than zero");
    }

    (void)impl.session_.fullAccess(address, static_cast<std::size_t>(size));
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaRuntime::Impl::luaAutoAssemble(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    const auto* source = luaL_checkstring(lua, 1);
    if (!lua_isnoneornil(lua, 2) && lua_toboolean(lua, 2) != 0) {
        lua_pushboolean(lua, 0);
        lua_pushliteral(lua, "autoAssemble targetself=true is not supported");
        return 2;
    }
    if (!lua_isnoneornil(lua, 3)) {
        lua_pushboolean(lua, 0);
        lua_pushliteral(lua, "autoAssemble disableInfo is not supported");
        return 2;
    }

    try {
        engine::AssemblyScript program(impl.currentScriptContext());
        (void)program.execute(source);
        lua_pushboolean(lua, 1);
        return 1;
    } catch (const std::exception& exception) {
        lua_pushboolean(lua, 0);
        lua_pushstring(lua, exception.what());
        return 2;
    }
}

int LuaRuntime::Impl::luaCreateTimer(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    TimerRecord timer;
    timer.id = impl.nextTimerId_++;
    if (impl.currentScriptId_.has_value()) {
        timer.ownerScriptId = impl.currentScriptId_;
    }

    const auto argc = lua_gettop(lua);
    if (argc == 0) {
        timer.enabled = true;
    } else if (lua_isboolean(lua, 1)) {
        timer.enabled = lua_toboolean(lua, 1) != 0;
    } else if (lua_isinteger(lua, 1)) {
        timer.interval = std::chrono::milliseconds(luaL_checkinteger(lua, 1));
        if (timer.interval.count() < 0) {
            return luaL_error(lua, "createTimer interval must be non-negative");
        }

        luaL_checktype(lua, 2, LUA_TFUNCTION);
        lua_pushvalue(lua, 2);
        timer.callbackRef = luaL_ref(lua, LUA_REGISTRYINDEX);
        timer.oneshot = true;
        timer.enabled = true;
    } else {
        return luaL_error(lua, "createTimer only supports (), (enabled), or (interval, callback) in this runtime");
    }

    timer.nextFire = std::chrono::steady_clock::now() + timer.interval;
    impl.timers_.emplace(timer.id, std::move(timer));
    impl.pushTimerHandle(impl.nextTimerId_ - 1);
    return 1;
}

int LuaRuntime::Impl::luaSleep(lua_State* lua) {
    const auto duration = luaL_checkinteger(lua, 1);
    if (duration < 0) {
        return luaL_error(lua, "sleep duration must be non-negative");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
    return 0;
}

int LuaRuntime::Impl::luaTargetIs64Bit(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    lua_pushboolean(lua, impl.session_.process().pointerSize() == 8);
    return 1;
}

int LuaRuntime::Impl::luaGetOpenedProcessID(lua_State* lua) {
    auto& impl = fromUpvalue(lua);
    lua_pushinteger(lua, static_cast<lua_Integer>(impl.session_.process().pid()));
    return 1;
}

int LuaRuntime::Impl::luaStringListIndex(lua_State* lua) {
    auto& handle = stringListHandle(lua, 1);
    if (handle.destroyed || !handle.values) {
        lua_pushnil(lua);
        return 1;
    }

    if (lua_isinteger(lua, 2)) {
        const auto index = lua_tointeger(lua, 2);
        if (index < 0 || static_cast<std::size_t>(index) >= handle.values->size()) {
            lua_pushnil(lua);
            return 1;
        }

        const auto& value = (*handle.values)[static_cast<std::size_t>(index)];
        lua_pushlstring(lua, value.data(), value.size());
        return 1;
    }

    const auto* key = luaL_checkstring(lua, 2);
    if (std::strcmp(key, "Count") == 0) {
        lua_pushinteger(lua, static_cast<lua_Integer>(handle.values->size()));
        return 1;
    }
    if (std::strcmp(key, "destroy") == 0) {
        lua_pushcfunction(lua, &Impl::luaStringListDestroy);
        return 1;
    }

    lua_pushnil(lua);
    return 1;
}

int LuaRuntime::Impl::luaStringListLength(lua_State* lua) {
    auto& handle = stringListHandle(lua, 1);
    lua_pushinteger(lua, handle.destroyed || !handle.values ? 0 : static_cast<lua_Integer>(handle.values->size()));
    return 1;
}

int LuaRuntime::Impl::luaStringListDestroy(lua_State* lua) {
    auto& handle = stringListHandle(lua, 1);
    handle.values.reset();
    handle.destroyed = true;
    return 0;
}

int LuaRuntime::Impl::luaStringListGc(lua_State* lua) {
    auto& handle = stringListHandle(lua, 1);
    handle.values.reset();
    handle.destroyed = true;
    handle.~StringListHandle();
    return 0;
}

int LuaRuntime::Impl::luaTimerIndex(lua_State* lua) {
    auto& handle = timerHandle(lua, 1);
    auto iterator = handle.impl->timers_.find(handle.id);
    if (iterator == handle.impl->timers_.end()) {
        lua_pushnil(lua);
        return 1;
    }

    const auto* key = luaL_checkstring(lua, 2);
    if (std::strcmp(key, "Interval") == 0) {
        lua_pushinteger(lua, static_cast<lua_Integer>(iterator->second.interval.count()));
        return 1;
    }
    if (std::strcmp(key, "Enabled") == 0) {
        lua_pushboolean(lua, iterator->second.enabled);
        return 1;
    }
    if (std::strcmp(key, "OnTimer") == 0) {
        if (iterator->second.callbackRef == LUA_NOREF) {
            lua_pushnil(lua);
        } else {
            lua_rawgeti(lua, LUA_REGISTRYINDEX, iterator->second.callbackRef);
        }
        return 1;
    }
    if (std::strcmp(key, "destroy") == 0) {
        lua_pushcfunction(lua, &Impl::luaTimerDestroy);
        return 1;
    }

    lua_pushnil(lua);
    return 1;
}

int LuaRuntime::Impl::luaTimerNewIndex(lua_State* lua) {
    auto& handle = timerHandle(lua, 1);
    auto iterator = handle.impl->timers_.find(handle.id);
    if (iterator == handle.impl->timers_.end()) {
        return luaL_error(lua, "timer has been destroyed");
    }

    const auto* key = luaL_checkstring(lua, 2);
    if (std::strcmp(key, "Interval") == 0) {
        const auto interval = luaL_checkinteger(lua, 3);
        if (interval < 0) {
            return luaL_error(lua, "timer interval must be non-negative");
        }
        iterator->second.interval = std::chrono::milliseconds(interval);
        if (iterator->second.enabled) {
            iterator->second.nextFire = std::chrono::steady_clock::now() + iterator->second.interval;
        }
        return 0;
    }
    if (std::strcmp(key, "Enabled") == 0) {
        iterator->second.enabled = lua_toboolean(lua, 3) != 0;
        if (iterator->second.enabled) {
            iterator->second.nextFire = std::chrono::steady_clock::now() + iterator->second.interval;
        }
        return 0;
    }
    if (std::strcmp(key, "OnTimer") == 0) {
        if (iterator->second.callbackRef != LUA_NOREF) {
            luaL_unref(lua, LUA_REGISTRYINDEX, iterator->second.callbackRef);
            iterator->second.callbackRef = LUA_NOREF;
        }

        if (!lua_isnil(lua, 3)) {
            luaL_checktype(lua, 3, LUA_TFUNCTION);
            lua_pushvalue(lua, 3);
            iterator->second.callbackRef = luaL_ref(lua, LUA_REGISTRYINDEX);
        }
        return 0;
    }

    return luaL_error(lua, "unknown timer property");
}

int LuaRuntime::Impl::luaTimerDestroy(lua_State* lua) {
    auto& handle = timerHandle(lua, 1);
    handle.impl->destroyTimer(handle.id);
    return 0;
}

LuaRuntime::LuaRuntime(engine::EngineSession& session)
    : impl_(std::make_unique<Impl>(session)) {
}

LuaRuntime::~LuaRuntime() = default;

LuaResult LuaRuntime::runGlobal(std::string_view source, std::string_view chunkName) {
    return impl_->runGlobal(source, chunkName);
}

LuaResult LuaRuntime::runScript(std::string_view scriptId, std::string_view source, std::string_view chunkName) {
    return impl_->runScript(scriptId, source, chunkName);
}

bool LuaRuntime::destroyScript(std::string_view scriptId) {
    return impl_->destroyScript(scriptId);
}

}  // namespace hexengine::lua
