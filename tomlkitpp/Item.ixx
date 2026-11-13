module;

#include "TKPMacros.hpp"

export module tomlkitpp:Item;

export import std;
export import :Spec;
export import :Error;
export import :Types;
export import :Decor;

export NAMESPACE_BEGIN(tomlkit)

class Item;

template<typename T>
struct Serializer;

struct ArrayElement
{
    Item* pValue() noexcept;
    const Item* pValue() const noexcept;

    std::unique_ptr<Item> value;
    std::string beforeComma;
    std::string afterComma;
    bool hasComma = false;

    ArrayElement();
    explicit ArrayElement(Item val);
    ArrayElement(const ArrayElement& other);
    ArrayElement(ArrayElement&&) noexcept;
    ArrayElement& operator=(const ArrayElement& other);
    ArrayElement& operator=(ArrayElement&&) noexcept;
    ~ArrayElement();
};

struct ArrayStorage
{
    ArrayStyle style = ArrayStyle::Standard;
    bool multiline = false;
    std::vector<ArrayElement> elements;
    std::string trailing;

    ArrayStorage();
    ArrayStorage(const ArrayStorage&);
    ArrayStorage(ArrayStorage&&) noexcept;
    ArrayStorage& operator=(const ArrayStorage&);
    ArrayStorage& operator=(ArrayStorage&&) noexcept;
    ~ArrayStorage();
};

struct TableEntry
{
    Key key;
    std::unique_ptr<Item> value;
    uint32_t entryOrder = 0;

    // 仅在 `TableStyle::Inline` 下用于保留逗号前后的空白与行内注释
    std::string beforeComma;
    std::string afterComma;
    bool hasComma = false;

    TableEntry(Key k, Item v, uint32_t order = 0);
    TableEntry(const TableEntry& other);
    TableEntry(TableEntry&& other) noexcept;
    TableEntry& operator=(const TableEntry& other);
    TableEntry& operator=(TableEntry&& other) noexcept;
    ~TableEntry();

    void relinkOwner() noexcept;
};

struct TableStorage
{
    static constexpr size_t kHashIndexThreshold = 16;

    TableStyle style = TableStyle::Standard;
    bool hasBom = false;
    bool multilineInline = false;

    // 解析期状态位（遵循 TOML v1.1 #859 表与点分键重定义校验规则）
    bool isSealed = false;
    bool isExplicitlyDefined = false;
    uint32_t dottedKeyScopeId = 0;

    // 文档级 `[header]` 出现序号（0 表示程序动态新建，序列化时紧随父表输出）
    uint32_t sectionOrder = 0;
    uint32_t nextEntryOrder = 1;

    Decor headerDecor{};
    std::vector<KeySegment> headerKeyPath;

    std::vector<std::unique_ptr<TableEntry>> entries;
    std::string trailing;

    // 开放寻址透明哈希索引槽（0 = 空槽，否则存 `entryIndex + 1`）
    mutable std::vector<size_t> hashSlots;
    mutable bool hashDirty = true;

    TableStorage();
    explicit TableStorage(TableStyle tblStyle);
    TableStorage(const TableStorage& other);
    TableStorage(TableStorage&&) noexcept;
    TableStorage& operator=(const TableStorage& other);
    TableStorage& operator=(TableStorage&&) noexcept;
    ~TableStorage();

    size_t findIndex(std::string_view keyName) const noexcept;
    void invalidateIndex() noexcept { hashDirty = true; }
    void rebuildIndexIfNeeded() const;
};

// 数组元素解引用迭代器，支持 `for (auto& item : arrItem)` 与 `std::ranges`
template<bool IsConst>
class ArrayIterator
{
public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;
    using value_type = Item;
    using difference_type = ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const Item*, Item*>;
    using reference = std::conditional_t<IsConst, const Item&, Item&>;
    using ElemIter = std::conditional_t<
        IsConst,
        std::vector<ArrayElement>::const_iterator,
        std::vector<ArrayElement>::iterator
    >;

    ArrayIterator() = default;
    explicit ArrayIterator(ElemIter it) noexcept
        : m_it(it)
    { }

    template<bool WasConst>
        requires (IsConst && !WasConst)
    ArrayIterator(const ArrayIterator<WasConst>& other) noexcept
        : m_it(other.rawIter())
    { }

    ElemIter rawIter() const noexcept { return m_it; }

    reference operator*() const noexcept { return *m_it->value; }
    pointer operator->() const noexcept { return m_it->value.get(); }
    reference operator[](difference_type n) const noexcept { return *m_it[n].value; }

    ArrayIterator& operator++() noexcept { ++m_it; return *this; }
    ArrayIterator operator++(int) noexcept { auto tmp = *this; ++m_it; return tmp; }
    ArrayIterator& operator--() noexcept { --m_it; return *this; }
    ArrayIterator operator--(int) noexcept { auto tmp = *this; --m_it; return tmp; }

