# tomlkitpp

简体中文 | [English](./README_EN.md)

`tomlkitpp` 是一个基于 C++23 模块（`export module tomlkitpp;`）的现代 TOML 解析、编辑与序列化库，默认支持 **TOML v1.1.0**，在读写过程中完整保留原始注释、空白缩进与字面量格式。

设计定位参考 Python 的 `tomlkit` 与 Rust 的 `toml_edit`，同时采用类似 `nlohmann/json` 的单值节点类型 `tomlkit::Item` 承载标量、数组与表。

---

## 特性概览

- **默认支持 TOML v1.1.0**（可通过 `SpecVersion::v1_0` 切换为 1.0 规范）：
  - 内联表支持跨多行书写、内部注释与尾随逗号
  - 基本字符串支持 `\e`（ESC）与 `\xHH` 转义
  - 时间与日期时间支持省略秒数（如 `07:32`、`1979-05-27T07:32Z`）
- **保留原始格式与注释**：
  - 完整保留前置注释、行尾注释、数组及内联表内部注释、空行、缩进、CRLF/LF 换行以及 UTF-8 BOM
  - 保留整数进制与下划线分隔（如 `0xff`、`1_000_000`）、字符串引号形态（单双引号与多行引号）、点分键及表头出现顺序
  - 修改指定键值或新增字段时仅重排受影响节点，其余未改动内容原样输出
- **单类型节点与正交查找接口**：
  - 统一使用 `tomlkit::Item` 表示任意 TOML 值，可按个人习惯起别名（如 `using toml = tomlkit::Item;`）
  - 支持 C++23 多维下标链式读写：`cfg["plugins", "TextHook", "threads"] = 8;`
  - 职责划分明确的查找函数族：`find`、`findOptional`、`tryFind`、`findOr`、`findArrayOrEmpty`、`findTableOrEmpty`
  - 同时提供抛异常接口（`parse` / `get<T>()`）与基于 `std::expected` / `std::optional` 的无异常接口（`tryParse` / `tryGet<T>()`）
  - 支持通过特化 `tomlkit::Serializer<T>` 接入自定义结构体双向转换
- **跨平台 UTF-8 路径处理**：
  - 文件读写接口直接支持 `std::filesystem::path`、UTF-8 `std::string_view`、`std::wstring_view` 与 `std::u8string_view`，在 Windows、macOS 与 Linux 上均保持一致的 UTF-8 路径行为，避免窄字符本地代码页转换导致的乱码
- **通过 [`toml-lang/toml-test`](https://github.com/toml-lang/toml-test) 官方测试集**：
  - **TOML v1.1.0**：Valid `218/218`、Invalid `502/502`、往返一致性 `218/218`、Encoder `218/218`
  - **TOML v1.0.0**：Valid `208/208`、Invalid `509/509`、往返一致性 `208/208`、Encoder `208/208`

---

## 环境要求与接入

- 语言标准：C++23（编译器与标准库需支持标准库模块 `import std;`，兼容 Windows / macOS / Linux）
- 构建工具：`mcpp >= 2026.10.10.1`（或任意支持 C++23 模块构建的系统）

在工程的 `mcpp.toml` 中通过包索引、Git 仓库或本地子目录引入（或执行 `mcpp add julixian.tomlkitpp@1.0.0`）：

```toml
[dependencies]
# 通过 mcpp-index 引入
julixian.tomlkitpp = "1.0.0"

# 通过 Git 引入
julixian.tomlkitpp = { git = "https://github.com/julixian/tomlkitpp.git", subdir = "tomlkitpp" }

# 或通过本地子模块路径引入
julixian.tomlkitpp = { path = "../3rdParty/tomlkitpp/tomlkitpp" }
```

在源码中导入模块，可配合类型别名与 JSON 库对照使用：

```cpp
import std;
import tomlkitpp;

// 可与 using json = nlohmann::json; 对照定义别名
using toml = tomlkit::Item;
```

---

## 快速上手

### 1. 解析、查找与原位修改

```cpp
import std;
import tomlkitpp;

using toml = tomlkit::Item;

int main()
{
    toml cfg = tomlkit::parse(R"(
# 应用配置
title = "GalTranslPP"

[server]
endpoints = [
    { host = "127.0.0.1", port = 8080, }, # v1.1 尾随逗号与内联表
]
wakeup = 07:30 # v1.1 省略秒数的时间
)");

    // 查找接口：支持任意层级的键名与数组下标混排
    auto title = tomlkit::find<std::string>(cfg, "title");                         // 缺失抛 OutOfRangeError，类型不符抛 TypeError
    auto tagsOpt = tomlkit::findOptional<std::vector<std::string>>(cfg, "tags");   // 缺失返回 nullopt，类型不符抛 TypeError
    auto portOpt = tomlkit::tryFind<int>(cfg, "server", "endpoints", 0, "port");   // noexcept，缺失或类型不符均返回 nullopt
    int port = tomlkit::findOr(cfg, "server", "endpoints", 0, "port", 80);         // 带默认值回退

    // 修改已有键值并追加新字段与注释（原有行尾注释、缩进与其余段落保持不变）
    cfg["server", "endpoints", 0, "port"] = 9090;
    cfg["server", "timeoutMs"] = 5000;
    cfg["server", "timeoutMs"].addLeadingComment("连接超时（毫秒）");
    cfg["server", "timeoutMs"].setInlineComment("新增配置项");

    std::println("{}", tomlkit::format(cfg));
}
```

### 2. 无异常解析与诊断输出

```cpp
auto res = tomlkit::tryParseFile("config.toml");
if (!res) {
    // 输出包含文件路径、行列号及源码位置指示的诊断文本
    std::println(std::cerr, "{}", res.error().formatDiagnostic());
    return 1;
}
toml& cfg = *res;
```

### 3. 自定义类型转换

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

// 直接赋值与按类型提取（自动支持 std::vector<Endpoint> 等标准容器组合）
cfg["primary"] = Endpoint{ .host = "localhost", .port = 9000 };
Endpoint ep = cfg["primary"].get<Endpoint>();
auto list = cfg["endpoints"].get<std::vector<Endpoint>>();
```

### 4. 原子写盘与规范版本指定

```cpp
// 先写入临时文件再原子替换目标文件，支持直接传入 UTF-8 字符串路径或 fs::path
tomlkit::saveFileAtomic("D:/项目配置/config.toml", cfg);

// 如需兼容仅支持 TOML v1.0 的下游工具，可在格式化时指定版本（自动折叠多行内联表并补齐时间秒数）
std::string v10Text = tomlkit::format(cfg, { .spec = tomlkit::SpecVersion::v1_0 });
```

---

## 构建与测试

```powershell
# 运行单元测试（默认 profile 为 release）
mcpp test --workspace

# 开发调试构建与测试（fast-release）
mcpp test --workspace --profile fast-release

# 运行官方 toml-lang/toml-test 测试集验证（同时测试 v1.1 与 v1.0）
mcpp run tomlkit-compliance-runner -- <path-to-toml-test/tests>
```

`tools/toml-test-adapter` 目录同时提供遵循官方标准输入输出协议的 `tomlkit-decoder` 与 `tomlkit-encoder` 可执行程序，可直接接入官方 `toml-test` 运行器。

---

## 许可证

本项目采用 [Apache License 2.0](./LICENSE.txt) 许可证开源。
