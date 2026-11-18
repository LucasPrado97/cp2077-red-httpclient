// macOS / iOS (CyPort) only. See MacSupport.hpp.
#include "MacSupport.hpp"

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

#include <dlfcn.h>

#include <RedLib.hpp>

namespace
{
const RED4ext::v1::Sdk* g_sdk = nullptr;
RED4ext::v1::PluginHandle g_handle = nullptr;
std::atomic<bool> g_typesRegistered{false};

// Any address inside this dylib will do for dladdr.
void ModuleMarker() {}

enum class Level
{
    Info,
    Warn,
    Error
};

void Emit(Level aLevel, const char* aFormat, va_list aArgs)
{
    char line[2048];
    std::vsnprintf(line, sizeof(line), aFormat, aArgs);
    std::fprintf(stderr, "[" REDMAC_TAG "] %s\n", line);
    if (g_sdk && g_sdk->logger && g_handle)
    {
        // The line may contain a '%' (a path, a name), so it goes in as a message, never as the format.
        switch (aLevel)
        {
        case Level::Info:
            if (g_sdk->logger->Info)
                g_sdk->logger->Info(g_handle, line);
            break;
        case Level::Warn:
            if (g_sdk->logger->Warn)
                g_sdk->logger->Warn(g_handle, line);
            break;
        case Level::Error:
            if (g_sdk->logger->Error)
                g_sdk->logger->Error(g_handle, line);
            break;
        }
    }
}

// Runs once, from the game's RTTI post-register callbacks (or from <plugin>_registerNatives, whichever comes first).
void RunTypeRegistration()
{
    static std::atomic<bool> once{false};
    if (once.exchange(true))
    {
        return;
    }
    if (!RedMac::VaultReady())
    {
        RedMac::LogError("the game's pool functions are not where this build expects them (game 2.5.0 Mac): nothing registered");
        return;
    }
    if (!RedMac::VaultSelfTest())
    {
        RedMac::LogError("vault self-test failed (see the [vault] self-test lines on stderr): nothing registered");
        return;
    }
    const auto pendingRegister = Red::TypeInfoRegistrar::PendingRegisterCount();
    const auto pendingDescribe = Red::TypeInfoRegistrar::PendingDescribeCount();
    if (pendingRegister == 0)
    {
        RedMac::LogError("no RedLib type registrar was instantiated: the native classes will be missing");
    }
    Red::TypeInfoRegistrar::RunPending();
    g_typesRegistered.store(true);
    RedMac::Log("natives registered: %zu type registrations and %zu descriptions ran, %zu / %zu left", pendingRegister, pendingDescribe,
                Red::TypeInfoRegistrar::PendingRegisterCount(), Red::TypeInfoRegistrar::PendingDescribeCount());
}
} // namespace

// The same entry point TweakXL, ArchiveXL, Codeware and Mod Settings export (<plugin>_registerNatives): a launcher or another plugin
// may call it; it runs once.
extern "C" __attribute__((visibility("default"))) void REDMAC_ENTRY()
{
    RunTypeRegistration();
}

void RedMac::Init(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle)
{
    g_sdk = aSdk;
    g_handle = aHandle;
}

void RedMac::Log(const char* aFormat, ...)
{
    va_list args;
    va_start(args, aFormat);
    Emit(Level::Info, aFormat, args);
    va_end(args);
}

void RedMac::LogWarn(const char* aFormat, ...)
{
    va_list args;
    va_start(args, aFormat);
    Emit(Level::Warn, aFormat, args);
    va_end(args);
}

void RedMac::LogError(const char* aFormat, ...)
{
    va_list args;
    va_start(args, aFormat);
    Emit(Level::Error, aFormat, args);
    va_end(args);
}

void RedMac::LogOnce(const char* aKey, const char* aFormat, ...)
{
    static std::mutex lock;
    static std::unordered_set<std::string> seen;
    {
        std::scoped_lock guard(lock);
        if (!seen.insert(aKey).second)
        {
            return;
        }
    }
    va_list args;
    va_start(args, aFormat);
    Emit(Level::Warn, aFormat, args);
    va_end(args);
}

bool RedMac::DiagEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("REDMAC_DIAG");
        return value && value[0] == '1';
    }();
    return enabled;
}

std::filesystem::path RedMac::GameRoot()
{
    static const std::filesystem::path root = []() -> std::filesystem::path {
        // CyPort (iOS): the game folder is in the app's Documents; the shim reports its pretend macOS executable path,
        // <root>/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077.
        using PathFn = int (*)(char*, uint32_t*);
        if (auto shimPath = reinterpret_cast<PathFn>(dlsym(RTLD_DEFAULT, "MSGameExecutablePath")))
        {
            char buffer[PATH_MAX];
            uint32_t size = sizeof(buffer);
            if (shimPath(buffer, &size) == 0 && buffer[0])
            {
                return std::filesystem::path(buffer).parent_path().parent_path().parent_path().parent_path();
            }
        }
        // <root>/red4ext/plugins/<Name>/<Name>.dylib
        Dl_info info{};
        if (dladdr(reinterpret_cast<const void*>(&ModuleMarker), &info) && info.dli_fname)
        {
            std::error_code error;
            auto path = std::filesystem::absolute(std::filesystem::path(info.dli_fname), error);
            if (error)
            {
                path = info.dli_fname;
            }
            return path.parent_path().parent_path().parent_path().parent_path();
        }
        return {};
    }();
    return root;
}