    ArrayIterator& operator+=(difference_type n) noexcept { m_it += n; return *this; }
    ArrayIterator& operator-=(difference_type n) noexcept { m_it -= n; return *this; }

    friend ArrayIterator operator+(ArrayIterator it, difference_type n) noexcept { it += n; return it; }
    friend ArrayIterator operator+(difference_type n, ArrayIterator it) noexcept { it += n; return it; }
    friend ArrayIterator operator-(ArrayIterator it, difference_type n) noexcept { it -= n; return it; }
    friend difference_type operator-(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it - b.m_it; }

    friend bool operator==(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it == b.m_it; }
    friend auto operator<=>(const ArrayIterator& a, const ArrayIterator& b) noexcept { return a.m_it <=> b.m_it; }

private:
    ElemIter m_it{};
};

// 表键值对视图与迭代器，支持 `for (auto [key, val] : tbl.tableItems())` 结构化绑定
template<bool IsConst>
struct TableKeyValuePair
{
    const Key& key;
    std::conditional_t<IsConst, const Item&, Item&> value;
};

template<bool IsConst>
class TableIterator
{
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;
    using value_type = TableKeyValuePair<IsConst>;
    using difference_type = ptrdiff_t;
    using EntryIter = std::conditional_t<
        IsConst,
        std::vector<std::unique_ptr<TableEntry>>::const_iterator,
        std::vector<std::unique_ptr<TableEntry>>::iterator
    >;

    TableIterator() = default;
    explicit TableIterator(EntryIter it) noexcept
        : m_it(it)
    { }

    value_type operator*() const noexcept
    {
        return value_type{ (*m_it)->key, *(*m_it)->value };
    }

    TableIterator& operator++() noexcept { ++m_it; return *this; }
    TableIterator operator++(int) noexcept { auto tmp = *this; ++m_it; return tmp; }
    TableIterator& operator--() noexcept { --m_it; return *this; }
    TableIterator operator--(int) noexcept { auto tmp = *this; --m_it; return tmp; }

    friend bool operator==(const TableIterator& a, const TableIterator& b) noexcept { return a.m_it == b.m_it; }

private:
    EntryIter m_it{};
};

template<bool IsConst>
class TableItemsRange
{
public:
    using EntryVec = std::conditional_t<
        IsConst,
        const std::vector<std::unique_ptr<TableEntry>>,
        std::vector<std::unique_ptr<TableEntry>>
    >;

    explicit TableItemsRange(EntryVec* pEntries) noexcept
        : m_pEntries(pEntries)
    { }

    TableIterator<IsConst> begin() const noexcept
    {
        return m_pEntries ? TableIterator<IsConst>(m_pEntries->begin()) : TableIterator<IsConst>{};
    }

    TableIterator<IsConst> end() const noexcept
    {
        return m_pEntries ? TableIterator<IsConst>(m_pEntries->end()) : TableIterator<IsConst>{};
    }

    size_t size() const noexcept { return m_pEntries ? m_pEntries->size() : 0; }
    bool empty() const noexcept { return size() == 0; }

private:
    EntryVec* m_pEntries = nullptr;
};

template<typename T>
concept PathKeyLike = std::convertible_to<const T&, std::string_view>;

template<typename T>
concept PathIndexLike = std::integral<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool>;

template<typename T>
concept PathStep = PathKeyLike<T> || PathIndexLike<T>;

template<typename T>
concept CustomTomlInto = requires(const T& v, Item& out) {
    Serializer<std::remove_cvref_t<T>>::toToml(v, out);
};

template<typename T>
concept CustomTomlFrom = requires(const Item& item) {
    { Serializer<std::remove_cvref_t<T>>::fromToml(item) } -> std::same_as<std::remove_cvref_t<T>>;
};

// 单一动态值核心类：同时承载标量、数组、内联表、标准表与根文档，并完整保留 CST 格式装饰
class Item
{
public:
    using iterator = ArrayIterator<false>;
    using const_iterator = ArrayIterator<true>;

    // ==================== 构造与静态工厂 ====================
    Item() noexcept;
    Item(const Item& other);
    Item(Item&& other) noexcept;
    Item& operator=(const Item& other);
    Item& operator=(Item&& other) noexcept;
    ~Item();

    Item(bool val) noexcept;

    template<typename T>
        requires (std::integral<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool> && !std::same_as<std::remove_cvref_t<T>, char>)
    Item(T val)
    {
        if constexpr (std::unsigned_integral<std::remove_cvref_t<T>>) {
            if (val > (std::remove_cvref_t<T>)std::numeric_limits<int64_t>::max()) {
                throw OutOfRangeError("Unsigned integer exceeds TOML 64-bit signed integer maximum");
            }
        }
        m_payload = (int64_t)val;
    }

