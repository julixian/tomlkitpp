module;

#include "TKPMacros.hpp"
#include <time.h>
#ifdef _WIN32
#include <Windows.h>
#endif

module tomlkitpp;

import :Types;

NAMESPACE_BEGIN(tomlkit)

namespace
{
    constexpr bool isLeapYear(uint16_t year) noexcept
    {
        return (year % 4u == 0u && year % 100u != 0u) || (year % 400u == 0u);
    }

    constexpr uint8_t daysInMonth(uint16_t year, uint8_t month) noexcept
    {
        switch (month)
        {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31;
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        case 2:
            return isLeapYear(year) ? 29 : 28;
        default:
            return 0;
        }
    }

    // Howard Hinnant 公历日期与自 1970-01-01 起天数的双向换算
    constexpr int64_t daysFromCivil(int64_t y, uint32_t m, uint32_t d) noexcept
    {
        y -= (m <= 2) ? 1 : 0;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const uint32_t yoe = (uint32_t)(y - era * 400);
        const uint32_t doy = (153u * (m + (m > 2 ? (uint32_t)-3 : 9u)) + 2u) / 5u + d - 1u;
        const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
        return era * 146097 + (int64_t)doe - 719468;
    }

    constexpr std::tuple<int64_t, uint32_t, uint32_t> civilFromDays(int64_t z) noexcept
    {
        z += 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const uint32_t doe = (uint32_t)(z - era * 146097);
        const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t y = (int64_t)yoe + era * 400;
        const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const uint32_t mp = (5 * doy + 2) / 153;
        const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
        const uint32_t m = mp < 10 ? mp + 3 : mp - 9;
        return { y + (m <= 2 ? 1 : 0), m, d };
    }
}

std::string_view itemKindName(ItemKind kind) noexcept
{
    switch (kind)
    {
    case ItemKind::Empty:          return "empty";
    case ItemKind::Boolean:        return "boolean";
    case ItemKind::Integer:        return "integer";
    case ItemKind::Float:          return "float";
    case ItemKind::String:         return "string";
    case ItemKind::LocalDate:      return "local_date";
    case ItemKind::LocalTime:      return "local_time";
    case ItemKind::LocalDateTime:  return "local_date_time";
    case ItemKind::OffsetDateTime: return "offset_date_time";
    case ItemKind::Array:          return "array";
    case ItemKind::Table:          return "table";
    default:                       TKP_UNREACHABLE();
    }
}

bool LocalDate::isValid() const noexcept
{
    if (year > 9999 || month < 1 || month > 12 || day < 1) {
        return false;
    }
    return day <= daysInMonth(year, month);
}

std::string LocalDate::toString() const
{
    return std::format("{:04d}-{:02d}-{:02d}", year, month, day);
}

bool LocalTime::isValid() const noexcept
{
    if (hour > 23 || minute > 59 || second > 60 || nanosecond >= 1'000'000'000u) {
        return false;
    }
    if (!hasSeconds && (second != 0 || nanosecond != 0)) {
        return false;
    }
    return true;
}

std::string LocalTime::toString(bool forceSeconds) const
{
    if (!hasSeconds && !forceSeconds && second == 0 && nanosecond == 0) {
        return std::format("{:02d}:{:02d}", hour, minute);
    }

    std::string res = std::format("{:02d}:{:02d}:{:02d}", hour, minute, second);
    if (nanosecond != 0 || subsecondDigits > 0) {
        std::string frac = std::format("{:09d}", nanosecond);
        if (subsecondDigits > 0 && subsecondDigits <= 9) {
            // 若指定了原始小数位数但有效位更长，保留全部非零精度；否则按原始位数输出
            size_t minKeep = subsecondDigits;
            while (frac.size() > minKeep && frac.back() == '0') {
                frac.pop_back();
            }
            if (frac.size() > minKeep) {
                while (frac.size() > 1 && frac.back() == '0') {
                    frac.pop_back();
                }
            }
        }
        else {
            while (frac.size() > 1 && frac.back() == '0') {
                frac.pop_back();
            }
        }
        res.push_back('.');
        res.append(frac);
    }
    return res;
}

bool LocalDateTime::isValid() const noexcept
{
    const bool validDelim = (delimiter == 'T' || delimiter == 't' || delimiter == ' ');
    return validDelim && date.isValid() && time.isValid();
}

