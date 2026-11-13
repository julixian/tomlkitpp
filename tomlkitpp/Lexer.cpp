module;

#include "TKPMacros.hpp"

module tomlkitpp;

import :Lexer;

NAMESPACE_BEGIN(tomlkit::detail)

namespace
{
    constexpr bool isAsciiDigit(char ch) noexcept
    {
        return ch >= '0' && ch <= '9';
    }

    constexpr bool isHexDigit(char ch) noexcept
    {
        return (ch >= '0' && ch <= '9')
            || (ch >= 'a' && ch <= 'f')
            || (ch >= 'A' && ch <= 'F');
    }

    constexpr uint32_t hexValue(char ch) noexcept
    {
        if (ch >= '0' && ch <= '9') {
            return (uint32_t)(ch - '0');
        }
        if (ch >= 'a' && ch <= 'f') {
            return (uint32_t)(ch - 'a' + 10);
        }
        return (uint32_t)(ch - 'A' + 10);
    }

    constexpr bool isHorizontalWs(char ch) noexcept
    {
        return ch == ' ' || ch == '\t';
    }

    constexpr bool isScalarDelimiter(char ch) noexcept
    {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'
            || ch == '#' || ch == ',' || ch == ']' || ch == '}';
    }

    // 检查控制字符：除 `\t` 外，`0x00..0x1F` 与 `0x7F` 在单行字符串/注释中一律禁止
    constexpr bool isForbiddenSingleLineCtrl(uint8_t b) noexcept
    {
        return (b < 0x20u && b != 0x09u) || b == 0x7Fu;
    }

    // 多行字符串允许 `\t`、`\n` 及合法的 `\r\n` 对，禁止其余控制字符
    constexpr bool isForbiddenMultiLineCtrl(uint8_t b) noexcept
    {
        return (b < 0x20u && b != 0x09u && b != 0x0Au && b != 0x0Du) || b == 0x7Fu;
    }

    bool parseFixedDigits(std::string_view sv, size_t offset, size_t count, uint32_t& outVal) noexcept
    {
        if (offset + count > sv.size()) {
            return false;
        }
        uint32_t acc = 0;
        for (size_t i = 0; i < count; ++i) {
            const char ch = sv[offset + i];
            if (!isAsciiDigit(ch)) {
                return false;
            }
            acc = acc * 10u + (uint32_t)(ch - '0');
        }
        outVal = acc;
        return true;
    }
}

Lexer::Lexer(std::string_view source, ParseOptions options, std::string_view sourcePath) noexcept
    : m_source(source)
    , m_options(options)
    , m_sourcePath(sourcePath)
{ }

ParseError Lexer::errorAt(ErrorCategory category, size_t offset, uint32_t length, std::string msg) const
{
    return makeParseError(category, m_source, offset, length, std::move(msg), m_sourcePath);
}

ParseError Lexer::errorHere(ErrorCategory category, std::string msg, uint32_t length) const
{
    return makeParseError(category, m_source, m_pos, length, std::move(msg), m_sourcePath);
}

std::expected<bool, ParseError> Lexer::validateUtf8AndConsumeBom()
{
    size_t errOffset = 0;
    if (!isValidUtf8(m_source, &errOffset)) {
        return std::unexpected(errorAt(
            ErrorCategory::InvalidUtf8,
            errOffset,
            1,
            "Invalid UTF-8 byte sequence in TOML input"
        ));
    }

    bool hasBom = false;
    if (m_pos == 0 && m_source.starts_with("\xEF\xBB\xBF")) {
        m_pos = 3;
        hasBom = true;
    }
    return hasBom;
}

std::string_view Lexer::consumeHorizontalWs() noexcept
{
    const size_t start = m_pos;
    while (!isEof() && isHorizontalWs(peek())) {
        ++m_pos;
    }
    return sliceFrom(start);
}

std::expected<void, ParseError> Lexer::consumeCommentBody()
{
    // 调用前当前字符为 '#'
    ++m_pos;
    while (!isEof()) {
        const char ch = peek();
        if (ch == '\n') {
            break;
        }
        if (ch == '\r') {
            if (peek(1) == '\n') {
                break;
            }
            return std::unexpected(errorHere(
                ErrorCategory::ControlCharacter,
                "Bare carriage return '\\r' without '\\n' is not permitted in comments"
            ));
        }
        if (isForbiddenSingleLineCtrl((uint8_t)ch)) {
            return std::unexpected(errorHere(
                ErrorCategory::ControlCharacter,
                std::format("Control character 0x{:02X} is not permitted in comments", (uint32_t)(uint8_t)ch)
            ));
        }
        ++m_pos;
    }
    return {};
}

