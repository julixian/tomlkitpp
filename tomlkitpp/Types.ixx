module;

#include "TKPMacros.hpp"

export module tomlkitpp:Types;

export import std;
export import :Spec;

export NAMESPACE_BEGIN(tomlkit)

enum class ItemKind : uint8_t
{
    Empty,
    Boolean,
    Integer,
    Float,
    String,
    LocalDate,
    LocalTime,
    LocalDateTime,
    OffsetDateTime,
    Array,
    Table
};

std::string_view itemKindName(ItemKind kind) noexcept;

enum class StringStyle : uint8_t
{
    Basic,
    Literal,
    MultilineBasic,
    MultilineLiteral
};

enum class IntegerRadix : uint8_t
{
    Decimal,
    Hexadecimal,
    Octal,
    Binary
};

enum class TableStyle : uint8_t
{
    DocumentRoot,
    Standard,
    ImplicitSuper,
    Dotted,
    Inline,
    ArrayOfTablesElement
};

enum class ArrayStyle : uint8_t
{
    Standard,
    ArrayOfTables
};

enum class KeyStyle : uint8_t
{
    Bare,
    BasicQuoted,
    LiteralQuoted
};

struct LocalDate
{
    uint16_t year = 1970;
    uint8_t month = 1;
    uint8_t day = 1;

    bool isValid() const noexcept;
    std::string toString() const;

    friend bool operator==(const LocalDate&, const LocalDate&) = default;
    friend auto operator<=>(const LocalDate&, const LocalDate&) = default;
};

struct LocalTime
{
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint32_t nanosecond = 0;
    uint8_t subsecondDigits = 0;
    bool hasSeconds = true;

    bool isValid() const noexcept;
    std::string toString(bool forceSeconds = false) const;

    friend bool operator==(const LocalTime& lhs, const LocalTime& rhs) noexcept
    {
        return lhs.hour == rhs.hour
            && lhs.minute == rhs.minute
            && lhs.second == rhs.second
            && lhs.nanosecond == rhs.nanosecond;
    }

    friend std::strong_ordering operator<=>(const LocalTime& lhs, const LocalTime& rhs) noexcept
    {
        return std::tie(lhs.hour, lhs.minute, lhs.second, lhs.nanosecond)
            <=> std::tie(rhs.hour, rhs.minute, rhs.second, rhs.nanosecond);
    }
};

struct LocalDateTime
{
    LocalDate date{};
    LocalTime time{};
    char delimiter = 'T';

    bool isValid() const noexcept;
    std::string toString(bool forceSeconds = false) const;

    friend bool operator==(const LocalDateTime& lhs, const LocalDateTime& rhs) noexcept
    {
        return lhs.date == rhs.date && lhs.time == rhs.time;
    }

    friend std::strong_ordering operator<=>(const LocalDateTime& lhs, const LocalDateTime& rhs) noexcept
    {
        return std::tie(lhs.date, lhs.time) <=> std::tie(rhs.date, rhs.time);
    }
};

struct OffsetDateTime
{
    LocalDate date{};
    LocalTime time{};
    int16_t offsetMinutes = 0;
    bool isUtcZ = true;
    bool negativeZeroOffset = false;
    char delimiter = 'T';
    char zChar = 'Z';

    bool isValid() const noexcept;
    std::string toString(bool forceSeconds = false) const;

    // 换算为相对 1970-01-01T00:00:00Z 的 UTC 秒与纳秒，用于精确时间点比较
    std::pair<int64_t, uint32_t> toUtcEpoch() const noexcept;

    friend bool operator==(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept
    {
        return lhs.toUtcEpoch() == rhs.toUtcEpoch();
    }

    friend std::strong_ordering operator<=>(const OffsetDateTime& lhs, const OffsetDateTime& rhs) noexcept
    {
        return lhs.toUtcEpoch() <=> rhs.toUtcEpoch();
    }
};

// UTF-8 / 宽字符 / std::filesystem::path 编解码与键字符集校验工具（严禁使用受 Windows ANSI 代码页污染的 path::string()）
bool decodeUtf8Codepoint(std::string_view text, size_t& pos, char32_t& outCp) noexcept;
bool isValidUtf8(std::string_view text, size_t* pErrorOffset = nullptr) noexcept;
void appendUtf8Codepoint(std::string& out, char32_t cp);

std::string wideToUtf8(std::wstring_view wide);
std::wstring utf8ToWide(std::string_view utf8);
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path utf8ToPath(std::string_view utf8);
std::string formatErrorCodeUtf8(const std::error_code& ec);

bool isBareKeyChar(char32_t cp, SpecVersion spec = SpecVersion::v1_1) noexcept;
bool isValidBareKey(std::string_view key, SpecVersion spec = SpecVersion::v1_1) noexcept;

NAMESPACE_END(tomlkit)
