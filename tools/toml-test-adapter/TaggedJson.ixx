module;

#include "TKPMacros.hpp"

export module TaggedJson;

export import std;
export import tomlkitpp;

export NAMESPACE_BEGIN(tomlkit::test_adapter)

struct JsonNode
{
    enum class Kind : std::uint8_t
    {
        String,
        Array,
        Object
    };

    Kind kind = Kind::String;
    std::string strVal;
    std::vector<JsonNode> arrVal;
    std::vector<std::pair<std::string, JsonNode>> objVal;

    const JsonNode* findProp(std::string_view key) const noexcept
    {
        for (const auto& [k, v] : objVal) {
            if (k == key) {
                return &v;
            }
        }
        return nullptr;
    }
};

void appendJsonEscaped(std::string& out, std::string_view s)
{
    out.push_back('"');
    for (const char ch : s) {
        const auto b = (std::uint8_t)ch;
        switch (ch)
        {
        case '"':  out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\b': out.append("\\b"); break;
        case '\f': out.append("\\f"); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        default:
            if (b < 0x20u) {
                out += std::format("\\u{:04x}", (std::uint32_t)b);
            }
            else {
                out.push_back(ch);
            }
            break;
        }
    }
    out.push_back('"');
}

// 将 `tomlkit::Item` 编码为官方 `toml-test` 标记 JSON 字符串
void emitTaggedJson(std::string& out, const Item& item)
{
    const auto emitTagObjFunc = [&](std::string_view tag, std::string_view val)
        {
            out.append("{\"type\":");
            appendJsonEscaped(out, tag);
            out.append(",\"value\":");
            appendJsonEscaped(out, val);
            out.push_back('}');
        };

    switch (item.kind())
    {
    case ItemKind::Empty:
        emitTagObjFunc("string", "");
        return;
    case ItemKind::Boolean:
        emitTagObjFunc("bool", item.asBoolean() ? "true" : "false");
        return;
    case ItemKind::Integer:
        emitTagObjFunc("integer", std::format("{}", item.asInteger()));
        return;
    case ItemKind::Float:
    {
        const double v = item.asFloat();
        if (std::isnan(v)) {
            emitTagObjFunc("float", "nan");
        }
        else if (std::isinf(v)) {
            emitTagObjFunc("float", v < 0.0 ? "-inf" : "inf");
        }
        else {
            char buf[64]{};
            const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
            std::string sv(buf, (std::size_t)(ptr - buf));
            if (!sv.contains('.') && !sv.contains('e') && !sv.contains('E')) {
                sv.append(".0");
            }
            emitTagObjFunc("float", sv);
        }
        return;
    }
    case ItemKind::String:
        emitTagObjFunc("string", item.asString());
        return;
    case ItemKind::LocalDate:
        emitTagObjFunc("date-local", item.asLocalDate().toString());
        return;
    case ItemKind::LocalTime:
        emitTagObjFunc("time-local", item.asLocalTime().toString(true));
        return;
    case ItemKind::LocalDateTime:
        emitTagObjFunc("datetime-local", item.asLocalDateTime().toString(true));
        return;
    case ItemKind::OffsetDateTime:
        emitTagObjFunc("datetime", item.asOffsetDateTime().toString(true));
        return;
    case ItemKind::Array:
    {
        out.push_back('[');
        bool first = true;
        for (const Item& elem : item) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            emitTaggedJson(out, elem);
        }
        out.push_back(']');
        return;
    }
    case ItemKind::Table:
    {
        out.push_back('{');
        bool first = true;
        for (const auto [k, v] : item.tableItems()) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            appendJsonEscaped(out, k.name());
            out.push_back(':');
            emitTaggedJson(out, v);
        }
        out.push_back('}');
        return;
    }
    }
}

std::string itemToTaggedJson(const Item& doc)
{
    std::string out;
    emitTaggedJson(out, doc);
    return out;
}

// 轻量自包含 JSON 解析器（专门解析 `toml-test` 的 UTF-8 JSON 用例）
class MiniJsonParser
{
public:
    explicit MiniJsonParser(std::string_view src) noexcept
        : m_src(src)
    { }

    JsonNode parseRoot()
    {
        skipWs();
        JsonNode root = parseValue();
        skipWs();
        if (m_pos != m_src.size()) {
            throw std::runtime_error("Trailing data in JSON");
        }
        return root;
    }

private:
    void skipWs() noexcept
    {
        while (m_pos < m_src.size()) {
            const char c = m_src[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            }
            else {
                break;
            }
        }
    }

    char peek() const
    {
        if (m_pos >= m_src.size()) {
            throw std::runtime_error("Unexpected EOF in JSON");
        }
        return m_src[m_pos];
    }