    template<std::floating_point T>
    Item(T val) noexcept
        : m_payload((double)val)
    { }

    Item(const char* str);
    Item(std::string_view sv);
    Item(std::string str);

    Item(LocalDate val) noexcept;
    Item(LocalTime val) noexcept;
    Item(LocalDateTime val) noexcept;
    Item(OffsetDateTime val) noexcept;

    // 列表初始化默认构造为 Array（避免 `{ "name", 1 }` 被静默误判为对象）
    Item(std::initializer_list<Item> arrayElements);

    template<typename T>
        requires (
            CustomTomlInto<T>
            && !std::same_as<std::remove_cvref_t<T>, Item>
        )
    Item(const T& customVal)
    {
        Serializer<std::remove_cvref_t<T>>::toToml(customVal, *this);
    }

    static Item document();
    static Item array(std::initializer_list<Item> init = {});
    static Item arrayOfTables();
    static Item table(TableStyle style = TableStyle::Standard);
    static Item table(std::initializer_list<std::pair<std::string_view, Item>> init, TableStyle style = TableStyle::Standard);
    static Item inlineTable(std::initializer_list<std::pair<std::string_view, Item>> init = {});

    // 赋值重载：当左侧节点原本持有格式装饰（如行尾注释与缩进）而右侧无装饰时，自动继承左侧装饰
    template<typename T>
        requires (!std::same_as<std::remove_cvref_t<T>, Item> && std::constructible_from<Item, T>)
    Item& operator=(T&& val)
    {
        assignPreservingDecor(Item(std::forward<T>(val)));
        return *this;
    }

    Item& operator=(std::initializer_list<Item> arrayElements)
    {
        assignPreservingDecor(Item(arrayElements));
        return *this;
    }

    // ==================== 类型判别 ====================
    ItemKind kind() const noexcept;
    std::string_view kindName() const noexcept { return itemKindName(kind()); }

    bool isEmpty() const noexcept { return kind() == ItemKind::Empty; }
    bool isBoolean() const noexcept { return kind() == ItemKind::Boolean; }
    bool isInteger() const noexcept { return kind() == ItemKind::Integer; }
    bool isFloat() const noexcept { return kind() == ItemKind::Float; }
    bool isNumber() const noexcept { return isInteger() || isFloat(); }
    bool isString() const noexcept { return kind() == ItemKind::String; }
    bool isLocalDate() const noexcept { return kind() == ItemKind::LocalDate; }
    bool isLocalTime() const noexcept { return kind() == ItemKind::LocalTime; }
    bool isLocalDateTime() const noexcept { return kind() == ItemKind::LocalDateTime; }
    bool isOffsetDateTime() const noexcept { return kind() == ItemKind::OffsetDateTime; }
    bool isDateTime() const noexcept
    {
        return isLocalDate() || isLocalTime() || isLocalDateTime() || isOffsetDateTime();
    }
    bool isArray() const noexcept { return kind() == ItemKind::Array; }
    bool isTable() const noexcept { return kind() == ItemKind::Table; }
    bool isInlineTable() const noexcept;
    bool isArrayOfTables() const noexcept;

