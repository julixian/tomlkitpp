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
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void testMultilineInlineTablesAndTrailingComma()
{
    using namespace tomlkit;

    constexpr std::string_view kSrc = R"(# TOML v1.1 多行内联表测试
endpoint = {
    # 主机配置
    host = "127.0.0.1", # 本地回环
    port = 8080,
    nested = {
        a.b = 1,
        a.c = [
            10,
            20, # 数组尾随逗号
        ], # 内嵌内联表尾随逗号
    }, # 顶层内联表尾随逗号
}
)";

    const Item doc = parse(kSrc);
    CHECK(doc["endpoint", "host"].asString() == "127.0.0.1");
    CHECK(doc["endpoint", "port"].asInteger() == 8080);
    CHECK(doc["endpoint", "nested", "a", "b"].asInteger() == 1);
    CHECK(doc["endpoint", "nested", "a", "c", 1].asInteger() == 20);

    // 字节级 100% 无损往返
    CHECK(doc.toToml() == kSrc);

    // 在 v1.0 模式下解析应触发 SpecRestriction
    const auto v10Res = tryParse(kSrc, ParseOptions{ .spec = SpecVersion::v1_0 });
    CHECK(!v10Res.has_value() && v10Res.error().category == ErrorCategory::SpecRestriction);

    // 单行内联表尾随逗号：v1.1 允许，v1.0 拒绝
    CHECK(tryParse("x = { a = 1, }").has_value());
    const auto v10Trailing = tryParse("x = { a = 1, }", ParseOptions{ .spec = SpecVersion::v1_0 });
    CHECK(!v10Trailing.has_value() && v10Trailing.error().category == ErrorCategory::SpecRestriction);
}

void testV1_1StringsAndOptionalSeconds()
{
    using namespace tomlkit;

    constexpr std::string_view kSrc = R"(ansi = "\e[1;32mOK\e[0m"
latin = "\x41\xE9\x00"
alarm = 07:30
meeting = 2026-11-14 09:45
utc = 2026-11-14T09:45Z
offset = 2026-11-14 09:45+08:00
)";

    const Item doc = parse(kSrc);
    CHECK(doc["ansi"].asString() == "\x1B[1;32mOK\x1B[0m");
    CHECK(doc["alarm"].asLocalTime() == (LocalTime{ 7, 30, 0 }));
    CHECK(!doc["alarm"].asLocalTime().hasSeconds);
    CHECK(doc["meeting"].asLocalDateTime().time == (LocalTime{ 9, 45, 0 }));
    CHECK(doc["utc"].asOffsetDateTime().time == (LocalTime{ 9, 45, 0 }));
    CHECK(doc["offset"].asOffsetDateTime().offsetMinutes == 480);

    // 默认 v1.1 无损原样输出
    CHECK(doc.toToml() == kSrc);

    // 降级导出为兼容 v1.0 的 TOML 文本，并验证可被严格 v1.0 解析器正常读取
    const std::string downgraded = doc.toToml(FormatOptions{ .spec = SpecVersion::v1_0 });
    const auto parsedByV10 = tryParse(downgraded, ParseOptions{ .spec = SpecVersion::v1_0 });
    CHECK(parsedByV10.has_value());
    CHECK(*parsedByV10 == doc);
}

void testTableAndDottedKeyConflictRules()
{
    using namespace tomlkit;

    // 合法：`[fruit]` 内点分键定义 `apple`，后续再跟子表数组 `[[fruit.apple.seeds]]`
    {
        constexpr std::string_view kValid = R"([fruit]
apple.color = "red"

[[fruit.apple.seeds]]
size = 2
)";
        const auto res = tryParse(kValid);
        CHECK(res.has_value());
        CHECK((*res)["fruit", "apple", "color"].asString() == "red");
        CHECK((*res)["fruit", "apple", "seeds", 0, "size"].asInteger() == 2);
        CHECK(res->toToml() == kValid);
    }

    // 合法：先定义子表 `[x.y.z]`，后显式定义父表 `[x]`
    {
        constexpr std::string_view kValidSuper = R"([x.y.z]
a = 1

[x]
b = 2
)";
        const auto res = tryParse(kValidSuper);
        CHECK(res.has_value());
        CHECK((*res)["x", "y", "z", "a"].asInteger() == 1);
        CHECK((*res)["x", "b"].asInteger() == 2);
        CHECK(res->toToml() == kValidSuper);
    }

    // 合法（Fuzzer 回归用例）：先通过深层表 `[a.b.c.f]` 产生隐式中间父表 `a.b` / `a.b.c`，
    // 后打开父表 `[a]` 并用点分键穿透 `b.c` 写入新分支 `b.c.d.k.t`，必须满足字节级无损与语义一致
    {
        constexpr std::string_view kImplicitSuperThenDotted =
            "# header comment\n"
            "\n"
            "[a.b.c.f]\n"
            "  z = 9\n"
            "\n"
            "[a]\n"
            "  first = 1\n"
            "  b.c.d.k.t = \"nested via implicit super\"\n"
            "  last = 2\n";
        const auto res = tryParse(kImplicitSuperThenDotted);
        CHECK(res.has_value());
        CHECK((*res)["a", "b", "c", "f", "z"].asInteger() == 9);
        CHECK((*res)["a", "b", "c", "d", "k", "t"].asString() == "nested via implicit super");
        CHECK(res->toToml() == kImplicitSuperThenDotted);
        const auto reparsed = tryParse(res->toToml());
        CHECK(reparsed.has_value() && *reparsed == *res);
    }

    // 非法：点分键定义表后试图再用 `[t1.t2]` 重新打开
    CHECK(!tryParse("[t1]\nt2.t3.v = 0\n[t1.t2]\n").has_value());

    // 非法：试图在后续块中用点分键向已显式定义的 `[a.b.c]` 注入键 (#859)
    CHECK(!tryParse("[a.b.c]\nz = 9\n[a]\nb.c.t = 1\n").has_value());

    // 非法：向已闭合内联表追加键或打开子表
    CHECK(!tryParse("a = { b = 1 }\na.c = 2\n").has_value());
    CHECK(!tryParse("a = { b = 1 }\n[a.c]\nx = 1\n").has_value());
    CHECK(!tryParse("a = { k1 = 1, k1.name = 'joe' }\n").has_value());

    // 非法：静态数组与 `[[aot]]` 同名冲突
    CHECK(!tryParse("arr = [{ x = 1 }]\n[[arr]]\nx = 2\n").has_value());

    // 非法：重复定义父表 `[a]` 两次
    CHECK(!tryParse("[a.b]\n[a]\n[a]\n").has_value());
}

int main()
{
    testMultilineInlineTablesAndTrailingComma();
    testV1_1StringsAndOptionalSeconds();
    testTableAndDottedKeyConflictRules();

    if (g_failures != 0) {
        std::println(std::cerr, "test_v1_1_features failed with {} error(s)", g_failures);
        return 1;
    }
    std::println("test_v1_1_features: all checks passed!");
    return 0;
}
