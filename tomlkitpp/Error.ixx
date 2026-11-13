module;

#include "TKPMacros.hpp"

export module tomlkitpp:Error;

export import std;
export import :Spec;

export NAMESPACE_BEGIN(tomlkit)

struct SourceLocation
{
    size_t offset = 0;
    uint32_t line = 1;
    uint32_t column = 1;
    uint32_t length = 1;

    friend bool operator==(const SourceLocation&, const SourceLocation&) = default;
};

enum class ErrorCategory : uint8_t
{
    InvalidUtf8,
    ControlCharacter,
    UnexpectedCharacter,
    UnexpectedEof,
    InvalidEscape,
    InvalidNumber,
    InvalidDateTime,
    InvalidKey,
    DuplicateKey,
    TableRedefinition,
    DottedKeyConflict,
    InlineTableImmutable,
    StaticArrayConflict,
    SpecRestriction,
    NestingLimitExceeded,
    TypeMismatch,
    IoError
};

struct ParseError
{
    ErrorCategory category = ErrorCategory::UnexpectedCharacter;
    SourceLocation location{};
    std::string message;
    std::string sourcePath;
    std::string formattedDiagnostic;

    const std::string& format() const noexcept { return formattedDiagnostic; }
    const std::string& formatDiagnostic() const noexcept { return formattedDiagnostic; }
};

// 从原始源码与字节偏移计算 1-based 行号与 UTF-8 码点列号，并生成带指示箭头 `^` 的诊断文本
ParseError makeParseError(
    ErrorCategory category,
    std::string_view source,
    size_t byteOffset,
    uint32_t byteLength,
    std::string message,
    std::string_view sourcePath = ""
);

class Exception : public std::runtime_error
{
public:
    explicit Exception(const std::string& msg)
        : std::runtime_error(msg)
    { }
};

class SyntaxError : public Exception
{
public:
    explicit SyntaxError(ParseError err)
        : Exception(err.formattedDiagnostic.empty() ? err.message : err.formattedDiagnostic)
        , m_error(std::move(err))
    { }

    const ParseError& error() const noexcept { return m_error; }
    const SourceLocation& location() const noexcept { return m_error.location; }
    ErrorCategory category() const noexcept { return m_error.category; }

private:
    ParseError m_error;
};

class TypeError : public Exception
{
public:
    explicit TypeError(const std::string& msg)
        : Exception(msg)
    { }
};

class OutOfRangeError : public Exception
{
public:
    explicit OutOfRangeError(const std::string& msg)
        : Exception(msg)
    { }
};

NAMESPACE_END(tomlkit)
