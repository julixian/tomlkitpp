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

// 将文档或任意节点序列化为 TOML 文本（未修改部分 100% 字节级原样回放）
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

// 原子落盘：先以二进制模式写入临时文件，成功关闭后再原子重命名覆盖目标路径（全程使用宽字符/UTF-8 路径，绝不触发 ANSI 转换）
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

inline void saveFileAtomic(
    std::string_view utf8FilePath,
    const Item& doc,
    FormatOptions options = {}
)
{
    detail::savePathAtomicImpl(utf8ToPath(utf8FilePath), doc, options);
}

inline void saveFileAtomic(
    std::wstring_view wideFilePath,
    const Item& doc,
    FormatOptions options = {}
)
{
    detail::savePathAtomicImpl(std::filesystem::path(wideFilePath), doc, options);
}

inline void saveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options = {}
)
{
    detail::savePathAtomicImpl(std::filesystem::path(u8FilePath), doc, options);
}

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

inline std::expected<void, ParseError> trySaveFileAtomic(
    std::string_view utf8FilePath,
    const Item& doc,
    FormatOptions options = {}
)
{
    return detail::trySavePathAtomicImpl(utf8ToPath(utf8FilePath), doc, options);
}

inline std::expected<void, ParseError> trySaveFileAtomic(
    std::wstring_view wideFilePath,
    const Item& doc,
    FormatOptions options = {}
)
{
    return detail::trySavePathAtomicImpl(std::filesystem::path(wideFilePath), doc, options);
}

inline std::expected<void, ParseError> trySaveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options = {}
)
{
    return detail::trySavePathAtomicImpl(std::filesystem::path(u8FilePath), doc, options);
}

NAMESPACE_END(tomlkit)
