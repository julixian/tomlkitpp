module;

#include "TKPMacros.hpp"

module tomlkitpp;

import :Decor;

NAMESPACE_BEGIN(tomlkit)

namespace
{
    std::string sanitizeSingleLineComment(std::string_view text)
    {
        std::string clean;
        clean.reserve(text.size());
        for (const char ch : text) {
            const uint8_t b = (uint8_t)ch;
            if (ch == '\r' || ch == '\n') {
                clean.push_back(' ');
            }
            else if ((b < 0x20u && ch != '\t') || b == 0x7Fu) {
                continue;
            }
            else {
                clean.push_back(ch);
            }
        }
        return clean;
    }

    std::string_view detectTrailingNewline(std::string_view s) noexcept
    {
        if (s.ends_with("\r\n")) {
            return "\r\n";
        }
        if (s.ends_with("\n")) {
            return "\n";
        }
        return "";
    }
}

std::vector<std::string> Decor::leadingComments() const
{
    std::vector<std::string> result;
    size_t i = 0;
    while (i < prefix.size()) {
        while (i < prefix.size() && (prefix[i] == ' ' || prefix[i] == '\t' || prefix[i] == '\r' || prefix[i] == '\n')) {
            ++i;
        }
        if (i < prefix.size() && prefix[i] == '#') {
            ++i; // 跳过 '#'
            if (i < prefix.size() && prefix[i] == ' ') {
                ++i; // 跳过惯例的单个前导空格
            }
            const size_t start = i;
            while (i < prefix.size() && prefix[i] != '\r' && prefix[i] != '\n') {
                ++i;
            }
            result.emplace_back(prefix.substr(start, i - start));
        }
        else {
            while (i < prefix.size() && prefix[i] != '\n') {
                ++i;
            }
        }
    }
    return result;
}

void Decor::addLeadingComment(std::string_view commentText, std::string_view newline)
{
    const std::string clean = sanitizeSingleLineComment(commentText);

    // 保留 prefix 最后一行的缩进（即最后一个 `\n` 之后的水平空白），将注释插入到该缩进之前
    const size_t lastNl = prefix.rfind('\n');
    const size_t indentStart = (lastNl == std::string::npos) ? 0 : (lastNl + 1);
    const std::string_view indent = std::string_view(prefix).substr(indentStart);

    std::string commentLine;
    commentLine.append(indent);
    commentLine.push_back('#');
    if (!clean.empty()) {
        commentLine.push_back(' ');
        commentLine.append(clean);
    }
    commentLine.append(newline);

    prefix.insert(indentStart, commentLine);
}

void Decor::clearLeadingComments()
{
    const size_t lastNl = prefix.rfind('\n');
    if (lastNl == std::string::npos) {
        if (const size_t hashPos = prefix.find('#'); hashPos != std::string::npos) {
            prefix.erase(hashPos);
        }
        return;
    }

    // 保留最后一行自身的缩进，以及开头原本的纯空行（不含 `#` 的行）
    std::string rebuilt;
    size_t lineStart = 0;
    while (lineStart <= lastNl) {
        const size_t nlPos = prefix.find('\n', lineStart);
        const size_t lineEnd = (nlPos == std::string::npos) ? prefix.size() : (nlPos + 1);
        const std::string_view line = std::string_view(prefix).substr(lineStart, lineEnd - lineStart);
        if (!line.contains('#')) {
            rebuilt.append(line);
        }
        lineStart = lineEnd;
    }
    rebuilt.append(std::string_view(prefix).substr(lastNl + 1));
    prefix = std::move(rebuilt);
}

std::optional<std::string> Decor::inlineComment() const
{
    const size_t hashPos = suffix.find('#');
    if (hashPos == std::string::npos) {
        return std::nullopt;
    }
    size_t start = hashPos + 1;
    if (start < suffix.size() && suffix[start] == ' ') {
        ++start;
    }
    size_t end = start;
    while (end < suffix.size() && suffix[end] != '\r' && suffix[end] != '\n') {
        ++end;
    }
    return suffix.substr(start, end - start);
}