std::expected<void, ParseError> Lexer::consumeNewline()
{
    if (peek() == '\n') {
        ++m_pos;
        return {};
    }
    if (peek() == '\r') {
        if (peek(1) == '\n') {
            m_pos += 2;
            return {};
        }
        return std::unexpected(errorHere(
            ErrorCategory::ControlCharacter,
            "Bare carriage return '\\r' without '\\n' is not permitted in TOML"
        ));
    }
    return std::unexpected(errorHere(ErrorCategory::UnexpectedCharacter, "Expected newline"));
}

std::expected<std::string_view, ParseError> Lexer::consumeWsCommentNewlines()
{
    const size_t start = m_pos;
    while (!isEof()) {
        consumeHorizontalWs();
        if (isEof()) {
            break;
        }
        const char ch = peek();
        if (ch == '#') {
            if (auto res = consumeCommentBody(); !res) {
                return std::unexpected(std::move(res.error()));
            }
            if (!isEof()) {
                if (auto nlRes = consumeNewline(); !nlRes) {
                    return std::unexpected(std::move(nlRes.error()));
                }
            }
        }
        else if (ch == '\n' || ch == '\r') {
            if (auto nlRes = consumeNewline(); !nlRes) {
                return std::unexpected(std::move(nlRes.error()));
            }
        }
        else {
            break;
        }
    }
    return sliceFrom(start);
}

std::expected<std::string_view, ParseError> Lexer::consumeLineTrail()
{
    const size_t start = m_pos;
    consumeHorizontalWs();
    if (!isEof() && peek() == '#') {
        if (auto res = consumeCommentBody(); !res) {
            return std::unexpected(std::move(res.error()));
        }
    }
    if (isEof()) {
        return sliceFrom(start);
    }
    if (peek() == '\n' || peek() == '\r') {
        if (auto nlRes = consumeNewline(); !nlRes) {
            return std::unexpected(std::move(nlRes.error()));
        }
        return sliceFrom(start);
    }
    return std::unexpected(errorHere(
        ErrorCategory::UnexpectedCharacter,
        std::format("Unexpected trailing character '{}' after statement; expected newline or '#'", peek())
    ));
}

std::expected<std::string_view, ParseError> Lexer::consumeAfterCommaSameLine()
{
    const size_t start = m_pos;
    consumeHorizontalWs();
    if (!isEof() && peek() == '#') {
        if (auto res = consumeCommentBody(); !res) {
            return std::unexpected(std::move(res.error()));
        }
    }
    if (!isEof() && (peek() == '\n' || peek() == '\r')) {
        if (auto nlRes = consumeNewline(); !nlRes) {
            return std::unexpected(std::move(nlRes.error()));
        }
    }
    return sliceFrom(start);
}

std::expected<KeySegment, ParseError> Lexer::lexKeySegment()
{
    if (isEof()) {
        return std::unexpected(errorHere(ErrorCategory::UnexpectedEof, "Unexpected end of file while expecting a key"));
    }

    const size_t start = m_pos;
    const char ch = peek();

    if (ch == '"' || ch == '\'') {
        if (peek(1) == ch && peek(2) == ch) {
            return std::unexpected(errorAt(
                ErrorCategory::InvalidKey,
                start,
                3,
                "Multi-line strings cannot be used as TOML keys"
            ));
        }

        auto strRes = lexString();
        if (!strRes) {
            return std::unexpected(std::move(strRes.error()));
        }

        KeySegment seg;
        seg.name = std::move(strRes->value);
        seg.style = (ch == '"') ? KeyStyle::BasicQuoted : KeyStyle::LiteralQuoted;
        seg.rawRepr = std::string(strRes->rawRepr);
        return seg;
    }

    while (!isEof() && isBareKeyChar((char32_t)(uint8_t)peek(), m_options.spec)) {
        ++m_pos;
    }

    const std::string_view raw = sliceFrom(start);
    if (raw.empty()) {
        return std::unexpected(errorHere(
            ErrorCategory::InvalidKey,
            "Expected a valid key (bare key or quoted string)"
        ));
    }

    KeySegment seg;
    seg.name = std::string(raw);
    seg.style = KeyStyle::Bare;
    seg.rawRepr = std::string(raw);
    return seg;
}

std::expected<std::vector<KeySegment>, ParseError> Lexer::lexDottedKey()
{
    std::vector<KeySegment> segments;
    uint32_t depth = 0;

    while (true) {
        ++depth;
        if (depth > m_options.maxNestingDepth) {
            return std::unexpected(errorHere(
                ErrorCategory::NestingLimitExceeded,
                std::format("Dotted key exceeds maximum nesting depth ({})", m_options.maxNestingDepth)
            ));
        }

        const std::string_view prefixWs = (segments.empty()) ? std::string_view{} : consumeHorizontalWs();
        auto segRes = lexKeySegment();
        if (!segRes) {
            return std::unexpected(std::move(segRes.error()));
        }
        const std::string_view suffixWs = consumeHorizontalWs();

        segRes->decor.prefix = std::string(prefixWs);
        segRes->decor.suffix = std::string(suffixWs);
        segments.push_back(std::move(*segRes));

        if (!isEof() && peek() == '.') {
            ++m_pos; // 跳过 '.'
            continue;
        }
        break;
    }

    return segments;
}