    static std::uint32_t hexDigit(char c)
    {
        if (c >= '0' && c <= '9') {
            return (std::uint32_t)(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return (std::uint32_t)(c - 'a' + 10);
        }
        if (c >= 'A' && c <= 'F') {
            return (std::uint32_t)(c - 'A' + 10);
        }
        throw std::runtime_error("Invalid hex in JSON escape");
    }

    std::uint16_t parseU16Hex()
    {
        if (m_pos + 4 > m_src.size()) {
            throw std::runtime_error("Truncated \\u escape in JSON");
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v = (v << 4) | hexDigit(m_src[m_pos++]);
        }
        return (std::uint16_t)v;
    }

    std::string parseString()
    {
        if (peek() != '"') {
            throw std::runtime_error("Expected '\"' in JSON");
        }
        ++m_pos;
        std::string out;
        while (m_pos < m_src.size()) {
            const char c = m_src[m_pos++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (m_pos >= m_src.size()) {
                    throw std::runtime_error("Truncated escape in JSON");
                }
                const char esc = m_src[m_pos++];
                switch (esc)
                {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                {
                    const std::uint16_t u1 = parseU16Hex();
                    if (u1 >= 0xD800u && u1 <= 0xDBFFu) {
                        if (m_pos + 2 > m_src.size() || m_src[m_pos] != '\\' || m_src[m_pos + 1] != 'u') {
                            throw std::runtime_error("Unpaired high surrogate in JSON");
                        }
                        m_pos += 2;
                        const std::uint16_t u2 = parseU16Hex();
                        if (u2 < 0xDC00u || u2 > 0xDFFFu) {
                            throw std::runtime_error("Invalid low surrogate in JSON");
                        }
                        const char32_t cp = 0x10000u + (((char32_t)(u1 - 0xD800u) << 10) | (char32_t)(u2 - 0xDC00u));
                        appendUtf8Codepoint(out, cp);
                    }
                    else {
                        appendUtf8Codepoint(out, (char32_t)u1);
                    }
                    break;
                }
                default:
                    throw std::runtime_error("Unknown escape in JSON");
                }
            }
            else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("Unterminated string in JSON");
    }

    JsonNode parseValue()
    {
        skipWs();
        const char c = peek();
        if (c == '"') {
            return JsonNode{ .kind = JsonNode::Kind::String, .strVal = parseString() };
        }
        if (c == '[') {
            ++m_pos;
            JsonNode node{ .kind = JsonNode::Kind::Array };
            skipWs();
            if (peek() == ']') {
                ++m_pos;
                return node;
            }
            while (true) {
                node.arrVal.push_back(parseValue());
                skipWs();
                if (peek() == ',') {
                    ++m_pos;
                    continue;
                }
                if (peek() == ']') {
                    ++m_pos;
                    break;
                }
                throw std::runtime_error("Expected ',' or ']' in JSON array");
            }
            return node;
        }
        if (c == '{') {
            ++m_pos;
            JsonNode node{ .kind = JsonNode::Kind::Object };
            skipWs();
            if (peek() == '}') {
                ++m_pos;
                return node;
            }
            while (true) {
                skipWs();
                std::string k = parseString();
                skipWs();
                if (peek() != ':') {
                    throw std::runtime_error("Expected ':' in JSON object");
                }
                ++m_pos;
                JsonNode v = parseValue();
                node.objVal.emplace_back(std::move(k), std::move(v));
                skipWs();
                if (peek() == ',') {
                    ++m_pos;
                    continue;
                }
                if (peek() == '}') {
                    ++m_pos;
                    break;
                }
                throw std::runtime_error("Expected ',' or '}' in JSON object");
            }
            return node;
        }
        throw std::runtime_error("Unexpected token in toml-test JSON");
    }

    std::string_view m_src;
    std::size_t m_pos = 0;
};

// 将官方标记 JSON 节点还原为 `tomlkit::Item`
Item taggedJsonNodeToItem(const JsonNode& node, bool isRoot = false)
{
    if (node.kind == JsonNode::Kind::Array) {
        // 若非空且全部元素均为未标记表对象，构造为 ArrayOfTables 或内联表数组均可；若含内嵌子表用 ArrayOfTables 更清晰
        bool allTables = !node.arrVal.empty();
        for (const JsonNode& el : node.arrVal) {
            if (el.kind != JsonNode::Kind::Object) {
                allTables = false;
                break;
            }
            if (el.objVal.size() == 2) {
                const JsonNode* pType = el.findProp("type");
                const JsonNode* pVal = el.findProp("value");
                if (pType && pVal && pType->kind == JsonNode::Kind::String && pVal->kind == JsonNode::Kind::String) {
                    allTables = false;
                    break;
                }
            }
        }

        if (allTables) {
            Item aot = Item::arrayOfTables();
            for (const JsonNode& el : node.arrVal) {
                Item elemTbl = taggedJsonNodeToItem(el, false);
                elemTbl.setTableStyle(TableStyle::ArrayOfTablesElement);
                aot.push_back(std::move(elemTbl));
            }
            return aot;
        }

        Item arr = Item::array();
        for (const JsonNode& el : node.arrVal) {
            Item child = taggedJsonNodeToItem(el, false);
            if (child.isTable()) {
                child.setTableStyle(TableStyle::Inline);
            }
            arr.push_back(std::move(child));
        }
        return arr;
    }

    if (node.kind == JsonNode::Kind::Object) {
        if (node.objVal.size() == 2) {
            const JsonNode* pType = node.findProp("type");
            const JsonNode* pVal = node.findProp("value");
            if (pType && pVal && pType->kind == JsonNode::Kind::String && pVal->kind == JsonNode::Kind::String) {
                const std::string_view tag = pType->strVal;
                const std::string_view raw = pVal->strVal;

                if (tag == "string") {
                    return Item(raw);
                }
                if (tag == "bool") {
                    return Item(raw == "true");
                }
                if (tag == "integer") {
                    std::int64_t iv = 0;
                    const auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), iv);
                    if (ec != std::errc{} || ptr != raw.data() + raw.size()) {
                        throw std::runtime_error(std::format("Invalid integer in tagged JSON: {}", raw));
                    }
                    return Item(iv);
                }
                if (tag == "float") {
                    if (raw == "inf" || raw == "+inf") {
                        return Item(std::numeric_limits<double>::infinity());
                    }
                    if (raw == "-inf") {
                        return Item(-std::numeric_limits<double>::infinity());
                    }
                    if (raw == "nan" || raw == "+nan") {
                        return Item(std::numeric_limits<double>::quiet_NaN());
                    }
                    if (raw == "-nan") {
                        return Item(-std::numeric_limits<double>::quiet_NaN());
                    }
                    std::string_view clean = raw;
                    if (clean.starts_with('+')) {
                        clean.remove_prefix(1);
                    }
                    double dv = 0.0;
                    std::from_chars(clean.data(), clean.data() + clean.size(), dv);
                    return Item(dv);
                }
                if (tag == "datetime" || tag == "datetime-local" || tag == "date-local" || tag == "time-local") {
                    detail::Lexer lex(raw);
                    const auto scRes = lex.lexScalar();
                    if (!scRes) {
                        throw std::runtime_error(std::format("Invalid datetime in tagged JSON: {}", raw));
                    }
                    return std::visit([](auto&& x) -> Item { return Item(std::forward<decltype(x)>(x)); }, scRes->payload);
                }
                throw std::runtime_error(std::format("Unknown tag '{}' in tagged JSON", tag));
            }
        }

        Item tbl = isRoot ? Item::document() : Item::table(TableStyle::Standard);
        for (const auto& [k, v] : node.objVal) {
            tbl.insertOrAssign(k, taggedJsonNodeToItem(v, false));
        }
        return tbl;
    }