std::filesystem::path RedMac::ResolveCase(const std::filesystem::path& aPath)
{
    using ResolveFn = int (*)(const char*, char*, size_t);
    static const auto resolve = reinterpret_cast<ResolveFn>(dlsym(RTLD_DEFAULT, "MSResolvePathCase"));
    char resolved[PATH_MAX];
    if (resolve && resolve(aPath.string().c_str(), resolved, sizeof(resolved)) == 0 && resolved[0])
    {
        return resolved;
    }
    return aPath;
}

bool RedMac::RegisterTypesWhenReady()
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    if (!rtti)
    {
        LogError("R30 no RTTI system at plugin load: the natives cannot register");
        return false;
    }
    // The game's RTTI POST-register callbacks run in the order they were added, the game's own describe steps first (its static
    // registrars add them before main()), so the natives register after every game type exists.
    rtti->AddPostRegisterCallback(&RunTypeRegistration);
    Log("R30 RTTI post-register callback added (natives register after the game's own describe steps)");
    return true;
}

bool RedMac::TypesRegistered()
{
    return g_typesRegistered.load();
}

// Copied from Codeware macos/MacSupport.cpp (Red::MacPublishGlobal). The virtual RegisterFunction can miss the global function
// map on the Mac game: insert into the CName map at CRTTISystem +0xA0 and, when GetFunction still misses, into the hash map at +0xD0.
// Each map is only written after it resolved a known game function (GetGameInstance) to the pointer the virtual returns.
void Red::MacPublishGlobal(RED4ext::CRTTISystem* aRtti, const char* aName, RED4ext::CGlobalFunction* aFunc)
{
    using Map = RED4ext::HashMap<RED4ext::CName, RED4ext::CGlobalFunction*>;
    auto* map = reinterpret_cast<Map*>(reinterpret_cast<uint8_t*>(aRtti) + 0xA0);
    const auto name = RED4ext::CName(aName);
    auto plausible = [](const void* aPtr) {
        const auto value = reinterpret_cast<uintptr_t>(aPtr);
        return value > 0x100000000ULL && (value % 8) == 0;
    };
    const bool shape = map->capacity >= 16 && map->capacity <= 100000 && map->size > 0 && map->size < map->capacity &&
                       map->nodeList.stride == sizeof(typename Map::Node) && plausible(map->indexTable) &&
                       plausible(map->nodeList.nodes);
    auto* known = aRtti->GetFunction(RED4ext::CName("GetGameInstance"));
    auto** slot = shape ? map->Get(RED4ext::CName("GetGameInstance")) : nullptr;
    const bool mapOk = shape && known && slot && *slot == known;
    int inserted = 0;
    if (mapOk)
    {
        const auto pair = map->Insert(name, aFunc);
        inserted = pair.second ? 1 : 0;
    }
    auto* found = aRtti->GetFunction(name);

    using HashMap64 = RED4ext::HashMap<uint64_t, RED4ext::CGlobalFunction*>;
    auto* byHash = reinterpret_cast<HashMap64*>(reinterpret_cast<uint8_t*>(aRtti) + 0xD0);
    const bool hashShape = byHash->capacity >= 16 && byHash->capacity <= 100000 && byHash->size > 0 &&
                           byHash->size < byHash->capacity && byHash->nodeList.stride == sizeof(typename HashMap64::Node) &&
                           plausible(byHash->indexTable) && plausible(byHash->nodeList.nodes);
    const auto knownHash = static_cast<uint64_t>(RED4ext::CName("GetGameInstance"));
    auto** hashKnown = hashShape ? byHash->Get(knownHash) : nullptr;
    const bool hashOk = hashShape && known && hashKnown && *hashKnown == known;
    int hashInserted = 0;
    if (hashOk && !found)
    {
        const auto pair = byHash->Insert(static_cast<uint64_t>(name), aFunc);
        hashInserted = pair.second ? 1 : 0;
        found = aRtti->GetFunction(name);
    }
    if (!found)
    {
        RedMac::LogError("global function %s could not be published (map=%d inserted=%d hashMap=%d hashInserted=%d): scripts that call it will not link",
                         aName, mapOk ? 1 : 0, inserted, hashOk ? 1 : 0, hashInserted);
    }
    else if (RedMac::DiagEnabled())
    {
        RedMac::Log("global function %s published (map=%d inserted=%d hashMap=%d hashInserted=%d)", aName, mapOk ? 1 : 0, inserted,
                    hashOk ? 1 : 0, hashInserted);
    }
}
