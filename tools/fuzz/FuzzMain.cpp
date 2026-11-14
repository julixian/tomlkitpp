import std;
import tomlkitpp;

namespace fs = std::filesystem;

namespace
{
    void verifyInvariants(std::string_view sv)
    {
        if (sv.size() > 65536) {
            return;
        }

        const tomlkit::ParseOptions v11Opts{ .spec = tomlkit::SpecVersion::v1_1, .maxNestingDepth = 64 };
        const tomlkit::ParseOptions v10Opts{ .spec = tomlkit::SpecVersion::v1_0, .maxNestingDepth = 64 };

        // 1. v1.0 与 v1.1 解析对任意畸形字节流绝不能崩溃、越界或抛出未捕获异常
        const auto v10Res = tomlkit::tryParse(sv, v10Opts, "fuzz.toml");
        (void)v10Res;

        const auto v11Res = tomlkit::tryParse(sv, v11Opts, "fuzz.toml");
        if (!v11Res.has_value()) {
            return;
        }

        // 2. 核心不变量 A：任意合法 TOML v1.1 输入必须满足 100% 原始字节全等无损往返
        const std::string lossless = v11Res->toToml();
        if (lossless != sv) {
            std::println(std::cerr,
                "[FUZZ INVARIANT FAILURE] Byte-lossless roundtrip mismatch!\n--- INPUT ({} bytes) ---\n{}\n--- OUTPUT ({} bytes) ---\n{}",
                sv.size(), sv, lossless.size(), lossless);
            std::abort();
        }

        // 3. 核心不变量 B：无损输出再次解析后必须成功且语义全等
        const auto reparsed = tomlkit::tryParse(lossless, v11Opts, "fuzz-reparse.toml");
        if (!reparsed.has_value() || !(*reparsed == *v11Res)) {
            std::println(std::cerr, "[FUZZ INVARIANT FAILURE] Reparse semantic mismatch!");
            std::abort();
        }

        // 4. 核心不变量 C：局部手术式插入 + 删除未知键不得崩溃或破坏可解析性
        tomlkit::Item mutated = *v11Res;
        if (!mutated.contains("__fuzz_surgical_probe__")) {
            mutated["__fuzz_surgical_probe__"] = 42;
            mutated["__fuzz_surgical_probe__"].setInlineComment("fuzz");
            const std::string editedText = mutated.toToml();
            const auto editedReparsed = tomlkit::tryParse(editedText, v11Opts, "fuzz-surgical.toml");
            if (!editedReparsed.has_value()) {
                std::println(std::cerr,
                    "[FUZZ INVARIANT FAILURE] Surgical edit produced invalid TOML: {}\n--- EDITED ---\n{}",
                    editedReparsed.error().message, editedText);
                std::abort();
            }
            mutated.erase("__fuzz_surgical_probe__");
            (void)mutated.toToml();
        }
    }
}

#ifdef TOMLKIT_LIBFUZZER

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view sv(reinterpret_cast<const char*>(data), size);
    verifyInvariants(sv);
    return 0;
}

#else

namespace
{
    constexpr std::array<std::string_view, 16> kBuiltinSeeds = {
        "",
        "# comment only\n",
        "\xEF\xBB\xBFk = 1 # bom\r\n",
        "a = 1\nb = \"hello\\e\\x41\"\nc = [1, 2, { x = 3, y = 4, }, ]\n",
        "[tbl]\ndotted.a.b = 0xdead_beef\nfl = -0.0\nsp = nan\n",
        "[[aot]]\nk = '''raw\nmultiline''''\n[[aot]]\nk = \"\"\"\nhello \\\n  world\"\"\"\"\"\n",
        "t1 = 07:30\nt2 = 2026-11-14 09:45\nt3 = 2026-11-14T09:45:59.123456789-08:00\n",
        "inline_ml = {\n  # c1\n  a = [1,\n    # c2\n    2\n  ],\n  'quoted.key' = true,\n}\n",
        "[a.b.c]\nx = 1\n[a]\ny = 2\n",
        "3.14 = -9_223_372_036_854_775_808\n",
        "utf8_key_🚀 = \"こんにちは世界\"\n",
        "bad1 = [1, 2\n",
        "bad2 = \"\\uD800\"\n",
        "bad3 = { a = 1 }\nbad3.b = 2\n",
        "deep = [[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]\n",
        "[x]\na = 0b1010_0101\nb = 0o755\nc = +inf\nd = -inf\n"
    };

