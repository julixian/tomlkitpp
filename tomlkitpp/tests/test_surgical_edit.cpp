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

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void testSurgicalSingleLineEdit()
{
    using namespace tomlkit;

    constexpr std::string_view kOriginal =
        "# 全局构建配置\n"
        "[workspace]\n"
        "mask   = 0xFF00 # 保持十六进制不被动\n"
        "opt    = 0      # 日常调试关闭优化\n"
        "target = 'x86_64-pc-windows-msvc' # 单引号字面量\n"
        "\n"
        "# 发布配置节\n"
        "[profile.release]\n"
        "lto = true # 开启链接时优化\n";

    Item doc = parse(kOriginal);

    // 仅修改 `workspace.opt` 的值：其等号后自定义空格与行尾注释 `# 日常调试关闭优化` 应完整保留，其余行纹丝不动！
    doc["workspace", "opt"] = 3;

    constexpr std::string_view kExpectedAfterOptChange =
        "# 全局构建配置\n"
        "[workspace]\n"
        "mask   = 0xFF00 # 保持十六进制不被动\n"
        "opt    = 3      # 日常调试关闭优化\n"
        "target = 'x86_64-pc-windows-msvc' # 单引号字面量\n"
        "\n"
        "# 发布配置节\n"
        "[profile.release]\n"
        "lto = true # 开启链接时优化\n";

    // 注意：`0      # ...` 中 `0` 后面的 6 个空格位于 `suffix` 中，将 `0` 改为单字符 `3` 后连对齐都完美保持！
    CHECK(doc.toToml() == kExpectedAfterOptChange);

    // 在 `[workspace]` 末尾新增一个键并添加行前注释与行尾注释：不应破坏下方 `# 发布配置节` 的位置！
    doc["workspace", "debug"] = true;
    doc["workspace", "debug"].addLeadingComment("是否生成调试符号");
    doc["workspace", "debug"].setInlineComment("保留 PDB");

    constexpr std::string_view kExpectedAfterInsert =
        "# 全局构建配置\n"
        "[workspace]\n"
        "mask   = 0xFF00 # 保持十六进制不被动\n"
        "opt    = 3      # 日常调试关闭优化\n"
        "target = 'x86_64-pc-windows-msvc' # 单引号字面量\n"
        "# 是否生成调试符号\n"
        "debug = true # 保留 PDB\n"
        "\n"
        "# 发布配置节\n"
        "[profile.release]\n"
        "lto = true # 开启链接时优化\n";

    CHECK(doc.toToml() == kExpectedAfterInsert);
}

void testArrayCommentPreservationOnEraseAndPush()
{
    using namespace tomlkit;

    constexpr std::string_view kSrc =
        "items = [\n"
        "  \"alpha\", # 第一项注释\n"
        "  \"beta\", # 第二项注释\n"
        "  \"gamma\", # 第三项注释\n"
        "]\n";

    Item doc = parse(kSrc);
    // 删除中间的 `"beta"`，第一项和第三项各自的同行注释必须严丝合缝保留在原项上
    doc["items"].erase(1);
    doc["items"].push_back("delta");

    constexpr std::string_view kExpected =
        "items = [\n"
        "  \"alpha\", # 第一项注释\n"
        "  \"gamma\", # 第三项注释\n"
        "  \"delta\",\n"
        "]\n";

    CHECK(doc.toToml() == kExpected);
}

void testAtomicFileSaveAndReload()
{
    using namespace tomlkit;

    // 覆盖超出 GBK / ANSI 范围的多语言 + Emoji Unicode 路径，验证 Windows 下宽字符路径与 UTF-8 Everywhere 互转无损
    const fs::path tempDir = fs::temp_directory_path() / utf8ToPath("tomlkitpp_测试_テスト_🚀");
    const fs::path targetFile = tempDir / utf8ToPath("配置_設定_✨.toml");
    const std::string targetFileUtf8 = pathToUtf8(targetFile);

    Item doc = Item::document();
    doc.addLeadingComment("自动生成的测试配置");
    doc["app", "name"] = "tomlkitpp";
    doc["app", "hexId"] = 255;
    doc["app", "hexId"].setIntegerRadix(IntegerRadix::Hexadecimal);
    doc["app", "hexId"].setInlineComment("十六进制输出");

    // 1. 通过 fs::path 原子写入并通过 UTF-8 string_view 读取
    saveFileAtomic(targetFile, doc);
    const Item reloadedFromUtf8Str = parseFile(targetFileUtf8);
    CHECK(reloadedFromUtf8Str == doc);
    CHECK(reloadedFromUtf8Str.toToml().contains("0xff # 十六进制输出"));

    // 2. 通过 UTF-8 string_view 原子写入并通过宽字符串 / fs::path 读取
    doc["app", "version"] = "1.0.0";
    saveFileAtomic(targetFileUtf8, doc);
    const Item reloadedFromPath = parseFile(targetFile);
    CHECK(reloadedFromPath == doc);

    // 3. 验证不存在的 Unicode 路径报错信息保持合法 UTF-8，无乱码
    const auto missingRes = tryParseFile(pathToUtf8(tempDir / utf8ToPath("不存在_存在しない_🔍.toml")));
    CHECK(!missingRes.has_value());
    CHECK(isValidUtf8(missingRes.error().message));
    CHECK(missingRes.error().sourcePath.contains("不存在_存在しない_🔍.toml"));

    std::error_code ec;
    fs::remove_all(tempDir, ec);
}

int main()
{
    testSurgicalSingleLineEdit();
    testArrayCommentPreservationOnEraseAndPush();
    testAtomicFileSaveAndReload();

    if (g_failures != 0) {
        std::println(std::cerr, "test_surgical_edit failed with {} error(s)", g_failures);
        return 1;
    }
    std::println("test_surgical_edit: all checks passed!");
    return 0;
}
