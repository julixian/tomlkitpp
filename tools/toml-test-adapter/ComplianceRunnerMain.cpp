#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

import std;
import tomlkitpp;
import TaggedJson;

namespace fs = std::filesystem;

namespace
{
    std::string readBinaryFile(const fs::path& p)
    {
        std::ifstream ifs(p, std::ios::binary);
        if (!ifs.is_open()) {
            throw std::runtime_error(std::format("Cannot open file: {}", tomlkit::pathToUtf8(p)));
        }
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    struct SuiteStats
    {
        int validPassed = 0;
        int validFailed = 0;
        int invalidPassed = 0;
        int invalidFailed = 0;
        int losslessPassed = 0;
        int losslessFailed = 0;
        int encoderPassed = 0;
        int encoderFailed = 0;
    };

    SuiteStats runManifest(const fs::path& testsDir, std::string_view manifestName, tomlkit::SpecVersion spec)
    {
        SuiteStats stats{};
        const fs::path manifestPath = testsDir / tomlkit::utf8ToPath(manifestName);
        const std::string manifestContent = readBinaryFile(manifestPath);

        std::size_t pos = 0;
        while (pos < manifestContent.size()) {
            std::size_t nl = manifestContent.find('\n', pos);
            if (nl == std::string::npos) {
                nl = manifestContent.size();
            }
            std::string_view line = std::string_view(manifestContent).substr(pos, nl - pos);
            if (line.ends_with('\r')) {
                line.remove_suffix(1);
            }
            pos = nl + 1;

            if (line.empty() || !line.ends_with(".toml")) {
                continue;
            }

            const fs::path tomlPath = testsDir / tomlkit::utf8ToPath(line);
            const std::string rawToml = readBinaryFile(tomlPath);
            const tomlkit::ParseOptions parseOpts{ .spec = spec, .maxNestingDepth = 512 };
            const tomlkit::FormatOptions fmtOpts{ .spec = spec };

            if (line.starts_with("invalid/")) {
                const auto res = tomlkit::tryParse(rawToml, parseOpts, line);
                if (!res) {
                    ++stats.invalidPassed;
                }
                else {
                    ++stats.invalidFailed;
                    if (stats.invalidFailed <= 20) {
                        std::println(std::cerr, "[{}] INVALID ACCEPTED: {}", manifestName, line);
                    }
                }
                continue;
            }

            if (line.starts_with("valid/")) {
                const auto res = tomlkit::tryParse(rawToml, parseOpts, line);
                if (!res) {
                    ++stats.validFailed;
                    if (stats.validFailed <= 20) {
                        std::println(std::cerr, "[{}] VALID REJECTED: {}\n  {}", manifestName, line, res.error().message);
                    }
                    continue;
                }

                fs::path jsonPath = tomlPath;
                jsonPath.replace_extension(u8".json");
                const std::string rawJson = readBinaryFile(jsonPath);
                const tomlkit::Item expectedDoc = tomlkit::test_adapter::taggedJsonToItem(rawJson);

                if (tomlkit::test_adapter::itemsMatchForTomlTest(*res, expectedDoc)) {
                    ++stats.validPassed;
                }
                else {
                    ++stats.validFailed;
                    if (stats.validFailed <= 20) {
                        std::println(std::cerr, "[{}] SEMANTIC MISMATCH: {}\n  expected: {}\n  actual:   {}",
                            manifestName,
                            line,
                            tomlkit::test_adapter::itemToTaggedJson(expectedDoc),
                            tomlkit::test_adapter::itemToTaggedJson(*res));
                    }
                }

                // 100% 字节级无损往返验证
                const std::string reemitted = res->toToml(fmtOpts);
                // 注意：若输入以 v1.0 模式格式化，仅在默认无损模式下比较原始字节全等
                const std::string losslessOut = res->toToml();
                if (losslessOut == rawToml) {
                    ++stats.losslessPassed;
                }
                else {
                    ++stats.losslessFailed;
                    if (stats.losslessFailed <= 20) {
                        std::println(std::cerr, "[{}] LOSSLESS MISMATCH: {}\n--- RAW ({}) ---\n{}\n--- OUT ({}) ---\n{}",
                            manifestName, line, rawToml.size(), rawToml, losslessOut.size(), losslessOut);
                    }
                }
                (void)reemitted;

                // Encoder 往返验证：`expectedDoc.toToml(fmtOpts)` -> 重新解析 -> 与 `expectedDoc` 语义全等
                const std::string encodedToml = expectedDoc.toToml(fmtOpts);
                const auto reparsedEncoded = tomlkit::tryParse(encodedToml, parseOpts, line);
                if (reparsedEncoded && tomlkit::test_adapter::itemsMatchForTomlTest(*reparsedEncoded, expectedDoc)) {
                    ++stats.encoderPassed;
                }
                else {
                    ++stats.encoderFailed;
                    if (stats.encoderFailed <= 20) {
                        std::println(std::cerr, "[{}] ENCODER ROUNDTRIP FAILED: {}\n--- ENCODED TOML ---\n{}\n--- ERROR ---\n{}",
                            manifestName,
                            line,
                            encodedToml,
                            reparsedEncoded ? "semantic mismatch" : reparsedEncoded.error().message);
                    }
                }
            }
        }
        return stats;
    }
}

int main(int argc, char** argv)
{
    fs::path testsDir;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv == nullptr || wargc < 2) {
        if (wargv != nullptr) {
            LocalFree(wargv);
        }
        std::println(std::cerr, "Usage: tomlkit-compliance-runner <path-to-toml-test/tests>");
        return 2;
    }
    testsDir = fs::path(wargv[1]);
    LocalFree(wargv);
#else
    if (argc < 2) {
        std::println(std::cerr, "Usage: tomlkit-compliance-runner <path-to-toml-test/tests>");
        return 2;
    }
    testsDir = tomlkit::utf8ToPath(argv[1]);
#endif

    int totalFailures = 0;

    for (const auto& [manifest, spec, title] : {
        std::tuple{ std::string("files-toml-1.1.0"), tomlkit::SpecVersion::v1_1, std::string_view("TOML v1.1.0") },
        std::tuple{ std::string("files-toml-1.0.0"), tomlkit::SpecVersion::v1_0, std::string_view("TOML v1.0.0") }
    })
    {
        const SuiteStats s = runManifest(testsDir, manifest, spec);
        std::println("=== {} Official Compliance Suite ===", title);
        std::println("  Valid Decoder    : {} passed, {} failed", s.validPassed, s.validFailed);
        std::println("  Invalid Reject   : {} passed, {} failed", s.invalidPassed, s.invalidFailed);
        std::println("  Byte-Lossless RT : {} passed, {} failed", s.losslessPassed, s.losslessFailed);
        std::println("  Encoder RT       : {} passed, {} failed", s.encoderPassed, s.encoderFailed);
        totalFailures += s.validFailed + s.invalidFailed + s.losslessFailed + s.encoderFailed;
    }

    return totalFailures == 0 ? 0 : 1;
}
