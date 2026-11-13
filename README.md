# tomlkitpp

**`tomlkitpp`** 是一个纯 C++23 模块化（`export module tomlkitpp;`）、零外部依赖、默认支持 **TOML v1.1.0** 且具备 **100% 字节级注释/空白/字面量样式无损保留（Style-Preserving Round-Trip）** 能力的现代 TOML 解析与编辑库。

设计定位对标 Python 的 `tomlkit` 与 Rust 的 `toml_edit`，同时提供融合 `nlohmann/json` 与 `toml11` 体验的单动态值类型（`tomlkit::Item` / `tomlkit::Document`）API。

---

## 核心特性

- **纯 C++23 模块 & 零第三方依赖**：仅依赖标准库模块 `import std;`，开箱即用，无宏污染。
- **默认支持 TOML v1.1.0**（同时向下兼容 `SpecVersion::v1_0`）：
  - 内联表允许跨多行与尾随逗号 `{ a = 1,\n b = 2, }`
  - 基础字符串支持 `\e`（ESC）与 `\xHH`（`U+00HH` 标量转义）
  - 时间与日期时间支持省略秒数（如 `07:32`、`1979-05-27T07:32Z`）
- **100% 字节级无损往返（Lossless Round-Trip）**：
  - 完整保留前置注释、行尾注释、表头与数组内部注释、缩进、空行、CRLF/LF 换行符、UTF-8 BOM。
  - 完整保留数字进制/下划线（`0xff`、`1_000_000`）、字符串引号形态（`'''...'''` / `"""..."""`）、点分键与表节物理顺序。
  - **外科手术式局部修改（Surgical Edit）**：修改单个键值或新增字段时，文件其余所有未触碰部分保证逐字节原样输出。
- **人体工学 C++23 API**：
  - 单一动态值 `tomlkit::Item`（`Document` 即根表 `Item`）
  - C++23 多维下标：`doc["plugins", "TextHook", "threads"] = 8;`
  - 链式默认回退查找：`tomlkit::findOr(doc, 4, "plugins", "threads")`（支持字符串键与数组整数下标混排）
  - 双轨错误处理：既支持抛异常 `parse` / `as<T>()`，也支持无异常 `std::expected` 风格的 `tryParse` / `tryAs<T>()`
  - 直观的注释编辑：`doc["port"].addLeadingComment(" 服务监听端口")`、`doc["port"].setInlineComment(" 默认 8080")`
  - 自定义结构体双向转换协议：`tomlkit::Serializer<T>` Concept