    // ==================== 强类型引用访问（C++23 Deducing this） ====================
    template<typename Self>
    auto&& asBoolean(this Self&& self)
    {
        self.ensureKind(ItemKind::Boolean);
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.m_rawRepr.clear();
        }
        return std::get<bool>(std::forward<Self>(self).m_payload);
    }

    template<typename Self>
    auto&& asInteger(this Self&& self)
    {
        self.ensureKind(ItemKind::Integer);
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.m_rawRepr.clear();
        }
        return std::get<int64_t>(std::forward<Self>(self).m_payload);
    }

    template<typename Self>
    auto&& asFloat(this Self&& self)
    {
        self.ensureKind(ItemKind::Float);
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.m_rawRepr.clear();
        }
        return std::get<double>(std::forward<Self>(self).m_payload);
    }

    template<typename Self>
    auto&& asString(this Self&& self)
    {
        self.ensureKind(ItemKind::String);
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.m_rawRepr.clear();
        }
        return std::get<std::string>(std::forward<Self>(self).m_payload);
    }

    template<typename Self>
    auto&& asLocalDate(this Self&& self)
    {
        self.ensureKind(ItemKind::LocalDate);
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.m_rawRepr.clear();
        }
        return std::get<LocalDate>(std::forward<Self>(self).m_payload);
    }

    template<typename Self>
    auto&& asLocalTime(this Self&& self)
    {
        self.ensureKind(ItemKind::LocalTime);
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.m_rawRepr.clear();
        }
        return std::get<LocalTime>(std::forward<Self>(self).m_payload);
    }

    template<typename Self>
    auto&& asLocalDateTime(this Self&& self)
    {
        self.ensureKind(ItemKind::LocalDateTime);
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.m_rawRepr.clear();
        }
        return std::get<LocalDateTime>(std::forward<Self>(self).m_payload);
    }

    template<typename Self>
    auto&& asOffsetDateTime(this Self&& self)
    {
        self.ensureKind(ItemKind::OffsetDateTime);
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.m_rawRepr.clear();
        }
        return std::get<OffsetDateTime>(std::forward<Self>(self).m_payload);
    }

    double asNumber() const;

    // ==================== 泛型值提取 `get<T>` / `tryGet<T>` / `getTo` ====================
    template<typename T>
    std::optional<T> tryGet() const
    {
        using CleanT = std::remove_cvref_t<T>;
        if constexpr (std::same_as<CleanT, Item>) {
            return *this;
        }
        else if constexpr (std::same_as<CleanT, bool>) {
            if (isBoolean()) {
                return std::get<bool>(m_payload);
            }
            return std::nullopt;
        }
        else if constexpr (std::integral<CleanT> && !std::same_as<CleanT, char>) {
            if (!isInteger()) {
                return std::nullopt;
            }
            const int64_t v = std::get<int64_t>(m_payload);
            if constexpr (std::signed_integral<CleanT>) {
                if (v < (int64_t)std::numeric_limits<CleanT>::min() || v > (int64_t)std::numeric_limits<CleanT>::max()) {
                    return std::nullopt;
                }
                return (CleanT)v;
            }
            else {
                if (v < 0 || (uint64_t)v > (uint64_t)std::numeric_limits<CleanT>::max()) {
                    return std::nullopt;
                }
                return (CleanT)v;
            }
        }
        else if constexpr (std::floating_point<CleanT>) {
            if (isFloat()) {
                return (CleanT)std::get<double>(m_payload);
            }
            return std::nullopt;
        }
        else if constexpr (std::same_as<CleanT, std::string>) {
            if (isString()) {
                return std::get<std::string>(m_payload);
            }
            return std::nullopt;
        }
        else if constexpr (std::same_as<CleanT, std::string_view>) {
            if (isString()) {
                return std::string_view(std::get<std::string>(m_payload));
            }
            return std::nullopt;
        }
        else if constexpr (std::same_as<CleanT, LocalDate>) {
            return isLocalDate() ? std::optional<CleanT>(std::get<LocalDate>(m_payload)) : std::nullopt;
        }
        else if constexpr (std::same_as<CleanT, LocalTime>) {
            return isLocalTime() ? std::optional<CleanT>(std::get<LocalTime>(m_payload)) : std::nullopt;
        }
        else if constexpr (std::same_as<CleanT, LocalDateTime>) {
            return isLocalDateTime() ? std::optional<CleanT>(std::get<LocalDateTime>(m_payload)) : std::nullopt;
        }
        else if constexpr (std::same_as<CleanT, OffsetDateTime>) {
            return isOffsetDateTime() ? std::optional<CleanT>(std::get<OffsetDateTime>(m_payload)) : std::nullopt;
        }
        else if constexpr (CustomTomlFrom<CleanT>) {
            try {
                return Serializer<CleanT>::fromToml(*this);
            }
            catch (...) {
                return std::nullopt;
            }
        }
        else if constexpr (requires { typename CleanT::value_type; CleanT{}.push_back(std::declval<typename CleanT::value_type>()); }) {
            if (!isArray()) {
                return std::nullopt;
            }
            CleanT out;
            for (const Item& elem : *this) {
                auto elemOpt = elem.tryGet<typename CleanT::value_type>();
                if (!elemOpt) {
                    return std::nullopt;
                }
                out.push_back(std::move(*elemOpt));
            }
            return out;
        }
        else if constexpr (requires { typename CleanT::key_type; typename CleanT::mapped_type; }
            && std::constructible_from<typename CleanT::key_type, std::string>)
        {
            if (!isTable()) {
                return std::nullopt;
            }
            CleanT out;
            for (const auto [k, v] : tableItems()) {
                auto valOpt = v.tryGet<typename CleanT::mapped_type>();
                if (!valOpt) {
                    return std::nullopt;
                }
                out.emplace(typename CleanT::key_type(k.name()), std::move(*valOpt));
            }
            return out;
        }
        else {
            static_assert(false, "TKP_UNSUPPORTED_GET_TYPE");
        }
    }

    template<typename T>
    T get() const
    {
        if (auto opt = tryGet<T>()) {
            return std::move(*opt);
        }
        throw TypeError(std::format("Cannot convert TOML item of kind '{}' to requested C++ type", kindName()));
    }

    template<typename T>
    void getTo(T& dst) const
    {
        dst = get<T>();
    }

    // ==================== 容器容量、查找、下标与修改 ====================
    size_t size() const noexcept;
    bool empty() const noexcept;
    void clear();

    bool contains(std::string_view key) const noexcept;

    Item* find(std::string_view key) noexcept;
    const Item* find(std::string_view key) const noexcept;

    Item* find(size_t index) noexcept;
    const Item* find(size_t index) const noexcept;

    template<typename Self, PathStep First, PathStep Second, PathStep... Rest>
    auto* find(this Self&& self, First&& first, Second&& second, Rest&&... rest) noexcept
    {
        auto* pNext = self.stepFind(std::forward<First>(first));
        using RetPtr = std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const Item*, Item*>;
        if (pNext == nullptr) {
            return (RetPtr)nullptr;
        }
        if constexpr (sizeof...(Rest) == 0) {
            return pNext->stepFind(std::forward<Second>(second));
        }
        else {
            return pNext->find(std::forward<Second>(second), std::forward<Rest>(rest)...);
        }
    }

    Item& at(std::string_view key);
    const Item& at(std::string_view key) const;

    Item& at(size_t index);
    const Item& at(size_t index) const;

    template<typename Self, PathStep First, PathStep Second, PathStep... Rest>
    auto&& at(this Self&& self, First&& first, Second&& second, Rest&&... rest)
    {
        auto&& next = self.stepAt(std::forward<First>(first));
        if constexpr (sizeof...(Rest) == 0) {
            return next.stepAt(std::forward<Second>(second));
        }
        else {
            return next.at(std::forward<Second>(second), std::forward<Rest>(rest)...);
        }
    }

    // 单维下标：可变表键（不存在时自动建项）、只读表键（等同 `at`）与数组索引
    Item& operator[](std::string_view key);
    Item& operator[](const char* key) { return (*this)[std::string_view(key)]; }
    Item& operator[](const std::string& key) { return (*this)[std::string_view(key)]; }

    const Item& operator[](std::string_view key) const { return at(key); }
    const Item& operator[](const char* key) const { return at(std::string_view(key)); }
    const Item& operator[](const std::string& key) const { return at(std::string_view(key)); }

    template<PathIndexLike Idx>
    Item& operator[](Idx index)
    {
        if constexpr (std::signed_integral<std::remove_cvref_t<Idx>>) {
            if (index < 0) {
                throw OutOfRangeError(std::format("Negative array index {}", index));
            }
        }
        return at((size_t)index);
    }

    template<PathIndexLike Idx>
    const Item& operator[](Idx index) const
    {
        if constexpr (std::signed_integral<std::remove_cvref_t<Idx>>) {
            if (index < 0) {
                throw OutOfRangeError(std::format("Negative array index {}", index));
            }
        }
        return at((size_t)index);
    }

    // C++23 多维下标：`doc["workspace", "profile", "release", "opt"] = 3;`
    template<PathStep First, PathStep Second, PathStep... Rest>
    Item& operator[](First&& first, Second&& second, Rest&&... rest)
    {
        Item& next = stepSubscriptMutable(std::forward<First>(first));
        if constexpr (sizeof...(Rest) == 0) {
            return next.stepSubscriptMutable(std::forward<Second>(second));
        }
        else {
            return next[std::forward<Second>(second), std::forward<Rest>(rest)...];
        }
    }

    template<PathStep First, PathStep Second, PathStep... Rest>
    const Item& operator[](First&& first, Second&& second, Rest&&... rest) const
    {
        return at(std::forward<First>(first), std::forward<Second>(second), std::forward<Rest>(rest)...);
    }

    template<typename T>
    auto valueOr(std::string_view key, T&& fallback) const
    {
        using OutT = std::conditional_t<
            std::convertible_to<T, std::string_view> && !std::same_as<std::remove_cvref_t<T>, std::string_view>,
            std::string,
            std::remove_cvref_t<T>
        >;
        if (const Item* pFound = find(key)) {
            if (auto opt = pFound->tryGet<OutT>()) {
                return std::move(*opt);
            }
        }
        return OutT(std::forward<T>(fallback));
    }

    // 数组增删与表增删
    void push_back(const Item& elem);
    void push_back(Item&& elem);

    template<typename... Args>
    Item& emplace_back(Args&&... args)
    {
        push_back(Item(std::forward<Args>(args)...));
        return back();
    }

    Item& front();
    const Item& front() const;
    Item& back();
    const Item& back() const;

    bool erase(std::string_view key);
    void erase(size_t index);

    std::pair<Item&, bool> insertOrAssign(std::string_view key, Item val);

    // 迭代器接口（Array 直接遍历元素，Table 通过 `tableItems()` 遍历键值对）
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    TableItemsRange<false> tableItems();
    TableItemsRange<true> tableItems() const;

    // ==================== 注释、键元数据与格式样式 API ====================
    Decor& decor() noexcept { return m_decor; }
    const Decor& decor() const noexcept { return m_decor; }

    std::optional<std::string> inlineComment() const;
    Item& setInlineComment(std::string_view comment);
    Item& clearInlineComment();

    std::vector<std::string> leadingComments() const;
    Item& addLeadingComment(std::string_view comment, std::string_view newline = "\n");
    Item& clearLeadingComments();

    Key& keyAt(std::string_view key);
    const Key& keyAt(std::string_view key) const;

    StringStyle stringStyle() const noexcept { return m_stringStyle; }
    Item& setStringStyle(StringStyle style) noexcept;

    IntegerRadix integerRadix() const noexcept { return m_intRadix; }
    Item& setIntegerRadix(IntegerRadix radix) noexcept;

    TableStyle tableStyle() const noexcept;
    Item& setTableStyle(TableStyle style);

    ArrayStyle arrayStyle() const noexcept;
    Item& setArrayStyle(ArrayStyle style);

    Item& setMultilineArray(bool multiline);
    Item& setMultilineInlineTable(bool multiline);

    // 序列化快捷方法（在 Emitter.cpp 中实现）
    std::string toToml(FormatOptions options = {}) const;
    std::string dump(FormatOptions options = {}) const { return toToml(options); }

    // 语义相等比较（忽略注释、缩进与进制等装饰差异，比较 TOML 语义值）
    friend bool operator==(const Item& lhs, const Item& rhs) noexcept;

    // 供内部 Parser / Emitter 访问底层 CST 存储
    const std::string& rawRepr() const noexcept { return m_rawRepr; }
    void setRawRepr(std::string raw) { m_rawRepr = std::move(raw); }
    void clearRawRepr() noexcept { m_rawRepr.clear(); }

    ArrayStorage* arrayStorage() noexcept;
    const ArrayStorage* arrayStorage() const noexcept;

    TableStorage* tableStorage() noexcept;
    const TableStorage* tableStorage() const noexcept;

    void setOwningKey(Key* pKey) noexcept { m_pOwningKey = pKey; }
    Key* owningKey() noexcept { return m_pOwningKey; }
    const Key* owningKey() const noexcept { return m_pOwningKey; }