std::string LocalDateTime::toString(bool forceSeconds) const
{
    const char outDelim = forceSeconds ? 'T' : delimiter;
    return std::format("{}{}{}", date.toString(), outDelim, time.toString(forceSeconds));
}

bool OffsetDateTime::isValid() const noexcept
{
    const bool validDelim = (delimiter == 'T' || delimiter == 't' || delimiter == ' ');
    const bool validOffset = (offsetMinutes >= -1439 && offsetMinutes <= 1439);
    return validDelim && validOffset && date.isValid() && time.isValid();
}

std::string OffsetDateTime::toString(bool forceSeconds) const
{
    const char outDelim = forceSeconds ? 'T' : delimiter;
    std::string res = std::format("{}{}{}", date.toString(), outDelim, time.toString(forceSeconds));
    if (isUtcZ) {
        res.push_back(forceSeconds ? 'Z' : zChar);
    }
    else {
        const bool isNeg = (offsetMinutes < 0) || (offsetMinutes == 0 && negativeZeroOffset);
        const int absMin = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
        res += std::format("{}{:02d}:{:02d}", isNeg ? '-' : '+', absMin / 60, absMin % 60);
    }
    return res;
}

std::pair<int64_t, uint32_t> OffsetDateTime::toUtcEpoch() const noexcept
{
    const int64_t days = daysFromCivil(date.year, date.month, date.day);
    const int64_t localSecs = days * 86400
        + (int64_t)time.hour * 3600
        + (int64_t)time.minute * 60
        + (int64_t)time.second;
    const int64_t utcSecs = localSecs - (int64_t)(isUtcZ ? 0 : offsetMinutes) * 60;
    return { utcSecs, time.nanosecond };
}

int64_t OffsetDateTime::toEpochMillis() const noexcept
{
    const auto [utcSecs, nanos] = toUtcEpoch();
    return utcSecs * 1000 + (int64_t)(nanos / 1'000'000u);
}

OffsetDateTime OffsetDateTime::fromEpochMillisLocal(int64_t epochMs) noexcept
{
    int64_t utcSec = epochMs / 1000;
    int64_t remMs = epochMs % 1000;
    if (remMs < 0) {
        remMs += 1000;
        --utcSec;
    }

    const time_t rawTime = (time_t)utcSec;
    tm localTm{};
    bool ok = false;
    if ((int64_t)rawTime == utcSec) {
#ifdef _WIN32
        ok = (::localtime_s(&localTm, &rawTime) == 0);
#else
        ok = (::localtime_r(&rawTime, &localTm) != nullptr);
#endif
    }

    OffsetDateTime out{};
    out.delimiter = 'T';
    out.time.nanosecond = (uint32_t)remMs * 1'000'000u;
    out.time.subsecondDigits = 3;
    out.time.hasSeconds = true;

    if (ok && localTm.tm_year + 1900 >= 0 && localTm.tm_year + 1900 <= 9999) {
        const uint16_t year = (uint16_t)(localTm.tm_year + 1900);
        const uint8_t month = (uint8_t)(localTm.tm_mon + 1);
        const uint8_t day = (uint8_t)localTm.tm_mday;
        const uint8_t hour = (uint8_t)localTm.tm_hour;
        const uint8_t minute = (uint8_t)localTm.tm_min;
        const uint8_t second = (uint8_t)localTm.tm_sec;

        const int64_t localEpochSec = daysFromCivil(year, month, day) * 86400
            + (int64_t)hour * 3600
            + (int64_t)minute * 60
            + (int64_t)second;

        out.date = LocalDate{ .year = year, .month = month, .day = day };
        out.time.hour = hour;
        out.time.minute = minute;
        out.time.second = second;
        out.isUtcZ = false;
        out.offsetMinutes = (int16_t)((localEpochSec - utcSec) / 60);
        return out;
    }

    int64_t days = utcSec / 86400;
    int64_t secOfDay = utcSec % 86400;
    if (secOfDay < 0) {
        secOfDay += 86400;
        --days;
    }
    const auto [y, m, d] = civilFromDays(days);
    out.date = LocalDate{
        .year = (uint16_t)std::clamp<int64_t>(y, 0, 9999),
        .month = (uint8_t)m,
        .day = (uint8_t)d
    };
    out.time.hour = (uint8_t)(secOfDay / 3600);
    out.time.minute = (uint8_t)((secOfDay % 3600) / 60);
    out.time.second = (uint8_t)(secOfDay % 60);
    out.isUtcZ = false;
    out.offsetMinutes = 0;
    return out;
}

