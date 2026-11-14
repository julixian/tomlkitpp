module;

#include "TKPMacros.hpp"

export module tomlkitpp;

export import std;
export import :Spec;
export import :Error;
export import :Types;
export import :Decor;
export import :Item;
export import :Parser;
export import :Emitter;
export import :Lexer;

export NAMESPACE_BEGIN(tomlkit)

constexpr std::string_view kLibraryVersion = "1.0.1";

NAMESPACE_END(tomlkit)
