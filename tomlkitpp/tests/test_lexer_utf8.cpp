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

#define CHECK(expr) check((expr), #expr, __LINE__)

void testUtf8Validation()
{
    using namespace tomlkit;

    CHECK(isValidUtf8("hello 世界 🚀"));

    // 孤立续字节
    std::size_t errPos = 999;
    CHECK(!isValidUtf8("abc\x80z", &errPos));
    CHECK(errPos == 3);

    // 2 字节超长编码 (\xC0\xAF)
    CHECK(!isValidUtf8("\xC0\xAF"));

    // 3 字节超长编码 (\xE0\x80\xAF)
    CHECK(!isValidUtf8("\xE0\x80\xAF"));

    // UTF-16 代理对 U+D800 (\xED\xA0\x80) 与 U+DFFF (\xED\xBF\xBF)
    CHECK(!isValidUtf8("\xED\xA0\x80"));
    CHECK(!isValidUtf8("\xED\xBF\xBF"));

    // 4 字节超出 U+10FFFF (\xF4\x90\x80\x80)
    CHECK(!isValidUtf8("\xF4\x90\x80\x80"));

    // UTF-16 BOM
    CHECK(!isValidUtf8("\xFF\xFE"));
    CHECK(!isValidUtf8("\xFE\xFF"));

    // UTF-8 BOM 识别与剥离
    detail::Lexer bomLexer("\xEF\xBB\xBFkey = 1");
    const auto bomRes = bomLexer.validateUtf8AndConsumeBom();
    CHECK(bomRes.has_value() && *bomRes == true);
    CHECK(bomLexer.position() == 3);
}

void testStringsAndV1_1Escapes()
{
    using namespace tomlkit;

    // v1.1 `\e` 与 `\xHH`（注意 `\xE9` 为 U+00E9 'é'，UTF-8 为 2 字节 \xC3\xA9）
    {
        detail::Lexer lex("\"\\e[31m\\x00\\x61\\xE9\"");
        const auto res = lex.lexString();
        CHECK(res.has_value());
        std::string expected;
        expected.append("\x1B[31m");
        expected.push_back('\0');
        expected.push_back('a');
        expected.append("\xC3\xA9");
        CHECK(res->value == expected);
        CHECK(res->style == StringStyle::Basic);
        CHECK(res->rawRepr == "\"\\e[31m\\x00\\x61\\xE9\"");
    }

    // v1.0 模式下拒绝 `\e` 与 `\xHH`
    {
        detail::Lexer lex1("\"\\e\"", ParseOptions{ .spec = SpecVersion::v1_0 });
        const auto r1 = lex1.lexString();
        CHECK(!r1.has_value() && r1.error().category == ErrorCategory::SpecRestriction);

        detail::Lexer lex2("\"\\x41\"", ParseOptions{ .spec = SpecVersion::v1_0 });
        const auto r2 = lex2.lexString();
        CHECK(!r2.has_value() && r2.error().category == ErrorCategory::SpecRestriction);
    }

    // 拒绝代理对转义 `\uD800` 与超范围 `\U00110000`
    {
        detail::Lexer lex1("\"\\uD800\"");
        CHECK(!lex1.lexString().has_value());

        detail::Lexer lex2("\"\\U00110000\"");
        CHECK(!lex2.lexString().has_value());
    }

    // 多行基本字符串：首行 CRLF 剥离 + 行尾反斜杠续行 + 闭合前双引号 `"""""` + CRLF 归一化为 LF
    {
        const std::string_view src = "\"\"\"\r\nhello \\\r\n   \r\n  world\r\nend\"\"\"\"\"";
        detail::Lexer lex(src);
        const auto res = lex.lexString();
        CHECK(res.has_value());
        CHECK(res->value == "hello world\nend\"\"");
        CHECK(res->rawRepr == src);
    }

    // 多行字面量字符串：CRLF 归一化为 LF，同时 rawRepr 原样保留 CRLF
    {
        const std::string_view src = "'''\r\nline1\\n\r\nline2''''";
        detail::Lexer lex(src);
        const auto res = lex.lexString();
        CHECK(res.has_value());
        CHECK(res->value == "line1\\n\nline2'");
        CHECK(res->rawRepr == src);
    }

    // 裸 `\r` 必须拒绝
    {
        detail::Lexer lex("\"\"\"\nhello\rworld\"\"\"");
        const auto res = lex.lexString();
        CHECK(!res.has_value() && res.error().category == ErrorCategory::ControlCharacter);
    }
}