- **100% 通过官方 [`toml-lang/toml-test`](https://github.com/toml-lang/toml-test) 合规测试套件**：
  - **TOML v1.1.0**：Valid `218/218`、Invalid `502/502`、无损往返 `218/218`、Encoder 往返 `218/218`
  - **TOML v1.0.0**：Valid `208/208`、Invalid `509/509`、无损往返 `208/208`、Encoder 往返 `208/208`

---

## 构建要求与接入（`mcpp`）

- 构建工具：`mcpp >= 2026.10.10.1`
- 工具链：`llvm@23.1.3`（Clang 23 + MSVC STL / C++23）

在你的工程 `mcpp.toml` 中引入路径依赖：

```toml
[dependencies]
tomlkit.core = { path = "../tomlkitpp/tomlkitpp" }
```

在代码中直接导入模块：

```cpp
import std;
import tomlkitpp;
```

---

## 快速上手

### 1. 解析、读取与多维下标写入

```cpp
import std;
import tomlkitpp;

int main()
{
    // 1. 解析 TOML 文本（默认 SpecVersion::v1_1）
    tomlkit::Document doc = tomlkit::parse(R"(
# 应用配置
title = "GalTranslPP"

[server]
endpoints = [
    { host = "127.0.0.1", port = 8080, }, # v1.1 尾随逗号 + 内联表
]
wakeup = 07:30 # v1.1 省略秒数的时间
)");

    // 2. 类型读取与正交查找族（find / findOptional / tryFind / findOr）
    auto title = tomlkit::find<std::string>(doc, "title");
    auto tagsOpt = tomlkit::findOptional<std::vector<std::string>>(doc, "tags"); // 缺省返回 nullopt，类型错抛 TypeError
    auto probeOpt = tomlkit::tryFind<int>(doc, "server", "endpoints", 0, "port"); // noexcept，绝不抛异常
    int port = tomlkit::findOr(doc, "server", "endpoints", 0, "port", 80);

    // 3. 外科手术式修改与 C++23 多维下标自动建表
    doc["server", "endpoints"][0]["port"] = 9090;
    doc["server", "timeoutMs"] = 5000;
    doc["server", "timeoutMs"].addLeadingComment(" 连接超时（毫秒）");
    doc["server", "timeoutMs"].setInlineComment(" 新增配置项");

    // 4. 序列化输出（未修改的注释、缩进、内联表样式 100% 原样保留）
    std::println("{}", doc.toToml());
}
```

### 2. 无异常 `std::expected` 解析与诊断高亮

```cpp
auto res = tomlkit::tryParseFile("config.toml");
if (!res) {
    // formatDiagnostic() 输出带行列号与 ^ 指示符的可视化报错信息
    std::println(std::cerr, "{}", res.error().formatDiagnostic());
    return 1;
}
tomlkit::Document& doc = *res;
```

### 3. 自定义类型序列化（`tomlkit::Serializer<T>`）

```cpp
struct Endpoint
{
    std::string host;
    int port = 8080;
};

template<>
struct tomlkit::Serializer<Endpoint>
{
    static tomlkit::Item toToml(const Endpoint& ep)
    {
        auto tbl = tomlkit::Item::inlineTable();
        tbl["host"] = ep.host;
        tbl["port"] = ep.port;
        return tbl;
    }

    static Endpoint fromToml(const tomlkit::Item& item)
    {
        return Endpoint{
            .host = item.at("host").as<std::string>(),
            .port = item.at("port").as<int>(),
        };
    }
};

// 直接赋值与读取：
doc["primary"] = Endpoint{ .host = "localhost", .port = 9000 };
Endpoint ep = doc["primary"].as<Endpoint>();
std::vector<Endpoint> list = doc["endpoints"].as<std::vector<Endpoint>>();
```

### 4. 原子保存文件、UTF-8 路径支持与版本控制

```cpp
// 全程 UTF-8 Everywhere：支持直接传入 UTF-8 string_view / wstring_view / std::filesystem::path
// Windows 下内部统一使用宽字符路径与 pathToUtf8 / utf8ToPath，绝不调用受 ANSI 代码页污染的 path::string()
tomlkit::saveFileAtomic("D:/配置_設定/config.toml", doc);

// 若需兼容仅支持 TOML v1.0 的下游消费者，可降级导出（自动折叠多行内联表、补齐秒数等）
std::string v10Text = doc.toToml({ .spec = tomlkit::SpecVersion::v1_0 });
```

---

## 运行测试与官方合规套件

```powershell
# 运行全部单元与无损往返测试（默认 profile 为 release：O3 + LTO）
mcpp test --workspace

# 开发期快速编译测试（fast-release：O0 + 无 LTO）
mcpp test --workspace --profile fast-release

# 运行官方 toml-lang/toml-test 全量合规验证（支持 v1.1 与 v1.0）
mcpp run tomlkit-compliance-runner -- <path-to-toml-test/tests>
```

此外，`tools/toml-test-adapter` 还提供了符合官方 `toml-test` 标准输入输出协议的独立可执行程序 `tomlkit-decoder` 与 `tomlkit-encoder`，可直接配合官方 Go 测试运行器使用。

---

## 许可证

本项目基于 [Apache License 2.0](./LICENSE.txt) 开源。
