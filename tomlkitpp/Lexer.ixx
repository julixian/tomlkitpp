module;

#include "TKPMacros.hpp"

export module tomlkitpp:Lexer;

export import std;
export import :Decor;
export import :Error;
export import :Spec;
export import :Types;

export NAMESPACE_BEGIN(tomlkit::detail)

struct LexedString
{
    std::string value;
    StringStyle style = StringStyle::Basic;
    std::string_view rawRepr;
};

using ScalarPayload = std::variant<
    bool,
    int64_t,
    double,
    LocalDate,
    LocalTime,
    LocalDateTime,
    OffsetDateTime
>;

struct LexedScalar
{
    ScalarPayload payload{ false };
    IntegerRadix integerRadix = IntegerRadix::Decimal;
    std::string_view rawRepr;
};

// 面向上下文敏感语法（键域 vs 值域 vs Trivia 域）的零拷贝游标词法扫描器
class Lexer
{
public:
    Lexer(std::string_view source, ParseOptions options = {}, std::string_view sourcePath = "") noexcept;

    // 校验整份文档的 UTF-8 合法性，并识别首部可选 UTF-8 BOM（`\xEF\xBB\xBF`）
    std::expected<bool, ParseError> validateUtf8AndConsumeBom();

    bool isEof() const noexcept { return m_pos >= m_source.size(); }
    size_t position() const noexcept { return m_pos; }
    void setPosition(size_t pos) noexcept { m_pos = std::min(pos, m_source.size()); }

    char peek(size_t lookahead = 0) const noexcept
    {
        const size_t idx = m_pos + lookahead;
        return idx < m_source.size() ? m_source[idx] : '\0';
    }

    char advance() noexcept
    {
        return m_pos < m_source.size() ? m_source[m_pos++] : '\0';
    }

    std::string_view sliceFrom(size_t startPos) const noexcept
    {
        return m_source.substr(startPos, m_pos - startPos);
    }

    std::string_view source() const noexcept { return m_source; }
    const ParseOptions& options() const noexcept { return m_options; }

    ParseError errorAt(ErrorCategory category, size_t offset, uint32_t length, std::string msg) const;
    ParseError errorHere(ErrorCategory category, std::string msg, uint32_t length = 1) const;

    // Trivia 扫描接口（全部返回指向原始 `m_source` 的零拷贝视图）
    std::string_view consumeHorizontalWs() noexcept;
    std::expected<std::string_view, ParseError> consumeWsCommentNewlines();
    std::expected<std::string_view, ParseError> consumeLineTrail();
    std::expected<std::string_view, ParseError> consumeAfterCommaSameLine();

    // 键域扫描接口
    std::expected<KeySegment, ParseError> lexKeySegment();
    std::expected<std::vector<KeySegment>, ParseError> lexDottedKey();

    // 值域扫描接口
    std::expected<LexedString, ParseError> lexString();
    std::expected<LexedScalar, ParseError> lexScalar();

private:
    std::expected<void, ParseError> consumeCommentBody();
    std::expected<void, ParseError> consumeNewline();

    std::expected<LexedString, ParseError> lexBasicString(bool multiline, size_t startOffset);
    std::expected<LexedString, ParseError> lexLiteralString(bool multiline, size_t startOffset);
    std::expected<char32_t, ParseError> lexHexEscape(size_t digits, size_t escapeStartOffset);

    std::expected<LexedScalar, ParseError> lexDateOrTime(size_t startOffset, std::string_view token);
    std::expected<LexedScalar, ParseError> lexNumber(size_t startOffset, std::string_view token);

    std::string_view m_source;
    ParseOptions m_options{};
    std::string_view m_sourcePath;
    size_t m_pos = 0;
};

NAMESPACE_END(tomlkit::detail)