std::expected<char32_t, ParseError> Lexer::lexHexEscape(size_t digits, size_t escapeStartOffset)
{
    if (m_pos + digits > m_source.size()) {
        return std::unexpected(errorAt(
            ErrorCategory::InvalidEscape,
            escapeStartOffset,
            (uint32_t)(m_source.size() - escapeStartOffset),
            "Incomplete hexadecimal escape sequence in string"
        ));
    }

    uint32_t codepoint = 0;
    for (size_t i = 0; i < digits; ++i) {
        const char ch = m_source[m_pos + i];
        if (!isHexDigit(ch)) {
            return std::unexpected(errorAt(
                ErrorCategory::InvalidEscape,
                escapeStartOffset,
                (uint32_t)(m_pos + i + 1 - escapeStartOffset),
                std::format("Invalid hexadecimal digit '{}' in string escape", ch)
            ));
        }
        codepoint = (codepoint << 4) | hexValue(ch);
    }

    m_pos += digits;

    // Unicode 标量值校验：拒绝 UTF-16 代理对 U+D800..U+DFFF 与 > U+10FFFF
    if ((codepoint >= 0xD800u && codepoint <= 0xDFFFu) || codepoint > 0x10FFFFu) {
        return std::unexpected(errorAt(
            ErrorCategory::InvalidEscape,
            escapeStartOffset,
            (uint32_t)(m_pos - escapeStartOffset),
            std::format("Escaped codepoint U+{:04X} is not a valid Unicode scalar value", codepoint)
        ));
    }

    return (char32_t)codepoint;
}