void Decor::setInlineComment(std::string_view commentText)
{
    const std::string clean = sanitizeSingleLineComment(commentText);
    const std::string nl(detectTrailingNewline(suffix));

    std::string wsBeforeHash = " ";
    if (const size_t hashPos = suffix.find('#'); hashPos != std::string::npos) {
        wsBeforeHash = suffix.substr(0, hashPos);
        if (wsBeforeHash.empty()) {
            wsBeforeHash = " ";
        }
    }
    else {
        const size_t contentLen = suffix.size() - nl.size();
        if (contentLen > 0) {
            wsBeforeHash = suffix.substr(0, contentLen);
        }
    }

    suffix.clear();
    suffix.append(wsBeforeHash);
    suffix.push_back('#');
    if (!clean.empty()) {
        suffix.push_back(' ');
        suffix.append(clean);
    }
    suffix.append(nl);
}

void Decor::clearInlineComment()
{
    if (const size_t hashPos = suffix.find('#'); hashPos != std::string::npos) {
        const std::string nl(detectTrailingNewline(suffix));
        suffix = nl;
    }
}

std::string escapeBasicString(std::string_view value, bool multiline, [[maybe_unused]] SpecVersion spec)
{
    std::string out;
    out.reserve(value.size() + 8);
    size_t consecutiveQuotes = 0;

    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        const uint8_t b = (uint8_t)ch;

        if (ch == '"') {
            if (multiline) {
                ++consecutiveQuotes;
                if (consecutiveQuotes == 3) {
                    out.append("\\\"");
                    consecutiveQuotes = 0;
                }
                else {
                    out.push_back('"');
                }
            }
            else {
                out.append("\\\"");
            }
            continue;
        }
        consecutiveQuotes = 0;

        switch (ch)
        {
        case '\\':
            out.append("\\\\");
            break;
        case '\b':
            out.append("\\b");
            break;
        case '\f':
            out.append("\\f");
            break;
        case '\n':
            if (multiline) {
                out.push_back('\n');
            }
            else {
                out.append("\\n");
            }
            break;
        case '\r':
            out.append("\\r");
            break;
        case '\t':
            out.append("\\t");
            break;
        default:
            if (b < 0x20u || b == 0x7Fu) {
                if (b == 0x1Bu && spec == SpecVersion::v1_1) {
                    out.append("\\e");
                }
                else {
                    out += std::format("\\u{:04X}", (uint32_t)b);
                }
            }
            else {
                out.push_back(ch);
            }
            break;
        }
    }

    // 多行基本字符串若以未转义的 `"` 结尾，紧贴闭合 `"""` 会形成 4~5 个连续引号，转义末尾引号消除歧义
    if (multiline && consecutiveQuotes > 0 && out.ends_with('"')) {
        out.pop_back();
        out.append("\\\"");
    }

    return out;
}

std::string formatKeySegment(const KeySegment& seg, SpecVersion spec)
{
    if (!seg.rawRepr.empty()) {
        return seg.rawRepr;
    }

    if (seg.style == KeyStyle::Bare && isValidBareKey(seg.name, spec)) {
        return seg.name;
    }

    if (seg.style == KeyStyle::LiteralQuoted) {
        bool canUseLiteral = !seg.name.contains('\'');
        if (canUseLiteral) {
            for (const char ch : seg.name) {
                const uint8_t b = (uint8_t)ch;
                if ((b < 0x20u && ch != '\t') || b == 0x7Fu) {
                    canUseLiteral = false;
                    break;
                }
            }
        }
        if (canUseLiteral) {
            std::string res;
            res.reserve(seg.name.size() + 2);
            res.push_back('\'');
            res.append(seg.name);
            res.push_back('\'');
            return res;
        }
    }

    std::string res;
    res.reserve(seg.name.size() + 2);
    res.push_back('"');
    res.append(escapeBasicString(seg.name, false, spec));
    res.push_back('"');
    return res;
}

NAMESPACE_END(tomlkit)