bool decodeUtf8Codepoint(std::string_view text, size_t& pos, char32_t& outCp) noexcept
{
    if (pos >= text.size()) {
        return false;
    }

    const uint8_t b0 = (uint8_t)text[pos];
    if (b0 < 0x80u) {
        outCp = (char32_t)b0;
        ++pos;
        return true;
    }

    // 2 字节序列：C2..DF 80..BF（拒绝 C0/C1 超长编码）
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        if (pos + 1 >= text.size()) {
            return false;
        }
        const uint8_t b1 = (uint8_t)text[pos + 1];
        if ((b1 & 0xC0u) != 0x80u) {
            return false;
        }
        outCp = ((char32_t)(b0 & 0x1Fu) << 6) | (char32_t)(b1 & 0x3Fu);
        pos += 2;
        return true;
    }

    // 3 字节序列：E0..EF，严格拒绝超长编码与 UTF-16 代理对 U+D800..U+DFFF
    if (b0 >= 0xE0u && b0 <= 0xEFu) {
        if (pos + 2 >= text.size()) {
            return false;
        }
        const uint8_t b1 = (uint8_t)text[pos + 1];
        const uint8_t b2 = (uint8_t)text[pos + 2];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
            return false;
        }
        if (b0 == 0xE0u && b1 < 0xA0u) {
            return false; // 超长编码 (< U+0800)
        }
        if (b0 == 0xEDu && b1 >= 0xA0u) {
            return false; // UTF-16 代理对 U+D800..U+DFFF
        }
        outCp = ((char32_t)(b0 & 0x0Fu) << 12)
            | ((char32_t)(b1 & 0x3Fu) << 6)
            | (char32_t)(b2 & 0x3Fu);
        pos += 3;
        return true;
    }

    // 4 字节序列：F0..F4，严格限制在 <= U+10FFFF
    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        if (pos + 3 >= text.size()) {
            return false;
        }
        const uint8_t b1 = (uint8_t)text[pos + 1];
        const uint8_t b2 = (uint8_t)text[pos + 2];
        const uint8_t b3 = (uint8_t)text[pos + 3];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) {
            return false;
        }
        if (b0 == 0xF0u && b1 < 0x90u) {
            return false; // 超长编码 (< U+10000)
        }
        if (b0 == 0xF4u && b1 > 0x8Fu) {
            return false; // 超出 U+10FFFF
        }
        outCp = ((char32_t)(b0 & 0x07u) << 18)
            | ((char32_t)(b1 & 0x3Fu) << 12)
            | ((char32_t)(b2 & 0x3Fu) << 6)
            | (char32_t)(b3 & 0x3Fu);
        pos += 4;
        return true;
    }

    return false;
}

bool isValidUtf8(std::string_view text, size_t* pErrorOffset) noexcept
{
    size_t pos = 0;
    char32_t cp = 0;
    while (pos < text.size()) {
        const size_t startPos = pos;
        if (!decodeUtf8Codepoint(text, pos, cp)) {
            if (pErrorOffset != nullptr) {
                *pErrorOffset = startPos;
            }
            return false;
        }
    }
    return true;
}