void testNumbers()
{
    using namespace tomlkit;

    const auto lexNumFunc = [](std::string_view s)
        {
            detail::Lexer lex(s);
            return lex.lexScalar();
        };

    // 64 位有符号整数上下界
    {
        const auto rMax = lexNumFunc("9_223_372_036_854_775_807");
        CHECK(rMax.has_value() && std::get<std::int64_t>(rMax->payload) == std::numeric_limits<std::int64_t>::max());

        const auto rMin = lexNumFunc("-9_223_372_036_854_775_808");
        CHECK(rMin.has_value() && std::get<std::int64_t>(rMin->payload) == std::numeric_limits<std::int64_t>::min());

        CHECK(!lexNumFunc("9223372036854775808").has_value());
        CHECK(!lexNumFunc("-9223372036854775809").has_value());
        CHECK(!lexNumFunc("0x8000_0000_0000_0000").has_value());
    }

    // 进制前缀校验
    {
        const auto rHex = lexNumFunc("0x00dead_BEEF");
        CHECK(rHex.has_value() && std::get<std::int64_t>(rHex->payload) == 0xdeadbeefLL && rHex->integerRadix == IntegerRadix::Hexadecimal);

        const auto rOct = lexNumFunc("0o0755");
        CHECK(rOct.has_value() && std::get<std::int64_t>(rOct->payload) == 0755LL && rOct->integerRadix == IntegerRadix::Octal);

        const auto rBin = lexNumFunc("0b0010_1010");
        CHECK(rBin.has_value() && std::get<std::int64_t>(rBin->payload) == 42LL && rBin->integerRadix == IntegerRadix::Binary);

        CHECK(!lexNumFunc("+0x10").has_value());
        CHECK(!lexNumFunc("-0b10").has_value());
        CHECK(!lexNumFunc("0X10").has_value());
        CHECK(!lexNumFunc("01").has_value());
        CHECK(!lexNumFunc("1__2").has_value());
        CHECK(!lexNumFunc("0x_1").has_value());
    }

    // 浮点数与特殊值
    {
        const auto rNegZero = lexNumFunc("-0.0");
        CHECK(rNegZero.has_value());
        const double nz = std::get<double>(rNegZero->payload);
        CHECK(nz == 0.0 && std::signbit(nz));

        const auto rInf = lexNumFunc("-inf");
        CHECK(rInf.has_value() && std::isinf(std::get<double>(rInf->payload)) && std::get<double>(rInf->payload) < 0);

        const auto rNan = lexNumFunc("+nan");
        CHECK(rNan.has_value() && std::isnan(std::get<double>(rNan->payload)));

        const auto rExp = lexNumFunc("+1_0.2_5e+002");
        CHECK(rExp.has_value() && std::get<double>(rExp->payload) == 1025.0);

        CHECK(!lexNumFunc(".5").has_value());
        CHECK(!lexNumFunc("5.").has_value());
        CHECK(!lexNumFunc("5.e2").has_value());
        CHECK(!lexNumFunc("INF").has_value());
        CHECK(!lexNumFunc("NaN").has_value());
    }
}