    constexpr std::array<std::string_view, 20> kTokens = {
        "=", ".", ",", "[", "]", "[[", "]]", "{", "}", "\"", "'", "\"\"\"", "'''",
        "#cmt\n", "\r\n", "\n", "\\e", "\\x00", "\\u0041", "2026-02-29T12:00Z"
    };

    std::string mutateInput(std::string base, std::mt19937_64& rng, const std::vector<std::string>& corpus)
    {
        const int ops = 1 + (int)(rng() % 4);
        for (int op = 0; op < ops; ++op) {
            const std::uint64_t kind = rng() % 5;
            if (kind == 0 && !base.empty()) {
                // 随机单字节翻转/替换
                const std::size_t idx = rng() % base.size();
                base[idx] = (char)(rng() & 0xFF);
            }
            else if (kind == 1 && !base.empty()) {
                // 随机删除一段切片
                const std::size_t pos = rng() % base.size();
                const std::size_t len = 1 + (rng() % std::min<std::size_t>(16, base.size() - pos));
                base.erase(pos, len);
            }
            else if (kind == 2) {
                // 插入 TOML 语法敏感 token
                const std::string_view tok = kTokens[rng() % kTokens.size()];
                const std::size_t pos = base.empty() ? 0 : (rng() % (base.size() + 1));
                base.insert(pos, tok);
            }
            else if (kind == 3 && !corpus.empty()) {
                // 与语料库另一个样本交叉拼接
                const std::string& other = corpus[rng() % corpus.size()];
                if (!other.empty()) {
                    const std::size_t cutA = base.empty() ? 0 : (rng() % base.size());
                    const std::size_t cutB = rng() % other.size();
                    base = base.substr(0, cutA) + other.substr(cutB);
                }
            }
            else if (!base.empty()) {
                // 复制一段内部片段（构造重复节头/嵌套结构）
                const std::size_t pos = rng() % base.size();
                const std::size_t len = 1 + (rng() % std::min<std::size_t>(24, base.size() - pos));
                const std::string chunk = base.substr(pos, len);
                const std::size_t ins = rng() % (base.size() + 1);
                base.insert(ins, chunk);
            }
            if (base.size() > 8192) {
                base.resize(8192);
            }
        }
        return base;
    }

    void collectCorpusFiles(const fs::path& p, std::vector<std::string>& corpus)
    {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) {
            std::ifstream ifs(p, std::ios::binary);
            std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            if (raw.size() <= 65536) {
                corpus.push_back(std::move(raw));
            }
            return;
        }
        if (!fs::is_directory(p, ec)) {
            return;
        }
        for (const auto& entry : fs::recursive_directory_iterator(p, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".toml") {
                std::ifstream ifs(entry.path(), std::ios::binary);
                std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                if (raw.size() <= 65536) {
                    corpus.push_back(std::move(raw));
                }
            }
        }
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> corpus;
    for (std::string_view seed : kBuiltinSeeds) {
        corpus.emplace_back(seed);
    }

    std::size_t iterations = 20000;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg.starts_with("--iterations=")) {
            const auto valStr = arg.substr(std::string_view("--iterations=").size());
            std::size_t parsed = 0;
            if (std::from_chars(valStr.data(), valStr.data() + valStr.size(), parsed).ec == std::errc{}) {
                iterations = parsed;
            }
        }
        else {
            collectCorpusFiles(tomlkit::utf8ToPath(arg), corpus);
        }
    }

    for (const std::string& item : corpus) {
        verifyInvariants(item);
    }

    std::mt19937_64 rng(0x544F4D4C4B495450ULL);
    for (std::size_t i = 0; i < iterations; ++i) {
        const std::string& base = corpus[rng() % corpus.size()];
        const std::string mutated = mutateInput(base, rng, corpus);
        verifyInvariants(mutated);
    }

    std::println("tomlkit-fuzz: verified {} seed(s) + {} mutated input(s) with 0 invariant violations!",
        corpus.size(), iterations);
    return 0;
}

#endif