    throw std::runtime_error("Unexpected bare string node in tagged JSON");
}

Item taggedJsonToItem(std::string_view jsonText)
{
    MiniJsonParser parser(jsonText);
    const JsonNode root = parser.parseRoot();
    return taggedJsonNodeToItem(root, true);
}

// 近似浮点容差 + 完整 TOML 语义等价比较（与官方 `toml-test` 判定准则对齐）
bool itemsMatchForTomlTest(const Item& a, const Item& b) noexcept
{
    if (a.kind() != b.kind()) {
        return false;
    }
    if (a.isFloat()) {
        const double x = a.asFloat();
        const double y = b.asFloat();
        if (std::isnan(x) && std::isnan(y)) {
            return true;
        }
        if (std::isinf(x) || std::isinf(y)) {
            return x == y;
        }
        if (x == y) {
            return true;
        }
        const double diff = std::abs(x - y);
        const double scale = std::max(1.0, std::max(std::abs(x), std::abs(y)));
        return diff <= 1e-12 * scale;
    }
    if (a.isArray()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!itemsMatchForTomlTest(a[i], b[i])) {
                return false;
            }
        }
        return true;
    }
    if (a.isTable()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (const auto [k, v] : a.tableItems()) {
            const Item* pOther = b.find(k.name());
            if (pOther == nullptr || !itemsMatchForTomlTest(v, *pOther)) {
                return false;
            }
        }
        return true;
    }
    return a == b;
}

NAMESPACE_END(tomlkit::test_adapter)