void testDatesAndTimes()
{
    using namespace tomlkit;

    // v1.1 省略秒数的时间与空格分隔日期时间
    {
        detail::Lexer lex1("14:15 # comment");
        const auto r1 = lex1.lexScalar();
        CHECK(r1.has_value());
        const auto& lt = std::get<LocalTime>(r1->payload);
        CHECK(lt.hour == 14 && lt.minute == 15 && lt.second == 0 && !lt.hasSeconds);
        CHECK(lt == (LocalTime{ .hour = 14, .minute = 15, .second = 0 }));
        CHECK(lt.toString(false) == "14:15");
        CHECK(lt.toString(true) == "14:15:00");

        detail::Lexer lex2("2026-11-14 22:30 # trailing comment");
        const auto r2 = lex2.lexScalar();
        CHECK(r2.has_value());
        const auto& ldt = std::get<LocalDateTime>(r2->payload);
        CHECK(ldt.date == (LocalDate{ 2026, 11, 14 }));
        CHECK(ldt.time == (LocalTime{ 22, 30, 0 }));
        CHECK(ldt.delimiter == ' ');

        // 日期后紧跟空格 + 注释（而非数字），应解析为纯 LocalDate！
        detail::Lexer lex3("2026-11-14 # birthday");
        const auto r3 = lex3.lexScalar();
        CHECK(r3.has_value() && std::holds_alternative<LocalDate>(r3->payload));

        // v1.0 模式下拒绝省略秒数
        detail::Lexer lexV10("14:15", ParseOptions{ .spec = SpecVersion::v1_0 });
        const auto rV10 = lexV10.lexScalar();
        CHECK(!rV10.has_value() && rV10.error().category == ErrorCategory::SpecRestriction);
    }

    // 闰年与时区换算等价性
    {
        detail::Lexer leapOk("2000-02-29");
        CHECK(leapOk.lexScalar().has_value());

        detail::Lexer leapBad("2100-02-29");
        CHECK(!leapBad.lexScalar().has_value());

        detail::Lexer odt1("2026-11-14T08:00:00.123456+08:00");
        detail::Lexer odt2("2026-11-14t00:00:00.123456z");
        const auto a = odt1.lexScalar();
        const auto b = odt2.lexScalar();
        CHECK(a.has_value() && b.has_value());
        CHECK(std::get<OffsetDateTime>(a->payload) == std::get<OffsetDateTime>(b->payload));
    }
}

void testKeysAndDecor()
{
    using namespace tomlkit;

    // `3.14` 在键域必须解析为两段裸键 `"3"` 与 `"14"`
    {
        detail::Lexer lex("3.14 = 1");
        const auto segs = lex.lexDottedKey();
        CHECK(segs.has_value() && segs->size() == 2);
        CHECK((*segs)[0].name == "3" && (*segs)[1].name == "14");
    }

    // 混合引号与空白的点分键
    {
        detail::Lexer lex("\"site\" . 'google.com' . port = 443");
        const auto segs = lex.lexDottedKey();
        CHECK(segs.has_value() && segs->size() == 3);
        CHECK((*segs)[0].name == "site" && (*segs)[0].decor.suffix == " ");
        CHECK((*segs)[1].name == "google.com" && (*segs)[1].decor.prefix == " " && (*segs)[1].decor.suffix == " ");
        CHECK((*segs)[2].name == "port" && (*segs)[2].decor.prefix == " " && (*segs)[2].decor.suffix == " ");
    }

    // Decor 注释提取与编辑
    {
        Decor d{
            .prefix = "\n  # 第一行注释\n  # 第二行注释\n  ",
            .suffix = " # 初始行尾注释\r\n"
        };
        const auto leading = d.leadingComments();
        CHECK(leading.size() == 2 && leading[0] == "第一行注释" && leading[1] == "第二行注释");
        CHECK(d.inlineComment() == "初始行尾注释");

        d.addLeadingComment("新增第三行");
        CHECK(d.prefix == "\n  # 第一行注释\n  # 第二行注释\n  # 新增第三行\n  ");

        d.setInlineComment("更新后的注释");
        CHECK(d.suffix == " # 更新后的注释\r\n");

        d.clearInlineComment();
        CHECK(d.suffix == "\r\n");
    }
}

int main()
{
    testUtf8Validation();
    testStringsAndV1_1Escapes();
    testNumbers();
    testDatesAndTimes();
    testKeysAndDecor();

    if (g_failures != 0) {
        std::println(std::cerr, "test_lexer_utf8 failed with {} error(s)", g_failures);
        return 1;
    }
    std::println("test_lexer_utf8: all checks passed!");
    return 0;
}
