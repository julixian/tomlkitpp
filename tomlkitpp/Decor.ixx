module;

#include "TKPMacros.hpp"

export module tomlkitpp:Decor;

export import std;
export import :Spec;
export import :Types;

export NAMESPACE_BEGIN(tomlkit)

struct Decor
{
    std::string prefix;
    std::string suffix;

    void clear() noexcept
    {
        prefix.clear();
        suffix.clear();
    }

    // 提取 prefix 中的所有行级注释文本（不含前导 `#` 及紧随其后的单个空格）
    std::vector<std::string> leadingComments() const;
    void addLeadingComment(std::string_view commentText, std::string_view newline = "\n");
    void clearLeadingComments();

    // 提取或修改 suffix 中的行尾注释（保留原有的换行符 `\n` 或 `\r\n`）
    std::optional<std::string> inlineComment() const;
    void setInlineComment(std::string_view commentText);
    void clearInlineComment();

    friend bool operator==(const Decor&, const Decor&) = default;
};

struct KeySegment
{
    std::string name;
    KeyStyle style = KeyStyle::Bare;
    std::string rawRepr;
    Decor decor{};

    KeySegment() = default;
    explicit KeySegment(std::string keyName, KeyStyle keyStyle = KeyStyle::Bare)
        : name(std::move(keyName))
        , style(keyStyle)
    { }

    void setName(std::string newName)
    {
        name = std::move(newName);
        rawRepr.clear();
    }

    friend bool operator==(const KeySegment& lhs, const KeySegment& rhs) noexcept
    {
        return lhs.name == rhs.name;
    }
};

// 表示单个键值对或表头中的键（支持保留 `a . "b" . c = 1` 每一段的独立引号与点号两侧空白）
class Key
{
public:
    Key() = default;

    explicit Key(std::string leafName, KeyStyle style = KeyStyle::Bare)
        : m_leaf(std::move(leafName), style)
    { }

    Key(std::vector<KeySegment> dottedPrefix, KeySegment leaf)
        : m_dottedPrefix(std::move(dottedPrefix))
        , m_leaf(std::move(leaf))
    { }

    const std::string& name() const noexcept { return m_leaf.name; }
    void setName(std::string newName) { m_leaf.setName(std::move(newName)); }

    KeyStyle style() const noexcept { return m_leaf.style; }
    void setStyle(KeyStyle style) noexcept
    {
        m_leaf.style = style;
        m_leaf.rawRepr.clear();
    }

    KeySegment& leaf() noexcept { return m_leaf; }
    const KeySegment& leaf() const noexcept { return m_leaf; }

    std::vector<KeySegment>& dottedPrefix() noexcept { return m_dottedPrefix; }
    const std::vector<KeySegment>& dottedPrefix() const noexcept { return m_dottedPrefix; }

    // 整行最前方的 Decor（若该行是点分键 `a.b = 1`，前导注释与缩进位于首段 `a` 的 prefix 上）
    Decor& lineLeadDecor() noexcept
    {
        return m_dottedPrefix.empty() ? m_leaf.decor : m_dottedPrefix.front().decor;
    }

    const Decor& lineLeadDecor() const noexcept
    {
        return m_dottedPrefix.empty() ? m_leaf.decor : m_dottedPrefix.front().decor;
    }

    std::vector<std::string> leadingComments() const { return lineLeadDecor().leadingComments(); }
    void addLeadingComment(std::string_view text, std::string_view newline = "\n") { lineLeadDecor().addLeadingComment(text, newline); }
    void clearLeadingComments() { lineLeadDecor().clearLeadingComments(); }

private:
    std::vector<KeySegment> m_dottedPrefix;
    KeySegment m_leaf;
};

// 格式化键名或字符串字面量的转义辅助函数
std::string formatKeySegment(const KeySegment& seg, SpecVersion spec = SpecVersion::v1_1);
std::string escapeBasicString(std::string_view value, bool multiline = false, SpecVersion spec = SpecVersion::v1_1);

NAMESPACE_END(tomlkit)
