# tomlkitpp

[简体中文](./README.md) | English

`tomlkitpp` is a modern TOML parsing, editing, and serialization library written as a C++23 module (`export module tomlkitpp;`). It targets **TOML v1.1.0** by default and preserves comments, whitespace, indentation, and literal representations across edits.

Its design draws from Python's `tomlkit` and Rust's `toml_edit`, combined with a single dynamic value class `tomlkit::Item` inspired by `nlohmann/json`.

---

## Highlights

- **TOML v1.1.0 by Default** (with `SpecVersion::v1_0` compatibility mode):
  - Multi-line inline tables with comments and trailing commas
  - `\e` (ESC) and `\xHH` escapes in basic strings
  - Optional seconds in `LocalTime`, `LocalDateTime`, and `OffsetDateTime` (e.g. `07:32`, `1979-05-27T07:32Z`)
- **Format and Comment Preservation**:
  - Preserves leading comments, inline comments, comments inside arrays and inline tables, blank lines, indentation, CRLF/LF line endings, and UTF-8 BOM
  - Preserves integer radices and underscores (`0xff`, `1_000_000`), string quote styles, dotted keys, and section ordering
  - Updating a value or inserting a new entry only formats the affected node while emitting untouched regions verbatim
- **Single-Type Node and Orthogonal Lookup API**:
  - Represents all TOML values through `tomlkit::Item` (conveniently aliased as `using toml = tomlkit::Item;` alongside `using json = nlohmann::json;`)
  - C++23 multidimensional subscripting: `cfg["plugins", "TextHook", "threads"] = 8;`
  - Clear lookup functions with distinct error contracts: `find`, `findOptional`, `tryFind`, `findOr`, `findArrayOrEmpty`, and `findTableOrEmpty`
  - Both throwing (`parse` / `get<T>()`) and non-throwing (`tryParse` / `tryGet<T>()`) APIs
  - Custom struct conversion via `tomlkit::Serializer<T>` specialization
- **Consistent Cross-Platform UTF-8 File Paths**:
  - File I/O accepts `std::filesystem::path`, UTF-8 `std::string_view`, `std::wstring_view`, and `std::u8string_view`, maintaining UTF-8 path semantics uniformly across Windows, macOS, and Linux without locale-dependent narrow codepage conversions
- **100% Pass Rate on Official [`toml-lang/toml-test`](https://github.com/toml-lang/toml-test)**:
  - **TOML v1.1.0**: Valid `218/218`, Invalid `502/502`, Round-trip `218/218`, Encoder `218/218`
  - **TOML v1.0.0**: Valid `208/208`, Invalid `509/509`, Round-trip `208/208`, Encoder `208/208`

---

## Requirements & Integration

- C++ Standard: C++23 (compiler and standard library with `import std;` support across Windows, macOS, and Linux)
- Build System: `mcpp >= 2026.10.10.1` (or any build system supporting C++23 modules)

Add `tomlkit.core` to `[dependencies]` in `mcpp.toml`:

```toml
[dependencies]
# Via Git repository
tomlkit.core = { git = "https://github.com/julixian/tomlkitpp.git", subdir = "tomlkitpp" }

# Or via local submodule path
tomlkit.core = { path = "../3rdParty/tomlkitpp/tomlkitpp" }
```

Import the module in source files:

```cpp
import std;
import tomlkitpp;

// Pairs naturally with `using json = nlohmann::json;`
using toml = tomlkit::Item;
```

---

## Quick Start

### 1. Parsing, Lookup, and In-Place Editing

```cpp
import std;
import tomlkitpp;

using toml = tomlkit::Item;

int main()
{
    toml cfg = tomlkit::parse(R"(
# Application settings
title = "GalTranslPP"

[server]
endpoints = [
    { host = "127.0.0.1", port = 8080, }, # v1.1 trailing comma in inline table
]
wakeup = 07:30 # v1.1 time without seconds
)");

    // Orthogonal path lookups mixing string keys and integer array indices
    auto title = tomlkit::find<std::string>(cfg, "title");                         // throws OutOfRangeError if missing, TypeError on type mismatch
    auto tagsOpt = tomlkit::findOptional<std::vector<std::string>>(cfg, "tags");   // returns nullopt if missing, throws TypeError on type mismatch
    auto portOpt = tomlkit::tryFind<int>(cfg, "server", "endpoints", 0, "port");   // noexcept, returns nullopt if missing or mismatched
    int port = tomlkit::findOr(cfg, "server", "endpoints", 0, "port", 80);         // returns fallback value if missing or mismatched

    // Update existing values and insert new entries while keeping surrounding comments intact
    cfg["server", "endpoints", 0, "port"] = 9090;
    cfg["server", "timeoutMs"] = 5000;
    cfg["server", "timeoutMs"].addLeadingComment("Connection timeout in milliseconds");
    cfg["server", "timeoutMs"].setInlineComment("newly added");

    std::println("{}", tomlkit::format(cfg));
}
```

### 2. Non-Throwing Parsing and Diagnostics

```cpp
auto res = tomlkit::tryParseFile("config.toml");
if (!res) {
    // Prints file path, line/column numbers, and caret indicator
    std::println(std::cerr, "{}", res.error().formatDiagnostic());
    return 1;
}
toml& cfg = *res;
```

### 3. Custom Type Serialization

```cpp
struct Endpoint
{
    std::string host;
    int port = 8080;
};

template<>
struct tomlkit::Serializer<Endpoint>
{
    static void toToml(const Endpoint& ep, tomlkit::Item& out)
    {
        out = tomlkit::Item::inlineTable({
            { "host", ep.host },
            { "port", ep.port }
        });
    }

    static Endpoint fromToml(const tomlkit::Item& item)
    {
        return Endpoint{
            .host = item.at("host").get<std::string>(),
            .port = item.valueOr("port", 8080)
        };
    }
};

// Works directly with assignment, get<T>(), and standard containers such as std::vector<Endpoint>
cfg["primary"] = Endpoint{ .host = "localhost", .port = 9000 };
Endpoint ep = cfg["primary"].get<Endpoint>();
auto list = cfg["endpoints"].get<std::vector<Endpoint>>();
```

### 4. Atomic File Saving and Spec Selection

```cpp
// Writes to a temporary file first and atomically renames onto the target path
tomlkit::saveFileAtomic("D:/Configs/config.toml", cfg);

// Emit TOML v1.0-compatible output if needed by older downstream tools
std::string v10Text = tomlkit::format(cfg, { .spec = tomlkit::SpecVersion::v1_0 });
```

---

## Building & Testing

```powershell
# Run unit tests (default profile: release)
mcpp test --workspace

# Fast unoptimized build and test cycle
mcpp test --workspace --profile fast-release

# Run the official toml-lang/toml-test suite (validates both v1.1 and v1.0)
mcpp run tomlkit-compliance-runner -- <path-to-toml-test/tests>
```

Standalone `tomlkit-decoder` and `tomlkit-encoder` binaries implementing the standard `toml-test` stdin/stdout JSON protocol are also provided under `tools/toml-test-adapter`.

---

## License

Licensed under the [Apache License 2.0](./LICENSE.txt).
