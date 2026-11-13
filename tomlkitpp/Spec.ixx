module;

#include "TKPMacros.hpp"

export module tomlkitpp:Spec;

export import std;

NAMESPACE_BEGIN(tomlkit)

using std::int8_t;
using std::int16_t;
using std::int32_t;
using std::int64_t;
using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::size_t;
using std::ptrdiff_t;

NAMESPACE_END(tomlkit)

export NAMESPACE_BEGIN(tomlkit)

enum class SpecVersion : uint8_t
{
    v1_0,
    v1_1
};

struct ParseOptions
{
    SpecVersion spec = SpecVersion::v1_1;
    uint32_t maxNestingDepth = 128;
};

struct FormatOptions
{
    SpecVersion spec = SpecVersion::v1_1;
    std::string_view indent = "  ";
    std::string_view newline = "\n";
    bool preferTrailingNewline = true;
};

NAMESPACE_END(tomlkit)
