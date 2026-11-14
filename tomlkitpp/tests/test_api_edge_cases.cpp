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

    template<typename Ex, typename Fn>
    void checkThrows(Fn&& fn, std::string_view expr, int line)
    {
        try {
            fn();
            std::println(std::cerr, "[FAIL] line {}: expected exception was not thrown: {}", line, expr);
            ++g_failures;
        }
        catch (const Ex& ex) {
            if (std::string_view(ex.what()).empty()) {
                std::println(std::cerr, "[FAIL] line {}: thrown exception had empty what(): {}", line, expr);
                ++g_failures;
            }
        }
        catch (const std::exception& ex) {
            std::println(std::cerr, "[FAIL] line {}: wrong exception type ({}): {}", line, ex.what(), expr);
            ++g_failures;
        }
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
#define CHECK_THROWS(ExType, ...) checkThrows<ExType>([&]() { (void)(__VA_ARGS__); }, #__VA_ARGS__, __LINE__)

void testExceptionColdPaths()
{
    using namespace tomlkit;

    // 1. 语法错误抛 `SyntaxError`（附带准确行列号与错误类别）
    CHECK_THROWS(SyntaxError, parse("broken = [1, 2"));
    try {
        (void)parse("ok = 1\nbad = 0xGG\n", {}, "test.toml");
        CHECK(false);
    }
    catch (const SyntaxError& err) {
        CHECK(err.location().line == 2);
        CHECK(err.error().sourcePath == "test.toml");
        CHECK(!err.error().format().empty());
    }

    // 2. 类型不匹配抛 `TypeError`（覆盖全部强类型访问器与 `get<T>` 窄化转换冷路径）
    Item doc = parse("flag = true\nnum = -42\nbig = 500\nflt = 3.5\nstr = 'hi'\narr = [10, 20]\n[tbl]\nk = 1\n");
    const Item& cdoc = doc;

    CHECK_THROWS(TypeError, cdoc["flag"].asInteger());
    CHECK_THROWS(TypeError, cdoc["num"].asBoolean());
    CHECK_THROWS(TypeError, cdoc["num"].asFloat());
    CHECK_THROWS(TypeError, cdoc["num"].asString());
    CHECK_THROWS(TypeError, cdoc["num"].asLocalDate());
    CHECK_THROWS(TypeError, cdoc["num"].asLocalTime());
    CHECK_THROWS(TypeError, cdoc["num"].asLocalDateTime());
    CHECK_THROWS(TypeError, cdoc["num"].asOffsetDateTime());
    CHECK_THROWS(TypeError, cdoc["str"].asNumber());

    // 窄化整数溢出与负值转无符号应拒绝并抛 `TypeError`
    CHECK(!cdoc["big"].tryGet<std::uint8_t>().has_value());
    CHECK(!cdoc["num"].tryGet<std::uint32_t>().has_value());
    CHECK_THROWS(TypeError, cdoc["big"].get<std::uint8_t>());
    CHECK_THROWS(TypeError, cdoc["num"].get<std::uint32_t>());
    CHECK_THROWS(TypeError, cdoc["flt"].get<int>());
    CHECK_THROWS(TypeError, cdoc["str"].get<std::vector<int>>());

    // 超出 TOML 有符号 64 位上限的 `uint64_t` 构造应抛 `OutOfRangeError`
    CHECK_THROWS(OutOfRangeError, Item(std::numeric_limits<std::uint64_t>::max()));

    // 3. 在非容器标量上按表键或数组下标访问（覆盖 v1.0.1 抽离的 `throwCannot*` 辅助函数）
    CHECK_THROWS(TypeError, cdoc["num"]["sub"]);
    CHECK_THROWS(TypeError, doc["num"]["sub"]);
    CHECK_THROWS(TypeError, cdoc["num"][0]);
    CHECK_THROWS(TypeError, doc["num"][0]);
    CHECK_THROWS(TypeError, cdoc["num", "deep", 0]);
    CHECK_THROWS(TypeError, doc["arr"]["not_a_table_key"]);

    // 4. 负数数组下标、越界下标与缺失键抛 `OutOfRangeError`
    CHECK_THROWS(OutOfRangeError, doc["arr"][-1]);
    CHECK_THROWS(OutOfRangeError, cdoc["arr"][-5]);
    CHECK_THROWS(OutOfRangeError, doc["arr", -1]);
    CHECK_THROWS(OutOfRangeError, cdoc["arr"].at(2));
    CHECK_THROWS(OutOfRangeError, cdoc["tbl"].at("missing"));
    CHECK_THROWS(OutOfRangeError, cdoc["tbl", "missing"]);
    CHECK_THROWS(OutOfRangeError, cdoc["tbl"].keyAt("missing"));
    CHECK_THROWS(OutOfRangeError, find<int>(cdoc, "tbl", "missing"));
    CHECK_THROWS(OutOfRangeError, find<int>(cdoc, "arr", 99));

    Item emptyArr = Item::array();
    CHECK_THROWS(OutOfRangeError, emptyArr.front());
    CHECK_THROWS(OutOfRangeError, emptyArr.back());
}

void testCopyMoveAndHashTableStress()
{
    using namespace tomlkit;

    Item original = parse(
        "# 顶部注释\n"
        "[server]\n"
        "host = \"127.0.0.1\" # 主机\n"
        "ports = [80, 443]\n"
    );

    // 1. 深拷贝独立性：修改副本不应污染原对象的值、数组元素或注释装饰
    Item copied = original;
    copied["server", "host"] = "0.0.0.0";
    copied["server", "host"].setInlineComment("新注释");
    copied["server", "ports"].push_back(8080);

    CHECK(original["server", "host"].asString() == "127.0.0.1");
    CHECK(original["server", "host"].inlineComment() == "主机");
    CHECK(original["server", "ports"].size() == 2);
    CHECK(copied["server", "ports"].size() == 3);

    // 2. 自拷贝与移动语义安全性
    Item& alias = copied;
    copied = alias;
    CHECK(copied["server", "host"].asString() == "0.0.0.0");

    const std::string expectedDump = copied.toToml();
    Item movedCtor(std::move(copied));
    CHECK(movedCtor.toToml() == expectedDump);

    Item movedAssign = Item::document();
    movedAssign = std::move(movedCtor);
    CHECK(movedAssign.toToml() == expectedDump);

    // 3. 超过 `TableStorage::kHashIndexThreshold` (16) 的大规模键插入、删除、覆盖与迭代顺序验证
    Item bigTbl = Item::document();
    constexpr int kKeyCount = 64;
    for (int i = 0; i < kKeyCount; ++i) {
        auto [itRef, inserted] = bigTbl.insertOrAssign(std::format("key_{:03d}", i), i * 10);
        CHECK(inserted);
        CHECK(itRef.asInteger() == i * 10);
    }
    CHECK(bigTbl.size() == (std::size_t)kKeyCount);

    // 覆盖已有键不应改变插入顺序且返回 inserted == false
    {
        auto [itRef, inserted] = bigTbl.insertOrAssign("key_010", 9999);
        CHECK(!inserted);
        CHECK(itRef.asInteger() == 9999);
    }

    // 删除所有偶数键，验证开放寻址哈希索引重建后查找与顺序均准确无误
    for (int i = 0; i < kKeyCount; i += 2) {
        CHECK(bigTbl.erase(std::format("key_{:03d}", i)));
    }
    CHECK(!bigTbl.erase("non_existent_key"));
    CHECK(bigTbl.size() == (std::size_t)(kKeyCount / 2));

    int expectedOdd = 1;
    for (auto [key, val] : std::as_const(bigTbl).tableItems()) {
        CHECK(key.name() == std::format("key_{:03d}", expectedOdd));
        const int expectedVal = (expectedOdd == 10) ? 9999 : (expectedOdd * 10);
        CHECK(val.asInteger() == expectedVal);
        CHECK(bigTbl.contains(key.name()));
        expectedOdd += 2;
    }
    CHECK(expectedOdd == kKeyCount + 1);

    const Item reparsedBig = parse(bigTbl.toToml());
    CHECK(reparsedBig == bigTbl);

    bigTbl.clear();
    CHECK(bigTbl.empty() && bigTbl.size() == 0);
}

void testExtremeScalarAndControlCharRoundtrip()
{
    using namespace tomlkit;

    Item doc = Item::document();

    // 1. 极端 64 位整数边界（尤其是 `INT64_MIN` 取负溢出陷阱）与不同进制导出
    doc["int_min"] = std::numeric_limits<std::int64_t>::min();
    doc["int_max"] = std::numeric_limits<std::int64_t>::max();
    doc["hex_max"] = std::numeric_limits<std::int64_t>::max();
    doc["hex_max"].setIntegerRadix(IntegerRadix::Hexadecimal);
    doc["oct_val"] = 0755;
    doc["oct_val"].setIntegerRadix(IntegerRadix::Octal);
    doc["bin_val"] = 0b101101;
    doc["bin_val"].setIntegerRadix(IntegerRadix::Binary);
    // 负整数即使被设为 Hex 也必须安全回退为十进制（TOML 规范不允许 `-0x...`）
    doc["neg_hex_fallback"] = -255;
    doc["neg_hex_fallback"].setIntegerRadix(IntegerRadix::Hexadecimal);

    // 2. 极端浮点数（`-0.0`、`+inf`、`-inf`、`nan`、次正规数、`DBL_MAX`）
    doc["neg_zero"] = -0.0;
    doc["pos_inf"] = std::numeric_limits<double>::infinity();
    doc["neg_inf"] = -std::numeric_limits<double>::infinity();
    doc["quiet_nan"] = std::numeric_limits<double>::quiet_NaN();
    doc["denorm_min"] = std::numeric_limits<double>::denorm_min();
    doc["dbl_max"] = std::numeric_limits<double>::max();

    // 3. 含全套 ASCII 控制字符 (0x00..0x1F, 0x7F)、引号簇与反斜杠的字符串
    std::string allControls;
    for (int c = 0; c <= 0x1F; ++c) {
        allControls.push_back((char)c);
    }
    allControls.push_back('\x7F');
    doc["controls"] = allControls;

    Item trickyMlBasic("line1 \"\"\" triple quotes \"\"\" inside\nand trailing \"\"");
    trickyMlBasic.setStringStyle(StringStyle::MultilineBasic);
    doc["tricky_ml_basic"] = std::move(trickyMlBasic);

    // 含有 `'''` 的字符串若设为 MultilineLiteral，序列化器必须安全回退为可无损转义的样式而不产出非法 TOML
    Item trickyMlLit("cannot stay raw ''' triple single quotes ''' here");
    trickyMlLit.setStringStyle(StringStyle::MultilineLiteral);
    doc["tricky_ml_lit"] = std::move(trickyMlLit);

    // 4. 极端日期时间偏移 (-23:59 / +23:59) 与纳秒精度
    doc["odt_min_offset"] = OffsetDateTime{
        .date = LocalDate{ 2024, 2, 29 },
        .time = LocalTime{ .hour = 23, .minute = 59, .second = 59, .nanosecond = 999999999, .subsecondDigits = 9, .hasSeconds = true },
        .offsetMinutes = -(23 * 60 + 59)
    };
    doc["odt_max_offset"] = OffsetDateTime{
        .date = LocalDate{ 2000, 2, 29 },
        .time = LocalTime{ .hour = 0, .minute = 0, .second = 0, .nanosecond = 1, .subsecondDigits = 9, .hasSeconds = true },
        .offsetMinutes = +(23 * 60 + 59)
    };

    // 同时验证 v1.1 与严格 v1.0 序列化导出均可被对应解析器 100% 语义无损还原
    for (const SpecVersion spec : { SpecVersion::v1_1, SpecVersion::v1_0 }) {
        const std::string emitted = doc.toToml(FormatOptions{ .spec = spec });
        const auto reparsed = tryParse(emitted, ParseOptions{ .spec = spec });
        if (!reparsed) {
            std::println(std::cerr, "[FAIL] failed to reparse under spec {}: {}\n{}",
                spec == SpecVersion::v1_1 ? "v1.1" : "v1.0",
                reparsed.error().message,
                emitted);
            ++g_failures;
            continue;
        }
        CHECK(*reparsed == doc);
        CHECK((*reparsed)["int_min"].asInteger() == std::numeric_limits<std::int64_t>::min());
        CHECK((*reparsed)["int_max"].asInteger() == std::numeric_limits<std::int64_t>::max());
        CHECK((*reparsed)["controls"].asString() == allControls);
        CHECK(std::signbit((*reparsed)["neg_zero"].asFloat()));
        CHECK(std::isnan((*reparsed)["quiet_nan"].asFloat()));
    }

    // 非法时区偏移与非法日期拒绝验证
    CHECK(!tryParse("dt = 2026-11-14T08:00:00+24:00\n").has_value());
    CHECK(!tryParse("dt = 2026-11-14T08:00:00-00:60\n").has_value());
    CHECK(!tryParse("dt = 1900-02-29\n").has_value());
}

int main()
{
    testExceptionColdPaths();
    testCopyMoveAndHashTableStress();
    testExtremeScalarAndControlCharRoundtrip();

    if (g_failures != 0) {
        std::println(std::cerr, "test_api_edge_cases failed with {} error(s)", g_failures);
        return 1;
    }
    std::println("test_api_edge_cases: all checks passed!");
    return 0;
}
