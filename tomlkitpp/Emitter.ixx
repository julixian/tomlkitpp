module;

#include "TKPMacros.hpp"

export module tomlkitpp:Emitter;

export import std;
export import :Spec;
export import :Error;
export import :Types;
export import :Decor;
export import :Item;

export NAMESPACE_BEGIN(tomlkit)

// 将文档或任意节点序列化为 TOML 文本（未修改部分原样保留）
std::string format(const Item& item, FormatOptions options = {});

namespace detail
{
    std::expected<void, ParseError> trySavePathAtomicImpl(
        const std::filesystem::path& filePath,
        const Item& doc,
        FormatOptions options
    );

    void savePathAtomicImpl(
        const std::filesystem::path& filePath,
        const Item& doc,
        FormatOptions options
    );
}

// 原子落盘：先以二进制模式写入临时文件，成功关闭后再原子重命名覆盖目标路径
template<typename PathT>
    requires std::same_as<std::remove_cvref_t<PathT>, std::filesystem::path>
void saveFileAtomic(
    const PathT& filePath,
    const Item& doc,
    FormatOptions options = {}
)
{
    detail::savePathAtomicImpl(filePath, doc, options);
}

void saveFileAtomic(
    std::string_view utf8FilePath,
    const Item& doc,
    FormatOptions options = {}
);

void saveFileAtomic(
    std::wstring_view wideFilePath,
    const Item& doc,
    FormatOptions options = {}
);

void saveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options = {}
);

template<typename PathT>
    requires std::same_as<std::remove_cvref_t<PathT>, std::filesystem::path>
std::expected<void, ParseError> trySaveFileAtomic(
    const PathT& filePath,
    const Item& doc,
    FormatOptions options = {}
)
{
    return detail::trySavePathAtomicImpl(filePath, doc, options);
}

std::expected<void, ParseError> trySaveFileAtomic(
    std::string_view utf8FilePath,
    const Item& doc,
    FormatOptions options = {}
);

std::expected<void, ParseError> trySaveFileAtomic(
    std::wstring_view wideFilePath,
    const Item& doc,
    FormatOptions options = {}
);

std::expected<void, ParseError> trySaveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options = {}
);

NAMESPACE_END(tomlkit)