void appendUtf8Codepoint(std::string& out, char32_t cp)
{
    if (cp <= 0x7Fu) {
        out.push_back((char)cp);
    }
    else if (cp <= 0x7FFu) {
        out.push_back((char)(0xC0u | (cp >> 6)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    else if (cp <= 0xFFFFu) {
        out.push_back((char)(0xE0u | (cp >> 12)));
        out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    else if (cp <= 0x10FFFFu) {
        out.push_back((char)(0xF0u | (cp >> 18)));
        out.push_back((char)(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
}

std::string wideToUtf8(std::wstring_view wide)
{
    std::string out;
    out.reserve(wide.size() * 3);

    if constexpr (sizeof(wchar_t) == 2) {
        size_t i = 0;
        while (i < wide.size()) {
            const uint32_t u0 = (uint16_t)wide[i++];
            if (u0 >= 0xD800u && u0 <= 0xDBFFu) {
                if (i < wide.size()) {
                    const uint32_t u1 = (uint16_t)wide[i];
                    if (u1 >= 0xDC00u && u1 <= 0xDFFFu) {
                        ++i;
                        const char32_t cp = (char32_t)(0x10000u + ((u0 - 0xD800u) << 10) + (u1 - 0xDC00u));
                        appendUtf8Codepoint(out, cp);
                        continue;
                    }
                }
                appendUtf8Codepoint(out, 0xFFFDu);
            }
            else if (u0 >= 0xDC00u && u0 <= 0xDFFFu) {
                appendUtf8Codepoint(out, 0xFFFDu);
            }
            else {
                appendUtf8Codepoint(out, (char32_t)u0);
            }
        }
    }
    else {
        for (const wchar_t wc : wide) {
            const uint32_t cp = (uint32_t)wc;
            if (cp <= 0x10FFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
                appendUtf8Codepoint(out, (char32_t)cp);
            }
            else {
                appendUtf8Codepoint(out, 0xFFFDu);
            }
        }
    }

    return out;
}

std::wstring utf8ToWide(std::string_view utf8)
{
    std::wstring out;
    out.reserve(utf8.size());

    size_t pos = 0;
    char32_t cp = 0;
    while (pos < utf8.size()) {
        if (!decodeUtf8Codepoint(utf8, pos, cp)) {
            ++pos;
            cp = 0xFFFDu;
        }

        if constexpr (sizeof(wchar_t) == 2) {
            if (cp <= 0xFFFFu) {
                out.push_back((wchar_t)cp);
            }
            else {
                const uint32_t v = (uint32_t)cp - 0x10000u;
                out.push_back((wchar_t)(0xD800u + (v >> 10)));
                out.push_back((wchar_t)(0xDC00u + (v & 0x3FFu)));
            }
        }
        else {
            out.push_back((wchar_t)cp);
        }
    }

    return out;
}

std::string pathToUtf8(const std::filesystem::path& path)
{
#ifdef _WIN32
    return wideToUtf8(path.native());
#else
    return path.native();
#endif
}

std::filesystem::path utf8ToPath(std::string_view utf8)
{
#ifdef _WIN32
    return std::filesystem::path(utf8ToWide(utf8));
#else
    return std::filesystem::path(std::string(utf8));
#endif
}

std::string formatErrorCodeUtf8(const std::error_code& ec)
{
#ifdef _WIN32
    if (ec.category() == std::system_category()) {
        LPWSTR pBuffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            (DWORD)ec.value(),
            0,
            (LPWSTR)&pBuffer,
            0,
            nullptr
        );
        if (length != 0 && pBuffer != nullptr) {
            std::wstring_view wideMsg(pBuffer, length);
            while (!wideMsg.empty() && (wideMsg.back() == L'\r' || wideMsg.back() == L'\n' || wideMsg.back() == L' ')) {
                wideMsg.remove_suffix(1);
            }
            std::string utf8Msg = wideToUtf8(wideMsg);
            LocalFree(pBuffer);
            return utf8Msg;
        }
        if (pBuffer != nullptr) {
            LocalFree(pBuffer);
        }
    }
#endif

    const std::string fallback = ec.message();
    if (isValidUtf8(fallback)) {
        return fallback;
    }
    return std::format("system error {}", ec.value());
}

bool isBareKeyChar(char32_t cp, [[maybe_unused]] SpecVersion spec) noexcept
{
    // TOML v1.0 与 v1.1 正式规范均规定裸键字符为 A-Z / a-z / 0-9 / - / _
    return (cp >= U'A' && cp <= U'Z')
        || (cp >= U'a' && cp <= U'z')
        || (cp >= U'0' && cp <= U'9')
        || cp == U'-'
        || cp == U'_';
}

bool isValidBareKey(std::string_view key, SpecVersion spec) noexcept
{
    if (key.empty()) {
        return false;
    }
    for (const char ch : key) {
        if (!isBareKeyChar((char32_t)(uint8_t)ch, spec)) {
            return false;
        }
    }
    return true;
}

NAMESPACE_END(tomlkit)