private:
    using Payload = std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        std::string,
        LocalDate,
        LocalTime,
        LocalDateTime,
        OffsetDateTime,
        std::unique_ptr<ArrayStorage>,
        std::unique_ptr<TableStorage>
    >;

    void ensureKind(ItemKind expected) const;
    void assignPreservingDecor(Item&& replacement);

    template<typename Step>
    Item* stepFind(Step&& s) noexcept
    {
        if constexpr (PathKeyLike<Step>) {
            return find(std::string_view(s));
        }
        else {
            if constexpr (std::signed_integral<std::remove_cvref_t<Step>>) {
                if (s < 0) {
                    return nullptr;
                }
            }
            return find((size_t)s);
        }
    }

    template<typename Step>
    const Item* stepFind(Step&& s) const noexcept
    {
        if constexpr (PathKeyLike<Step>) {
            return find(std::string_view(s));
        }
        else {
            if constexpr (std::signed_integral<std::remove_cvref_t<Step>>) {
                if (s < 0) {
                    return nullptr;
                }
            }
            return find((size_t)s);
        }
    }

    template<typename Step>
    Item& stepAt(Step&& s)
    {
        if constexpr (PathKeyLike<Step>) {
            return at(std::string_view(s));
        }
        else {
            if constexpr (std::signed_integral<std::remove_cvref_t<Step>>) {
                if (s < 0) {
                    throw OutOfRangeError(std::format("Negative array index {}", s));
                }
            }
            return at((size_t)s);
        }
    }

    template<typename Step>
    const Item& stepAt(Step&& s) const
    {
        if constexpr (PathKeyLike<Step>) {
            return at(std::string_view(s));
        }
        else {
            if constexpr (std::signed_integral<std::remove_cvref_t<Step>>) {
                if (s < 0) {
                    throw OutOfRangeError(std::format("Negative array index {}", s));
                }
            }
            return at((size_t)s);
        }
    }

    template<typename Step>
    Item& stepSubscriptMutable(Step&& s)
    {
        if constexpr (PathKeyLike<Step>) {
            return (*this)[std::string_view(s)];
        }
        else {
            if constexpr (std::signed_integral<std::remove_cvref_t<Step>>) {
                if (s < 0) {
                    throw OutOfRangeError(std::format("Negative array index {}", s));
                }
            }
            return at((size_t)s);
        }
    }

    Payload m_payload{};
    Decor m_decor{};
    std::string m_rawRepr;
    StringStyle m_stringStyle = StringStyle::Basic;
    IntegerRadix m_intRadix = IntegerRadix::Decimal;
    Key* m_pOwningKey = nullptr;
};