std::expected<LexedString, ParseError> Lexer::lexBasicString(bool multiline, size_t startOffset)
{
    // 已确认首字符为 '"'
    m_pos += multiline ? 3 : 1;

    // 多行基本字符串若紧跟单个换行符（`\n` 或 `\r\n`），按规范剥离首行换行
    if (multiline && !isEof()) {
        if (peek() == '\n') {
            ++m_pos;
        }
        else if (peek() == '\r') {
            if (peek(1) == '\n') {
                m_pos += 2;
            }
            else {
                return std::unexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    "Bare carriage return '\\r' without '\\n' in multi-line basic string"
                ));
            }
        }
    }

    std::string decoded;
    while (!isEof()) {
        const char ch = peek();

        if (ch == '"') {
            if (!multiline) {
                ++m_pos;
                return LexedString{
                    .value = std::move(decoded),
                    .style = StringStyle::Basic,
                    .rawRepr = sliceFrom(startOffset)
                };
            }

            size_t quoteRun = 0;
            while (peek(quoteRun) == '"') {
                ++quoteRun;
            }
            if (quoteRun < 3) {
                decoded.append(quoteRun, '"');
                m_pos += quoteRun;
                continue;
            }
            const size_t bodyQuotes = std::min<size_t>(quoteRun - 3, 2);
            decoded.append(bodyQuotes, '"');
            m_pos += bodyQuotes + 3;
            return LexedString{
                .value = std::move(decoded),
                .style = StringStyle::MultilineBasic,
                .rawRepr = sliceFrom(startOffset)
            };
        }

        if (ch == '\\') {
            const size_t escStart = m_pos;
            ++m_pos; // 跳过 '\\'
            if (isEof()) {
                return std::unexpected(errorAt(
                    ErrorCategory::UnexpectedEof,
                    escStart,
                    1,
                    "Unterminated escape sequence at end of file"
                ));
            }

            const char esc = peek();

            // 多行基本字符串的行尾反斜杠续行符：`\` + 可选水平空白 + 换行 -> 吞掉后续所有空白与换行
            if (multiline && (isHorizontalWs(esc) || esc == '\n' || esc == '\r')) {
                consumeHorizontalWs();
                if (!isEof() && (peek() == '\n' || peek() == '\r')) {
                    if (auto nlRes = consumeNewline(); !nlRes) {
                        return std::unexpected(std::move(nlRes.error()));
                    }
                    while (!isEof()) {
                        consumeHorizontalWs();
                        if (!isEof() && (peek() == '\n' || peek() == '\r')) {
                            if (auto nlRes = consumeNewline(); !nlRes) {
                                return std::unexpected(std::move(nlRes.error()));
                            }
                        }
                        else {
                            break;
                        }
                    }
                    continue;
                }
                return std::unexpected(errorAt(
                    ErrorCategory::InvalidEscape,
                    escStart,
                    (uint32_t)(m_pos - escStart + 1),
                    "Invalid escape sequence: backslash followed by whitespace without newline"
                ));
            }

            ++m_pos;
            switch (esc)
            {
            case '"':  decoded.push_back('"'); break;
            case '\\': decoded.push_back('\\'); break;
            case 'b':  decoded.push_back('\b'); break;
            case 'f':  decoded.push_back('\f'); break;
            case 'n':  decoded.push_back('\n'); break;
            case 'r':  decoded.push_back('\r'); break;
            case 't':  decoded.push_back('\t'); break;
            case 'e':
                if (m_options.spec == SpecVersion::v1_0) {
                    return std::unexpected(errorAt(
                        ErrorCategory::SpecRestriction,
                        escStart,
                        2,
                        "Escape sequence '\\e' requires TOML v1.1"
                    ));
                }
                decoded.push_back('\x1B');
                break;
            case 'x':
            {
                if (m_options.spec == SpecVersion::v1_0) {
                    return std::unexpected(errorAt(
                        ErrorCategory::SpecRestriction,
                        escStart,
                        2,
                        "Hexadecimal escape '\\xHH' requires TOML v1.1"
                    ));
                }
                auto cpRes = lexHexEscape(2, escStart);
                if (!cpRes) {
                    return std::unexpected(std::move(cpRes.error()));
                }
                appendUtf8Codepoint(decoded, *cpRes);
                break;
            }
            case 'u':
            {
                auto cpRes = lexHexEscape(4, escStart);
                if (!cpRes) {
                    return std::unexpected(std::move(cpRes.error()));
                }
                appendUtf8Codepoint(decoded, *cpRes);
                break;
            }
            case 'U':
            {
                auto cpRes = lexHexEscape(8, escStart);
                if (!cpRes) {
                    return std::unexpected(std::move(cpRes.error()));
                }
                appendUtf8Codepoint(decoded, *cpRes);
                break;
            }
            default:
                return std::unexpected(errorAt(
                    ErrorCategory::InvalidEscape,
                    escStart,
                    2,
                    std::format("Invalid string escape sequence '\\{}'", esc)
                ));
            }
            continue;
        }

        if (!multiline) {
            if (ch == '\n' || ch == '\r') {
                return std::unexpected(errorAt(
                    ErrorCategory::UnexpectedCharacter,
                    startOffset,
                    (uint32_t)(m_pos - startOffset),
                    "Unterminated single-line basic string before newline"
                ));
            }
            if (isForbiddenSingleLineCtrl((uint8_t)ch)) {
                return std::unexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    std::format("Control character 0x{:02X} is not permitted in basic string", (uint32_t)(uint8_t)ch)
                ));
            }
            decoded.push_back(advance());
        }
        else {
            if (ch == '\r') {
                if (peek(1) != '\n') {
                    return std::unexpected(errorHere(
                        ErrorCategory::ControlCharacter,
                        "Bare carriage return '\\r' without '\\n' is not permitted in multi-line basic string"
                    ));
                }
                m_pos += 2;
                decoded.push_back('\n'); // 多行字符串语义值统一归一化换行为 `\n`，原始文本保留在 `rawRepr`
                continue;
            }
            if (isForbiddenMultiLineCtrl((uint8_t)ch)) {
                return std::unexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    std::format("Control character 0x{:02X} is not permitted in multi-line basic string", (uint32_t)(uint8_t)ch)
                ));
            }
            decoded.push_back(advance());
        }
    }

    return std::unexpected(errorAt(
        ErrorCategory::UnexpectedEof,
        startOffset,
        (uint32_t)(m_pos - startOffset),
        multiline ? "Unterminated multi-line basic string" : "Unterminated basic string"
    ));
}

std::expected<LexedString, ParseError> Lexer::lexLiteralString(bool multiline, size_t startOffset)
{
    m_pos += multiline ? 3 : 1;

    if (multiline && !isEof()) {
        if (peek() == '\n') {
            ++m_pos;
        }
        else if (peek() == '\r') {
            if (peek(1) == '\n') {
                m_pos += 2;
            }
            else {
                return std::unexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    "Bare carriage return '\\r' without '\\n' in multi-line literal string"
                ));
            }
        }
    }

    std::string decoded;
    while (!isEof()) {
        const char ch = peek();

        if (ch == '\'') {
            if (!multiline) {
                ++m_pos;
                return LexedString{
                    .value = std::move(decoded),
                    .style = StringStyle::Literal,
                    .rawRepr = sliceFrom(startOffset)
                };
            }

            size_t quoteRun = 0;
            while (peek(quoteRun) == '\'') {
                ++quoteRun;
            }
            if (quoteRun < 3) {
                decoded.append(quoteRun, '\'');
                m_pos += quoteRun;
                continue;
            }
            const size_t bodyQuotes = std::min<size_t>(quoteRun - 3, 2);
            decoded.append(bodyQuotes, '\'');
            m_pos += bodyQuotes + 3;
            return LexedString{
                .value = std::move(decoded),
                .style = StringStyle::MultilineLiteral,
                .rawRepr = sliceFrom(startOffset)
            };
        }

        if (!multiline) {
            if (ch == '\n' || ch == '\r') {
                return std::unexpected(errorAt(
                    ErrorCategory::UnexpectedCharacter,
                    startOffset,
                    (uint32_t)(m_pos - startOffset),
                    "Unterminated single-line literal string before newline"
                ));
            }
            if (isForbiddenSingleLineCtrl((uint8_t)ch)) {
                return std::unexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    std::format("Control character 0x{:02X} is not permitted in literal string", (uint32_t)(uint8_t)ch)
                ));
            }
            decoded.push_back(advance());
        }
        else {
            if (ch == '\r') {
                if (peek(1) != '\n') {
                    return std::unexpected(errorHere(
                        ErrorCategory::ControlCharacter,
                        "Bare carriage return '\\r' without '\\n' is not permitted in multi-line literal string"
                    ));
                }
                m_pos += 2;
                decoded.push_back('\n');
                continue;
            }
            if (isForbiddenMultiLineCtrl((uint8_t)ch)) {
                return std::unexpected(errorHere(
                    ErrorCategory::ControlCharacter,
                    std::format("Control character 0x{:02X} is not permitted in multi-line literal string", (uint32_t)(uint8_t)ch)
                ));
            }
            decoded.push_back(advance());
        }
    }

    return std::unexpected(errorAt(
        ErrorCategory::UnexpectedEof,
        startOffset,
        (uint32_t)(m_pos - startOffset),
        multiline ? "Unterminated multi-line literal string" : "Unterminated literal string"
    ));
}

