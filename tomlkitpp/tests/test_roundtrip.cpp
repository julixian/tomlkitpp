import std;
import tomlkitpp;

namespace fs = std::filesystem;

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

#define CHECK(expr) check((expr), #expr, __LINE__)

void verifyLosslessString(std::string_view label, std::string_view input)
{
    const auto res = tomlkit::tryParse(input, {}, label);
    if (!res) {
        std::println(std::cerr, "[FAIL] {} failed to parse:\n{}", label, res.error().format());
        ++g_failures;
        return;
    }

    const std::string output = res->toToml();
    if (output != input) {
        std::println(std::cerr, "[FAIL] {} round-trip mismatch!\n--- EXPECTED ({} bytes) ---\n{}\n--- ACTUAL ({} bytes) ---\n{}",
            label, input.size(), input, output.size(), output);
        ++g_failures;
    }
}

void testTortureRoundtripCases()
{
    // 1. 乱序子表 `[a.c]` -> `[b]` -> `[a]` + 交错点分键 + 不同引号风格前缀 + 孤立尾部注释
    constexpr std::string_view kOutOfOrder =
        "# 文件头第一段注释\n"
        "# 文件头第二段注释\n"
        "\n"
        "\"root\" . x = 0xdead_beef # 十六进制字面量\n"
        "middle = 1_000.50e-2\n"
        "root.y = '''raw\n"
        "multiline\n"
        "string'''\n"
        "\n"
        "# 先定义子表 [alpha.child]\n"
        "[ alpha . \"child\" ] # 表头行尾注释\n"
        "k1 = true\n"
        "\n"
        "# 中间穿插无关表 [beta]\n"
        "[beta]\n"
        "arr = [\n"
        "  # 元素 1 前注释\n"
        "  1 , # 元素 1 逗号后注释\n"
        "  # 元素 2 前注释\n"
        "  2 # 无尾随逗号的行尾注释\n"
        "  # 闭合括号前孤立注释\n"
        "]\n"
        "\n"
        "# 最后才定义父表 [alpha]\n"
        "[alpha]\n"
        "self_key = 0b1010_0101\n"
        "\n"
        "[[products]] # 第一个表数组元素\n"
        "name = \"Hammer\"\n"
        "\n"
        "[products.meta]\n"
        "weight = 738\n"
        "\n"
        "[[products]] # 第二个表数组元素\n"
        "\n"
        "[[products]]\n"
        "name = 'Nail'\n"
        "\n"
        "# 文件末尾孤立注释块\n"
        "# 完\n";

    verifyLosslessString("out-of-order-torture", kOutOfOrder);

    // 2. CRLF 换行 + UTF-8 BOM + 无末尾换行的文件
    constexpr std::string_view kCrlfBomNoFinalNl =
        "\xEF\xBB\xBF# CRLF BOM header\r\n"
        "a   =   123 # keep custom spaces\r\n"
        "\r\n"
        "[sec]\r\n"
        "b = { x = 1,\r\n"
        "  # multiline inline table with CRLF\r\n"
        "  y = 14:30,\r\n"
        "}";

    verifyLosslessString("crlf-bom-no-final-nl", kCrlfBomNoFinalNl);

    // 3. 纯注释文档与空文档
    verifyLosslessString("empty-doc", "");
    verifyLosslessString("comment-only-doc", "# 仅有注释的模板文件\n\n# 第二段注释");

    // 4. 程序构造以 `\n` 开头的多行字符串往返后不应被开头换行裁剪规则吞掉首行换行
    {
        using toml = tomlkit::Item;
        toml doc = toml::document();
        tomlkit::Item mlBasic("\nleading newline basic\nline2");
        mlBasic.setStringStyle(tomlkit::StringStyle::MultilineBasic);
        doc["ml_basic"] = std::move(mlBasic);

        tomlkit::Item mlLiteral("\nleading newline literal\nline2");
        mlLiteral.setStringStyle(tomlkit::StringStyle::MultilineLiteral);
        doc["ml_literal"] = std::move(mlLiteral);

        const auto reparsed = tomlkit::parse(tomlkit::format(doc));
        CHECK(reparsed["ml_basic"].asString() == "\nleading newline basic\nline2");
        CHECK(reparsed["ml_literal"].asString() == "\nleading newline literal\nline2");
    }
}

void testRealWorldWorkspaceTomlFiles()
{
    // 若本地存在 GalTranslPP 示例配置与 mcpp.toml，直接做字节级 100% 原样往返回归测试
    const std::vector<fs::path> realFiles = {
        tomlkit::utf8ToPath(R"(D:\VSProj\tomlkitpp\mcpp.toml)"),
        tomlkit::utf8ToPath(R"(D:\VSProj\tomlkitpp\tomlkitpp\mcpp.toml)"),
        tomlkit::utf8ToPath(R"(D:\VSProj\GalTranslPP\mcpp.toml)"),
        tomlkit::utf8ToPath(R"(D:\VSProj\GalTranslPP\Example\BaseConfig\GlobalConfig.toml)"),
        tomlkit::utf8ToPath(R"(D:\VSProj\GalTranslPP\Example\BaseConfig\Prompt.toml)"),
        tomlkit::utf8ToPath(R"(D:\VSProj\GalTranslPP\Example\SampleProject\Config.toml)")
    };

    std::size_t testedCount = 0;
    for (const fs::path& p : realFiles) {
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            continue;
        }
        std::ifstream ifs(p, std::ios::binary);
        const std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        verifyLosslessString(tomlkit::pathToUtf8(p), raw);
        ++testedCount;
    }
    CHECK(testedCount >= 2);
}

int main()
{
    testTortureRoundtripCases();
    testRealWorldWorkspaceTomlFiles();

    if (g_failures != 0) {
        std::println(std::cerr, "test_roundtrip failed with {} error(s)", g_failures);
        return 1;
    }
    std::println("test_roundtrip: all checks passed!");
    return 0;
}
