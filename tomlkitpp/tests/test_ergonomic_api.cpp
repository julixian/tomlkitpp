import std;
import tomlkitpp;

namespace
{
    int g_failures = 0;

    void check(bool cond, std::string_view expr, int line)
    {
        if (!cond) {
            std::println(std::cerr, "[FAIL] line {}: {}", line, expr);
            ++g_failures;
        }
    }

    struct EndpointConfig
    {
        std::string url;
        int timeoutSec = 30;
        bool stream = true;

        friend bool operator==(const EndpointConfig&, const EndpointConfig&) = default;
    };
}

template<>
struct tomlkit::Serializer<EndpointConfig>
{
    static void toToml(const EndpointConfig& cfg, tomlkit::Item& out)
    {
        out = tomlkit::Item::inlineTable({
            { "url", cfg.url },
            { "timeoutSec", cfg.timeoutSec },
            { "stream", cfg.stream }
        });
    }

    static EndpointConfig fromToml(const tomlkit::Item& item)
    {
        return EndpointConfig{
            .url = item.at("url").get<std::string>(),
            .timeoutSec = item.valueOr("timeoutSec", 30),
            .stream = item.valueOr("stream", true)
        };
    }
};

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void testAutoVivificationAndMultidimensionalSubscript()
{
    using namespace tomlkit;

    Item doc = Item::document();
    doc["workspace", "package", "version"] = "1.0.0";
    doc["workspace", "profile", "release", "opt"] = 3;
    doc["workspace", "profile", "release", "lto"] = true;
    doc["workspace", "members"] = { "core", "cli", "gui" };

    // `{ "原名", 42 }` 保持为 2 元素数组而非误判为 Table
    doc["nameTable", "アリス"] = { "爱丽丝", 42 };

    const Item& cdoc = doc;
    CHECK(cdoc["workspace", "profile", "release", "opt"].get<int>() == 3);
    CHECK(cdoc["workspace", "members", 1].get<std::string_view>() == "cli");

    // 混合字符串键与整数下标的 `findOr`（贴合 GalTranslPP 实际调用形式）
    const std::string transName = findOr(cdoc, "nameTable", "アリス", 0, "");
    const int count = findOr(cdoc, "nameTable", "アリス", 1, 0);
    const std::string missing = findOr(cdoc, "nameTable", "不存在的角色", 0, "默认译名");
    CHECK(transName == "爱丽丝");
    CHECK(count == 42);
    CHECK(missing == "默认译名");

    // 自引用赋值稳定性测试（验证底层扩容不会造成悬垂引用）
    for (int i = 0; i < 40; ++i) {
        doc[std::format("k{}", i)] = i;
    }
    doc["copiedFromExisting"] = doc["k0"];
    CHECK(doc["copiedFromExisting"].get<int>() == 0);
    CHECK(doc["k39"].get<int>() == 39);

    // 往返解析语义一致
    const std::string text = doc.toToml();
    const Item reparsed = parse(text);
    CHECK(reparsed == doc);
}

void testStlContainerConversionsAndCustomSerializer()
{
    using namespace tomlkit;

    Item doc = Item::document();
    doc["ports"] = { 80, 443, 8080 };
    doc["labels"] = Item::table({
        { "env", "prod" },
        { "region", "ap-east" }
    });

    const EndpointConfig ep{ .url = "https://api.example.com/v1", .timeoutSec = 60, .stream = false };
    doc["endpoint"] = ep;

    std::vector<int> ports;
    doc["ports"].getTo(ports);
    CHECK((ports == std::vector<int>{ 80, 443, 8080 }));

    const auto labels = doc["labels"].get<std::map<std::string, std::string>>();
    CHECK(labels.at("env") == "prod" && labels.at("region") == "ap-east");

    const EndpointConfig decodedEp = doc["endpoint"].get<EndpointConfig>();
    CHECK(decodedEp == ep);

    const Item roundtripped = parse(doc.toToml());
    CHECK(roundtripped["endpoint"].get<EndpointConfig>() == ep);
}

void testDiagnosticFormatting()
{
    using namespace tomlkit;

    constexpr std::string_view kBadToml = "[server]\nhost = \"127.0.0.1\"\nport = 0xGG\n";
    const auto res = tryParse(kBadToml, {}, "config.toml");
    CHECK(!res.has_value());
    CHECK(res.error().location.line == 3);
    CHECK(res.error().format().contains("config.toml:3:"));
    CHECK(res.error().format().contains("^"));
}