std::expected<LexedString, ParseError> Lexer::lexString()
{
    const size_t start = m_pos;
    if (peek() == '"') {
        const bool ml = (peek(1) == '"' && peek(2) == '"');
        return lexBasicString(ml, start);
    }
    if (peek() == '\'') {
        const bool ml = (peek(1) == '\'' && peek(2) == '\'');
        return lexLiteralString(ml, start);
    }
    return std::unexpected(errorHere(ErrorCategory::UnexpectedCharacter, "Expected string literal"));
}

std::expected<LexedScalar, ParseError> Lexer::lexScalar()
{
    if (isEof()) {
        return std::unexpected(errorHere(ErrorCategory::UnexpectedEof, "Expected scalar value before end of file"));
    }

    const size_t start = m_pos;
    while (!isEof() && !isScalarDelimiter(peek())) {
        ++m_pos;
    }

    // 处理以空格作为日期与时间分隔符的 RFC 3339 日期时间，如 `1979-05-27 07:32:00Z` 或 v1.1 `1979-05-27 07:32`
    if ((m_pos - start) == 10
        && m_source[start + 4] == '-'
        && m_source[start + 7] == '-'
        && peek() == ' '
        && isAsciiDigit(peek(1)))
    {
        ++m_pos; // 包含分隔空格
        while (!isEof() && !isScalarDelimiter(peek())) {
            ++m_pos;
        }
    }

    const std::string_view token = sliceFrom(start);
    if (token.empty()) {
        return std::unexpected(errorHere(ErrorCategory::UnexpectedCharacter, "Expected a value"));
    }

    if (token == "true") {
        return LexedScalar{ .payload = true, .rawRepr = token };
    }
    if (token == "false") {
        return LexedScalar{ .payload = false, .rawRepr = token };
    }

    // 区分日期/时间与数值字面量：
    // 1) 含 ':' 必为时间或日期时间（例如 `07:32`、`1979-05-27T07:32:00Z`）
    // 2) 首字符为数字且在位置 4 或更早处出现 '-'（非科学计数法 `1e-5`）必为日期字面量（排除 `0x`/`0o`/`0b`）
    const bool startsWithRadixPrefix = token.starts_with("0x") || token.starts_with("0X")
        || token.starts_with("0o") || token.starts_with("0O")
        || token.starts_with("0b") || token.starts_with("0B");

    if (!startsWithRadixPrefix) {
        if (token.contains(':')) {
            return lexDateOrTime(start, token);
        }
        if (isAsciiDigit(token[0])) {
            const size_t dashPos = token.find('-');
            const size_t expPos = token.find_first_of("eE");
            if (dashPos != std::string_view::npos && (expPos == std::string_view::npos || dashPos < expPos)) {
                return lexDateOrTime(start, token);
            }
        }
    }

    return lexNumber(start, token);
}

