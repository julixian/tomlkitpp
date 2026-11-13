module;

#include "TKPMacros.hpp"

module tomlkitpp;

import :Error;

NAMESPACE_BEGIN(tomlkit)

namespace
{
    uint32_t countUtf8Codepoints(std::string_view sv) noexcept
    {
        uint32_t count = 0;
        for (const char ch : sv) {
            const uint8_t b = (uint8_t)ch;
            if ((b & 0xC0u) != 0x80u) {
                ++count;
            }
        }
        return count;
    }
}

ParseError makeParseError(
    ErrorCategory category,
    std::string_view source,
    size_t byteOffset,
    uint32_t byteLength,
    std::string message,
    std::string_view sourcePath
)
{
    const size_t clampedOffset = std::min(byteOffset, source.size());

    uint32_t line = 1;
    size_t lineStart = 0;
    for (size_t i = 0; i < clampedOffset; ++i) {
        if (source[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }

    size_t lineEnd = clampedOffset;
    while (lineEnd < source.size() && source[lineEnd] != '\n' && source[lineEnd] != '\r') {
        ++lineEnd;
    }

    const std::string_view prefixOnLine = source.substr(lineStart, clampedOffset - lineStart);
    const uint32_t column = countUtf8Codepoints(prefixOnLine) + 1;

    uint32_t caretCount = 1;
    if (byteLength > 1 && clampedOffset < lineEnd) {
        const size_t spanEnd = std::min(clampedOffset + (size_t)byteLength, lineEnd);
        caretCount = std::max<uint32_t>(1, countUtf8Codepoints(source.substr(clampedOffset, spanEnd - clampedOffset)));
    }

    // 将制表符展开为空格，保证诊断行与下方 `^` 指示器对齐
    std::string displayLine;
    displayLine.reserve((lineEnd - lineStart) + 8);
    uint32_t visualCaretPos = 0;
    const std::string_view fullLine = source.substr(lineStart, lineEnd - lineStart);
    for (size_t i = 0; i < fullLine.size();) {
        const bool beforeTarget = (lineStart + i) < clampedOffset;
        if (fullLine[i] == '\t') {
            displayLine.append("    ");
            if (beforeTarget) {
                visualCaretPos += 4;
            }
            ++i;
        }
        else {
            const uint8_t b = (uint8_t)fullLine[i];
            size_t seqLen = 1;
            if ((b & 0x80u) == 0u) {
                seqLen = 1;
            }
            else if ((b & 0xE0u) == 0xC0u) {
                seqLen = 2;
            }
            else if ((b & 0xF0u) == 0xE0u) {
                seqLen = 3;
            }
            else if ((b & 0xF8u) == 0xF0u) {
                seqLen = 4;
            }
            seqLen = std::min(seqLen, fullLine.size() - i);
            displayLine.append(fullLine.substr(i, seqLen));
            if (beforeTarget) {
                ++visualCaretPos;
            }
            i += seqLen;
        }
    }

    const std::string lineNumStr = std::to_string(line);
    const std::string gutterPad(lineNumStr.size(), ' ');
    const std::string locHeader = sourcePath.empty()
        ? std::format("line {}, column {}", line, column)
        : std::format("{}:{}:{}", sourcePath, line, column);

    std::string diag = std::format("TOML parse error at {}: {}\n", locHeader, message);
    diag += std::format(" {} |\n", gutterPad);
    diag += std::format(" {} | {}\n", lineNumStr, displayLine);
    diag += std::format(" {} | {}{}", gutterPad, std::string(visualCaretPos, ' '), std::string(caretCount, '^'));

    return ParseError{
        .category = category,
        .location = SourceLocation{
            .offset = clampedOffset,
            .line = line,
            .column = column,
            .length = std::max<uint32_t>(1, byteLength)
        },
        .message = std::move(message),
        .sourcePath = std::string(sourcePath),
        .formattedDiagnostic = std::move(diag)
    };
}

NAMESPACE_END(tomlkit)