void testEmptyItemSerializationErrorAndBoundsChecking()
{
    using namespace tomlkit;

    // 1. 未赋值的 Empty 节点在各层级序列化时必须抛 TypeError 并指明键路径/下标
    {
        Item doc = Item::document();
        (void)doc["server", "port"]; // 产生未赋值的 Empty 节点
        bool threw = false;
        try {
            (void)doc.toToml();
        }
        catch (const TypeError& ex) {
            threw = true;
            CHECK(std::string_view(ex.what()).contains("server.port"));
        }
        CHECK(threw);

        const auto saveRes = trySaveFileAtomic("should_not_be_created.toml", doc);
        CHECK(!saveRes.has_value());
        CHECK(saveRes.error().category == ErrorCategory::TypeMismatch);
    }

    {
        Item arr = Item::array();
        arr.push_back(42);
        arr.push_back(Item{});
        bool threw = false;
        try {
            (void)arr.toToml();
        }
        catch (const TypeError& ex) {
            threw = true;
            CHECK(std::string_view(ex.what()).contains("index 1"));
        }
        CHECK(threw);
    }

    // 2. 数组 operator[]（可变与 const、正越界与负索引）均严格检查越界并抛 OutOfRangeError
    Item doc = parse("nums = [10, 20, 30]\n");
    const Item& cdoc = doc;

    CHECK(doc["nums"][0].asInteger() == 10);
    CHECK(cdoc["nums"][2].asInteger() == 30);

    bool mutableUpperOob = false;
    try {
        (void)doc["nums"][3];
    }
    catch (const OutOfRangeError&) {
        mutableUpperOob = true;
    }
    CHECK(mutableUpperOob);

    bool constNegOob = false;
    try {
        (void)cdoc["nums"][-1];
    }
    catch (const OutOfRangeError& ex) {
        constNegOob = true;
        CHECK(std::string_view(ex.what()).contains("-1"));
    }
    CHECK(constNegOob);

    bool multiDimOob = false;
    try {
        (void)doc["nums", 5];
    }
    catch (const OutOfRangeError&) {
        multiDimOob = true;
    }
    CHECK(multiDimOob);

    // 3. 验证 find / findOptional / tryFind / findArrayOrEmpty / setMultilineArray
    CHECK(tomlkit::find<int>(doc, "nums", 1) == 20);
    CHECK(!tomlkit::findOptional<std::vector<std::string>>(doc, "missing", "arr").has_value());

    bool optTypeMismatchThrew = false;
    try {
        // 路径存在（nums 是整数数组），但请求转换为 vector<string>：findOptional 必须抛 TypeError
        (void)tomlkit::findOptional<std::vector<std::string>>(doc, "nums");
    }
    catch (const TypeError&) {
        optTypeMismatchThrew = true;
    }
    CHECK(optTypeMismatchThrew);

    // tryFind 在路径存在但类型不符时也必须返回 nullopt（noexcept）
    static_assert(noexcept(tomlkit::tryFind<std::vector<std::string>>(doc, "nums")));
    CHECK(!tomlkit::tryFind<std::vector<std::string>>(doc, "nums").has_value());
    CHECK(tomlkit::tryFind<std::vector<int>>(doc, "nums")->size() == 3);
    CHECK(tomlkit::findArrayOrEmpty(doc, "missing").isArray());
    CHECK(tomlkit::findArrayOrEmpty(doc, "missing").empty());

    Item dictArr = Item::array();
    dictArr.push_back(Item::inlineTable({ { "org", "a" }, { "rep", "b" } }));
    dictArr.push_back(Item::inlineTable({ { "org", "c" }, { "rep", "d" } }));
    dictArr.setMultilineArray(true);
    Item dictDoc = Item::document();
    dictDoc["gptDict"] = std::move(dictArr);
    CHECK(dictDoc.toToml().contains("\n  { org = \"a\", rep = \"b\" },\n  { org = \"c\", rep = \"d\" },\n]"));
}

int main()
{
    testAutoVivificationAndMultidimensionalSubscript();
    testStlContainerConversionsAndCustomSerializer();
    testDiagnosticFormatting();
    testEmptyItemSerializationErrorAndBoundsChecking();

    if (g_failures != 0) {
        std::println(std::cerr, "test_ergonomic_api failed with {} error(s)", g_failures);
        return 1;
    }
    std::println("test_ergonomic_api: all checks passed!");
    return 0;
}