std::expected<LexedScalar, ParseError> Lexer::lexDateOrTime(size_t startOffset, std::string_view token)
{
    const auto makeDtErrFunc = [&](std::string msg)
        {
            return std::unexpected(errorAt(ErrorCategory::InvalidDateTime, startOffset, (uint32_t)token.size(), std::move(msg)));
        };

    const auto parseTimePartFunc = [&](std::string_view sv, size_t& idx, LocalTime& outTime) -> std::expected<void, ParseError>
        {
            uint32_t hh = 0;
            uint32_t mm = 0;
            if (!parseFixedDigits(sv, idx, 2, hh) || idx + 2 >= sv.size() || sv[idx + 2] != ':') {
                return makeDtErrFunc("Invalid time format; expected HH:MM");
            }
            if (!parseFixedDigits(sv, idx + 3, 2, mm)) {
                return makeDtErrFunc("Invalid minute digits in time literal");
            }
            idx += 5;

            outTime.hour = (uint8_t)hh;
            outTime.minute = (uint8_t)mm;
            outTime.second = 0;
            outTime.nanosecond = 0;
            outTime.subsecondDigits = 0;
            outTime.hasSeconds = false;

            if (idx < sv.size() && sv[idx] == ':') {
                ++idx;
                uint32_t ss = 0;
                if (!parseFixedDigits(sv, idx, 2, ss)) {
                    return makeDtErrFunc("Invalid second digits in time literal");
                }
                idx += 2;
                outTime.second = (uint8_t)ss;
                outTime.hasSeconds = true;

                if (idx < sv.size() && sv[idx] == '.') {
                    ++idx;
                    const size_t fracStart = idx;
                    while (idx < sv.size() && isAsciiDigit(sv[idx])) {
                        ++idx;
                    }
                    const size_t fracLen = idx - fracStart;
                    if (fracLen == 0) {
                        return makeDtErrFunc("Fractional seconds must contain at least one digit after '.'");
                    }
                    uint32_t nanos = 0;
                    const size_t takeDigits = std::min<size_t>(fracLen, 9);
                    for (size_t i = 0; i < takeDigits; ++i) {
                        nanos = nanos * 10u + (uint32_t)(sv[fracStart + i] - '0');
                    }
                    for (size_t i = takeDigits; i < 9; ++i) {
                        nanos *= 10u;
                    }
                    outTime.nanosecond = nanos;
                    outTime.subsecondDigits = (uint8_t)std::min<size_t>(fracLen, 9);
                }
            }
            else if (m_options.spec == SpecVersion::v1_0) {
                return std::unexpected(errorAt(
                    ErrorCategory::SpecRestriction,
                    startOffset,
                    (uint32_t)token.size(),
                    "Omitting seconds in time/datetime requires TOML v1.1"
                ));
            }

            if (!outTime.isValid()) {
                return makeDtErrFunc("Time value out of valid RFC 3339 range");
            }
            return {};
        };

    // 1. 纯本地时间 `HH:MM[:SS[.frac]]`
    if (token.size() >= 5 && token[2] == ':') {
        size_t idx = 0;
        LocalTime lt{};
        if (auto res = parseTimePartFunc(token, idx, lt); !res) {
            return std::unexpected(std::move(res.error()));
        }
        if (idx != token.size()) {
            return makeDtErrFunc("Unexpected trailing characters in local time (offsets require a date part)");
        }
        return LexedScalar{ .payload = lt, .rawRepr = token };
    }

    // 2. 日期打头：`YYYY-MM-DD`
    uint32_t year = 0;
    uint32_t month = 0;
    uint32_t day = 0;
    if (token.size() < 10
        || !parseFixedDigits(token, 0, 4, year)
        || token[4] != '-'
        || !parseFixedDigits(token, 5, 2, month)
        || token[7] != '-'
        || !parseFixedDigits(token, 8, 2, day))
    {
        return makeDtErrFunc("Invalid date format; expected YYYY-MM-DD");
    }

    LocalDate ld{
        .year = (uint16_t)year,
        .month = (uint8_t)month,
        .day = (uint8_t)day
    };
    if (!ld.isValid()) {
        return makeDtErrFunc("Calendar date out of valid range (check month/day or leap year)");
    }

    if (token.size() == 10) {
        return LexedScalar{ .payload = ld, .rawRepr = token };
    }

    const char delim = token[10];
    if (delim != 'T' && delim != 't' && delim != ' ') {
        return makeDtErrFunc("Invalid date-time separator; expected 'T', 't', or space");
    }

    size_t idx = 11;
    LocalTime lt{};
    if (auto res = parseTimePartFunc(token, idx, lt); !res) {
        return std::unexpected(std::move(res.error()));
    }

    if (idx == token.size()) {
        LocalDateTime ldt{
            .date = ld,
            .time = lt,
            .delimiter = delim
        };
        return LexedScalar{ .payload = ldt, .rawRepr = token };
    }

    // 3. 解析时区偏移 `Z` / `z` / `+HH:MM` / `-HH:MM`
    OffsetDateTime odt{
        .date = ld,
        .time = lt,
        .delimiter = delim
    };

    const char tzLead = token[idx];
    if (tzLead == 'Z' || tzLead == 'z') {
        ++idx;
        if (idx != token.size()) {
            return makeDtErrFunc("Unexpected characters after timezone 'Z'");
        }
        odt.isUtcZ = true;
        odt.zChar = tzLead;
        return LexedScalar{ .payload = odt, .rawRepr = token };
    }

    if (tzLead == '+' || tzLead == '-') {
        ++idx;
        uint32_t offH = 0;
        uint32_t offM = 0;
        if (idx + 5 != token.size()
            || !parseFixedDigits(token, idx, 2, offH)
            || token[idx + 2] != ':'
            || !parseFixedDigits(token, idx + 3, 2, offM))
        {
            return makeDtErrFunc("Invalid timezone offset; expected +HH:MM or -HH:MM");
        }
        if (offH > 23 || offM > 59) {
            return makeDtErrFunc("Timezone offset hours/minutes out of valid range (00:00..23:59)");
        }
        const int16_t totalMins = (int16_t)(offH * 60u + offM);
        odt.isUtcZ = false;
        odt.negativeZeroOffset = (tzLead == '-' && totalMins == 0);
        odt.offsetMinutes = (tzLead == '-') ? (int16_t)(-totalMins) : totalMins;
        return LexedScalar{ .payload = odt, .rawRepr = token };
    }

    return makeDtErrFunc("Unexpected characters after time component in datetime literal");
}

