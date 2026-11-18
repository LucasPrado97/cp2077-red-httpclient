#pragma once
// macOS / iOS only. Force-included into every translation unit (macos/CMakeLists.txt: -include), so src/ keeps its Windows spelling.
// The SDK's RED4ext/Detail/WinCompat.hpp supplies the Windows types; this adds the std headers MSVC pulls in transitively and the
// few CRT names the sources use.
#ifdef __APPLE__

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <RED4ext/Detail/WinCompat.hpp>

#ifndef __fastcall
#define __fastcall
#endif

// The SDK headers include each other in a circle; they only compile when the umbrella header comes first.
#include <RED4ext/RED4ext.hpp>

// MSVC CRT: strerror_s(buffer, errnum) with the buffer size deduced.
template<size_t N>
inline int strerror_s(char (&aBuffer)[N], int aErrno)
{
    return strerror_r(aErrno, aBuffer, N);
}

#endif // __APPLE__
