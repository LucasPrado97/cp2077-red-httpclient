#pragma once
// macOS / iOS (CyPort) only. What the three Red* plugins (RedData, RedFileSystem, RedHttpClient) need on the Mac build of the game and
// that the Windows build gets from RED4ext and Win32: a logger that also writes stderr, the game folder, the vault the Mac SDK
// allocates through (MacVault.cpp), the trap for an address hash with no Mac address (MacTrap.cpp), the publishing of a global
// native function, and the entry point that registers the RedLib types.
// The same files are in the three repositories; what differs comes from macos/CMakeLists.txt (REDMAC_TAG, REDMAC_RTTI_ID_BAND,
// REDMAC_SERIAL_SLOT). Symbols are hidden, so each dylib binds its own copy.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>

namespace RedMac
{
// Called by Main (Load). Until then logging goes to stderr only.
void Init(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);

// stderr and the RED4ext logger (red4ext/logs/<plugin>.log). Never a dialog.
void Log(const char* aFormat, ...) __attribute__((format(printf, 1, 2)));
void LogWarn(const char* aFormat, ...) __attribute__((format(printf, 1, 2)));
void LogError(const char* aFormat, ...) __attribute__((format(printf, 1, 2)));
// One line per key for the life of the process.
void LogOnce(const char* aKey, const char* aFormat, ...) __attribute__((format(printf, 2, 3)));
// REDMAC_DIAG=1 in the environment. Read once.
bool DiagEnabled();

// The game folder (the one with red4ext/, r6/, archive/). CyPort: from the shim export MSGameExecutablePath (the pretend
// <root>/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077). Otherwise three levels above this dylib (<root>/red4ext/plugins/<Name>/).
std::filesystem::path GameRoot();
// The spelling of a path that exists on a case-sensitive volume (CyPort shim export MSResolvePathCase). The path itself when the
// export is missing (macOS) or nothing matches.
std::filesystem::path ResolveCase(const std::filesystem::path& aPath);

// Adds the RTTI post-register callback that registers and describes this plugin's RedLib types (R30: the Mac game never runs the
// RTTI REGISTER callbacks RedLib queues; the POST-register callbacks do run, after the game's own describe steps).
// Call it from Main (Load) in place of Red::TypeInfoRegistrar::RegisterDiscovered().
bool RegisterTypesWhenReady();
// True once the types are registered.
bool TypesRegistered();

bool VaultReady();
bool VaultSelfTest();
bool IsTrapAddress(uintptr_t aAddress);
} // namespace RedMac