namespace detail
{
    template<typename T>
    struct IsStdOptionalTrait : std::false_type { };

    template<typename U>
    struct IsStdOptionalTrait<std::optional<U>> : std::true_type { };

    template<typename T>
    constexpr bool IsStdOptional = IsStdOptionalTrait<std::remove_cvref_t<T>>::value;

    template<typename FallbackT>
    using NormalizedFallbackType = std::conditional_t<
        std::convertible_to<FallbackT, std::string_view> && !std::same_as<std::remove_cvref_t<FallbackT>, std::string_view>,
        std::string,
        std::remove_cvref_t<FallbackT>
    >;

    template<typename Step>
    const Item* findChildNoexcept(const Item& current, Step&& step) noexcept
    {
        if constexpr (PathKeyLike<Step>) {
            return current.find(std::string_view(step));
        }
        else {
            if constexpr (std::signed_integral<std::remove_cvref_t<Step>>) {
                if (step < 0) {
                    return nullptr;
                }
            }
            return current.find((size_t)step);
        }
    }

    template<PathStep LastStep>
    const Item* walkPathNoexcept(const Item& current, LastStep&& last) noexcept
    {
        return findChildNoexcept(current, std::forward<LastStep>(last));
    }

    template<PathStep FirstStep, PathStep SecondStep, PathStep... RestSteps>
    const Item* walkPathNoexcept(const Item& current, FirstStep&& first, SecondStep&& second, RestSteps&&... rest) noexcept
    {
        const Item* pNext = findChildNoexcept(current, std::forward<FirstStep>(first));
        if (pNext == nullptr) {
            return nullptr;
        }
        return walkPathNoexcept(*pNext, std::forward<SecondStep>(second), std::forward<RestSteps>(rest)...);
    }

