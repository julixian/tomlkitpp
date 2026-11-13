module;

#include "TKPMacros.hpp"

export module tomlkitpp:Parser;

export import std;
export import :Spec;
export import :Error;
export import :Types;
export import :Decor;
export import :Item;

export NAMESPACE_BEGIN(tomlkit)

// 解析 UTF-8 TOML 文本（默认启用 TOML v1.1），失败返回带精确行列诊断的 `ParseError`
std::expected<Item, ParseError> tryParse(
    std::string_view utf8Source,
    ParseOptions options = {},
    std::string_view sourcePath = ""
);

// 解析 UTF-8 TOML 文本，失败抛出 `SyntaxError`
Item parse(
    std::string_view utf8Source,
    ParseOptions options = {},
    std::string_view sourcePath = ""
);

namespace detail
{
    std::expected<Item, ParseError> tryParsePathImpl(
        const std::filesystem::path& filePath,
        ParseOptions options
    );

    Item parsePathImpl(
        const std::filesystem::path& filePath,
        ParseOptions options
    );
}

// 从 std::filesystem::path 解析 UTF-8 TOML 文件（Windows 下使用原生宽字符路径）
template<typename PathT>
    requires std::same_as<std::remove_cvref_t<PathT>, std::filesystem::path>
std::expected<Item, ParseError> tryParseFile(
    const PathT& filePath,
    ParseOptions options = {}
)
{
    return detail::tryParsePathImpl(filePath, options);
}

// 从 UTF-8 / 宽字符路径字符串解析 UTF-8 TOML 文件
std::expected<Item, ParseError> tryParseFile(
    std::string_view utf8FilePath,
    ParseOptions options = {}
);

std::expected<Item, ParseError> tryParseFile(
    std::wstring_view wideFilePath,
    ParseOptions options = {}
);

std::expected<Item, ParseError> tryParseFile(
    std::u8string_view u8FilePath,
    ParseOptions options = {}
);

template<typename PathT>
    requires std::same_as<std::remove_cvref_t<PathT>, std::filesystem::path>
Item parseFile(
    const PathT& filePath,
    ParseOptions options = {}
)
{
    return detail::parsePathImpl(filePath, options);
}

Item parseFile(
    std::string_view utf8FilePath,
    ParseOptions options = {}
);

Item parseFile(
    std::wstring_view wideFilePath,
    ParseOptions options = {}
);

Item parseFile(
    std::u8string_view u8FilePath,
    ParseOptions options = {}
);

NAMESPACE_END(tomlkit)