std::expected<LexedScalar, ParseError> Lexer::lexNumber(size_t startOffset, std::string_view token)
{
    const auto makeNumErrFunc = [&](std::string msg)
        {
            return std::unexpected(errorAt(ErrorCategory::InvalidNumber, startOffset, (uint32_t)token.size(), std::move(msg)));
        };

    std::string_view rest = token;
    char sign = '\0';
    if (rest.starts_with('+') || rest.starts_with('-')) {
        sign = rest[0];
        rest.remove_prefix(1);
    }

    if (rest.empty()) {
        return makeNumErrFunc("Incomplete numeric sign without digits");
    }

    // 特殊浮点字面量：`inf`、`nan`（必须严格小写）
    if (rest == "inf") {
        const double val = (sign == '-') ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        return LexedScalar{ .payload = val, .rawRepr = token };
    }
    if (rest == "nan") {
        const double val = (sign == '-') ? -std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::quiet_NaN();
        return LexedScalar{ .payload = val, .rawRepr = token };
    }

    // 非十进制整数：`0x`、`0o`、`0b`（禁止正负号，前缀必须严格小写）
    if (rest.size() >= 2 && rest[0] == '0' && (rest[1] == 'x' || rest[1] == 'X' || rest[1] == 'o' || rest[1] == 'O' || rest[1] == 'b' || rest[1] == 'B')) {
        if (sign != '\0') {
            return makeNumErrFunc("Radix-prefixed integers (0x, 0o, 0b) cannot have a '+' or '-' sign");
        }
        const char prefixCh = rest[1];
        if (prefixCh == 'X' || prefixCh == 'O' || prefixCh == 'B') {
            return makeNumErrFunc("Radix prefix must be lowercase ('0x', '0o', or '0b')");
        }

        IntegerRadix radix = IntegerRadix::Hexadecimal;
        uint32_t base = 16;
        if (prefixCh == 'o') {
            radix = IntegerRadix::Octal;
            base = 8;
        }
        else if (prefixCh == 'b') {
            radix = IntegerRadix::Binary;
            base = 2;
        }

        const std::string_view digits = rest.substr(2);
        if (digits.empty()) {
            return makeNumErrFunc("Missing digits after integer radix prefix");
        }

        const auto isValidRadixDigitFunc = [&](char ch) noexcept
            {
                if (base == 2) {
                    return ch == '0' || ch == '1';
                }
                if (base == 8) {
                    return ch >= '0' && ch <= '7';
                }
                return isHexDigit(ch);
            };

        uint64_t acc = 0;
        constexpr uint64_t kMaxSigned = (uint64_t)std::numeric_limits<int64_t>::max();
        bool prevUnderscore = false;

        for (size_t i = 0; i < digits.size(); ++i) {
            const char ch = digits[i];
            if (ch == '_') {
                if (i == 0 || i + 1 == digits.size() || prevUnderscore) {
                    return makeNumErrFunc("Underscores in integer literal must be surrounded by valid digits");
                }
                prevUnderscore = true;
                continue;
            }
            prevUnderscore = false;

            if (!isValidRadixDigitFunc(ch)) {
                return makeNumErrFunc(std::format("Invalid digit '{}' for base-{} integer literal", ch, base));
            }
            const uint64_t d = (uint64_t)hexValue(ch);
            if (acc > (kMaxSigned - d) / base) {
                return makeNumErrFunc("Integer literal exceeds 64-bit signed integer range");
            }
            acc = acc * base + d;
        }

        return LexedScalar{
            .payload = (int64_t)acc,
            .integerRadix = radix,
            .rawRepr = token
        };
    }

    // 判断是否为浮点数（含 `.` 或 `e`/`E`）
    const bool isFloat = rest.contains('.') || rest.contains('e') || rest.contains('E');

    if (!isFloat) {
        if (rest.size() > 1 && rest[0] == '0') {
            return makeNumErrFunc("Leading zeros are not allowed in decimal integer literals");
        }

        uint64_t acc = 0;
        const uint64_t maxMag = (sign == '-')
            ? ((uint64_t)std::numeric_limits<int64_t>::max() + 1ull)
            : (uint64_t)std::numeric_limits<int64_t>::max();

        bool prevUnderscore = false;
        for (size_t i = 0; i < rest.size(); ++i) {
            const char ch = rest[i];
            if (ch == '_') {
                if (i == 0 || i + 1 == rest.size() || prevUnderscore) {
                    return makeNumErrFunc("Underscores in decimal integer must be surrounded by digits");
                }
                prevUnderscore = true;
                continue;
            }
            prevUnderscore = false;

            if (!isAsciiDigit(ch)) {
                return makeNumErrFunc(std::format("Invalid character '{}' in numeric literal", ch));
            }
            const uint64_t d = (uint64_t)(ch - '0');
            if (acc > (maxMag - d) / 10ull) {
                return makeNumErrFunc("Decimal integer literal overflows 64-bit signed integer range");
            }
            acc = acc * 10ull + d;
        }

        int64_t signedVal = 0;
        if (sign == '-') {
            if (acc == (uint64_t)std::numeric_limits<int64_t>::max() + 1ull) {
                signedVal = std::numeric_limits<int64_t>::min();
            }
            else {
                signedVal = -(int64_t)acc;
            }
        }
        else {
            signedVal = (int64_t)acc;
        }

        return LexedScalar{
            .payload = signedVal,
            .integerRadix = IntegerRadix::Decimal,
            .rawRepr = token
        };
    }

    // 浮点数语法精确校验：`float-int-part ( exp / frac [ exp ] )`
    const auto validateDigitSeqFunc = [](std::string_view part, bool allowLeadingZero) noexcept -> bool
        {
            if (part.empty()) {
                return false;
            }
            if (!allowLeadingZero && part.size() > 1 && part[0] == '0') {
                return false;
            }
            bool prevUs = false;
            for (size_t i = 0; i < part.size(); ++i) {
                const char ch = part[i];
                if (ch == '_') {
                    if (i == 0 || i + 1 == part.size() || prevUs) {
                        return false;
                    }
                    prevUs = true;
                }
                else if (isAsciiDigit(ch)) {
                    prevUs = false;
                }
                else {
                    return false;
                }
            }
            return true;
        };

    const size_t dotPos = rest.find('.');
    const size_t expPos = rest.find_first_of("eE");

    if (dotPos != std::string_view::npos && expPos != std::string_view::npos && expPos < dotPos) {
        return makeNumErrFunc("Decimal point cannot appear in exponent part of float literal");
    }

    const size_t intEnd = (dotPos != std::string_view::npos) ? dotPos : expPos;
    const std::string_view intPart = rest.substr(0, intEnd);
    if (!validateDigitSeqFunc(intPart, false)) {
        return makeNumErrFunc("Invalid integer part in float literal");
    }

    if (dotPos != std::string_view::npos) {
        const size_t fracStart = dotPos + 1;
        const size_t fracEnd = (expPos != std::string_view::npos) ? expPos : rest.size();
        if (rest.find('.', fracStart) != std::string_view::npos) {
            return makeNumErrFunc("Multiple decimal points in float literal");
        }
        const std::string_view fracPart = rest.substr(fracStart, fracEnd - fracStart);
        if (!validateDigitSeqFunc(fracPart, true)) {
            return makeNumErrFunc("Invalid fractional part in float literal");
        }
    }

    if (expPos != std::string_view::npos) {
        if (rest.find_first_of("eE", expPos + 1) != std::string_view::npos) {
            return makeNumErrFunc("Multiple exponent markers in float literal");
        }
        std::string_view expPart = rest.substr(expPos + 1);
        if (expPart.starts_with('+') || expPart.starts_with('-')) {
            expPart.remove_prefix(1);
        }
        if (!validateDigitSeqFunc(expPart, true)) {
            return makeNumErrFunc("Invalid exponent digits in float literal");
        }
    }

    std::string clean;
    clean.reserve(token.size());
    if (sign == '-') {
        clean.push_back('-');
    }
    for (const char ch : rest) {
        if (ch != '_') {
            clean.push_back(ch);
        }
    }

    double floatVal = 0.0;
    const auto [ptr, ec] = std::from_chars(clean.data(), clean.data() + clean.size(), floatVal);
    if (ptr != clean.data() + clean.size()) {
        return makeNumErrFunc("Failed to parse float literal");
    }
    if (ec == std::errc::result_out_of_range) {
        // IEEE-754 下溢为 ±0.0 允许保留，上溢为 ±inf 也由 `from_chars` 写出（符合 TOML 1.1 #1058）
        if (sign == '-' && floatVal == 0.0 && !std::signbit(floatVal)) {
            floatVal = -0.0;
        }
    }

    return LexedScalar{
        .payload = floatVal,
        .rawRepr = token
    };
}

NAMESPACE_END(tomlkit::detail)