    // 严格路径探测（供 `findOptional<T>` 使用）：缺失键/数组越界返回 nullptr，但若中间节点存在且非对应容器类型则抛 TypeError
    template<typename Step>
    const Item* findChildStrictContainer(const Item& current, Step&& step)
    {
        if constexpr (PathKeyLike<Step>) {
            const std::string_view keySv(step);
            if (!current.isTable()) {
                throw TypeError(std::format("Cannot lookup key '{}' on non-table TOML item of kind '{}'", keySv, current.kindName()));
            }
            return current.find(keySv);
        }
        else {
            if (!current.isArray()) {
                throw TypeError(std::format("Cannot index on non-array TOML item of kind '{}'", current.kindName()));
            }
            if constexpr (std::signed_integral<std::remove_cvref_t<Step>>) {
                if (step < 0) {
                    return nullptr;
                }
            }
            return current.find((size_t)step);
        }
    }

    template<PathStep LastStep>
    const Item* walkPathStrictContainer(const Item& current, LastStep&& last)
    {
        return findChildStrictContainer(current, std::forward<LastStep>(last));
    }

    template<PathStep FirstStep, PathStep SecondStep, PathStep... RestSteps>
    const Item* walkPathStrictContainer(const Item& current, FirstStep&& first, SecondStep&& second, RestSteps&&... rest)
    {
        const Item* pNext = findChildStrictContainer(current, std::forward<FirstStep>(first));
        if (pNext == nullptr) {
            return nullptr;
        }
        return walkPathStrictContainer(*pNext, std::forward<SecondStep>(second), std::forward<RestSteps>(rest)...);
    }

    template<typename... Args>
    struct FindOrHelper;

    template<PathStep LastStep, typename FallbackT>
    struct FindOrHelper<LastStep, FallbackT>
    {
        static auto eval(const Item& current, LastStep&& step, FallbackT&& fallback)
            -> NormalizedFallbackType<FallbackT>
        {
            using OutT = NormalizedFallbackType<FallbackT>;
            const Item* pFound = findChildNoexcept(current, std::forward<LastStep>(step));
            if (pFound != nullptr) {
                if (auto opt = pFound->tryGet<OutT>()) {
                    return std::move(*opt);
                }
            }
            return OutT(std::forward<FallbackT>(fallback));
        }
    };

    template<PathStep FirstStep, typename Second, typename... Rest>
        requires (sizeof...(Rest) >= 1)
    struct FindOrHelper<FirstStep, Second, Rest...>
    {
        static auto eval(const Item& current, FirstStep&& first, Second&& second, Rest&&... rest)
        {
            const Item* pNext = findChildNoexcept(current, std::forward<FirstStep>(first));
            if (pNext == nullptr) {
                return extractFallback(std::forward<Second>(second), std::forward<Rest>(rest)...);
            }
            return FindOrHelper<Second, Rest...>::eval(*pNext, std::forward<Second>(second), std::forward<Rest>(rest)...);
        }

    private:
        template<typename Last>
        static NormalizedFallbackType<Last> extractFallback(Last&& last)
        {
            return NormalizedFallbackType<Last>(std::forward<Last>(last));
        }

        template<typename Head, typename... Tail>
            requires (sizeof...(Tail) >= 1)
        static auto extractFallback(Head&&, Tail&&... tail)
        {
            return extractFallback(std::forward<Tail>(tail)...);
        }
    };
}

// 1. 必填强类型查找：路径缺失抛 `OutOfRangeError`，类型不符抛 `TypeError`
template<typename T, PathStep... Steps>
    requires (sizeof...(Steps) >= 1)
T find(const Item& root, Steps&&... steps)
{
    static_assert(
        !detail::IsStdOptional<T>,
        "Use tomlkit::findOptional<T>(...) (throws on type mismatch) or tomlkit::tryFind<T>(...) (noexcept) instead of find<std::optional<T>>"
    );
    if constexpr (sizeof...(Steps) == 1) {
        auto stepAtOne = [&]<typename S>(S&& s) -> const Item&
            {
                if constexpr (PathKeyLike<S>) {
                    return root.at(std::string_view(s));
                }
                else {
                    if constexpr (std::signed_integral<std::remove_cvref_t<S>>) {
                        if (s < 0) {
                            throw OutOfRangeError(std::format("Negative array index {}", s));
                        }
                    }
                    return root.at((size_t)s);
                }
            };
        return stepAtOne(std::forward<Steps>(steps)...).template get<T>();
    }
    else {
        return root.at(std::forward<Steps>(steps)...).template get<T>();
    }
}

// 2. 选填强类型查找：路径缺失返回 `std::nullopt`，但若路径存在而类型不符则抛 `TypeError`
template<typename T, PathStep... Steps>
    requires (sizeof...(Steps) >= 1)
std::optional<T> findOptional(const Item& root, Steps&&... steps)
{
    static_assert(
        !detail::IsStdOptional<T>,
        "Pass the inner value type T directly to tomlkit::findOptional<T>(...)"
    );
    if (root.isEmpty()) {
        return std::nullopt;
    }
    const Item* pFound = detail::walkPathStrictContainer(root, std::forward<Steps>(steps)...);
    if (pFound == nullptr) {
        return std::nullopt;
    }
    return pFound->get<T>();
}

// 3. 纯容错无异常探测查找：路径缺失或类型不符均返回 `std::nullopt`（保证 `noexcept`）
template<typename T, PathStep... Steps>
    requires (sizeof...(Steps) >= 1)
std::optional<T> tryFind(const Item& root, Steps&&... steps) noexcept
{
    static_assert(
        !detail::IsStdOptional<T>,
        "Pass the inner value type T directly to tomlkit::tryFind<T>(...)"
    );
    const Item* pFound = detail::walkPathNoexcept(root, std::forward<Steps>(steps)...);
    if (pFound == nullptr) {
        return std::nullopt;
    }
    try {
        return pFound->tryGet<T>();
    }
    catch (...) {
        return std::nullopt;
    }
}

// 4. 宽容默认值读取：`tomlkit::findOr(doc, "GUIConfig", "apiLayoutMode", "default")`
template<typename... Args>
    requires (sizeof...(Args) >= 2)
auto findOr(const Item& root, Args&&... args)
{
    return detail::FindOrHelper<Args...>::eval(root, std::forward<Args>(args)...);
}

// 5. 宽容读取或返回默认构造值 / 空数组 / 空表
template<typename T, PathStep... Steps>
    requires (sizeof...(Steps) >= 1 && std::default_initializable<T>)
T findOrDefault(const Item& root, Steps&&... steps)
{
    if (auto opt = tryFind<T>(root, std::forward<Steps>(steps)...)) {
        return std::move(*opt);
    }
    return T{};
}

template<PathStep... Steps>
    requires (sizeof...(Steps) >= 1)
Item findArrayOrEmpty(const Item& root, Steps&&... steps)
{
    const Item* pFound = detail::walkPathNoexcept(root, std::forward<Steps>(steps)...);
    if (pFound != nullptr && pFound->isArray()) {
        return *pFound;
    }
    return Item::array();
}

template<PathStep... Steps>
    requires (sizeof...(Steps) >= 1)
Item findTableOrEmpty(const Item& root, Steps&&... steps)
{
    const Item* pFound = detail::walkPathNoexcept(root, std::forward<Steps>(steps)...);
    if (pFound != nullptr && pFound->isTable()) {
        return *pFound;
    }
    return Item::table();
}

NAMESPACE_END(tomlkit)
