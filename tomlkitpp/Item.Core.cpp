module;

#include "TKPMacros.hpp"

module tomlkitpp;

import :Item;

NAMESPACE_BEGIN(tomlkit)

namespace
{
    constexpr uint64_t hashStringView(std::string_view sv) noexcept
    {
        uint64_t h = 14695981039346656037ull;
        for (const char ch : sv) {
            h ^= (uint64_t)(uint8_t)ch;
            h *= 1099511628211ull;
        }
        return h;
    }
}

// ==================== ArrayElement ====================

Item* ArrayElement::pValue() noexcept
{
    return value.get();
}

const Item* ArrayElement::pValue() const noexcept
{
    return value.get();
}

ArrayElement::ArrayElement()
    : value(std::make_unique<Item>())
{ }

ArrayElement::ArrayElement(Item val)
    : value(std::make_unique<Item>(std::move(val)))
{ }

ArrayElement::ArrayElement(const ArrayElement& other)
    : value(other.value ? std::make_unique<Item>(*other.value) : std::make_unique<Item>())
    , beforeComma(other.beforeComma)
    , afterComma(other.afterComma)
    , hasComma(other.hasComma)
{ }

ArrayElement& ArrayElement::operator=(const ArrayElement& other)
{
    if (this != &other) {
        value = other.value ? std::make_unique<Item>(*other.value) : std::make_unique<Item>();
        beforeComma = other.beforeComma;
        afterComma = other.afterComma;
        hasComma = other.hasComma;
    }
    return *this;
}

ArrayElement::ArrayElement(ArrayElement&&) noexcept = default;
ArrayElement& ArrayElement::operator=(ArrayElement&&) noexcept = default;
ArrayElement::~ArrayElement() = default;

ArrayStorage::ArrayStorage() = default;
ArrayStorage::ArrayStorage(const ArrayStorage&) = default;
ArrayStorage::ArrayStorage(ArrayStorage&&) noexcept = default;
ArrayStorage& ArrayStorage::operator=(const ArrayStorage&) = default;
ArrayStorage& ArrayStorage::operator=(ArrayStorage&&) noexcept = default;
ArrayStorage::~ArrayStorage() = default;

// ==================== TableEntry ====================

TableEntry::TableEntry(Key k, Item v, uint32_t order)
    : key(std::move(k))
    , value(std::make_unique<Item>(std::move(v)))
    , entryOrder(order)
{
    relinkOwner();
}

TableEntry::TableEntry(const TableEntry& other)
    : key(other.key)
    , value(other.value ? std::make_unique<Item>(*other.value) : std::make_unique<Item>())
    , entryOrder(other.entryOrder)
    , beforeComma(other.beforeComma)
    , afterComma(other.afterComma)
    , hasComma(other.hasComma)
{
    relinkOwner();
}

TableEntry::TableEntry(TableEntry&& other) noexcept
    : key(std::move(other.key))
    , value(std::move(other.value))
    , entryOrder(other.entryOrder)
    , beforeComma(std::move(other.beforeComma))
    , afterComma(std::move(other.afterComma))
    , hasComma(other.hasComma)
{
    relinkOwner();
}

TableEntry& TableEntry::operator=(const TableEntry& other)
{
    if (this != &other) {
        key = other.key;
        value = other.value ? std::make_unique<Item>(*other.value) : std::make_unique<Item>();
        entryOrder = other.entryOrder;
        beforeComma = other.beforeComma;
        afterComma = other.afterComma;
        hasComma = other.hasComma;
        relinkOwner();
    }
    return *this;
}

TableEntry& TableEntry::operator=(TableEntry&& other) noexcept
{
    if (this != &other) {
        key = std::move(other.key);
        value = std::move(other.value);
        entryOrder = other.entryOrder;
        beforeComma = std::move(other.beforeComma);
        afterComma = std::move(other.afterComma);
        hasComma = other.hasComma;
        relinkOwner();
    }
    return *this;
}

void TableEntry::relinkOwner() noexcept
{
    if (value != nullptr) {
        value->setOwningKey(&key);
    }
}

TableEntry::~TableEntry() = default;

// ==================== TableStorage ====================

TableStorage::TableStorage() = default;

TableStorage::TableStorage(TableStyle tblStyle)
    : style(tblStyle)
{ }

TableStorage::TableStorage(TableStorage&&) noexcept = default;
TableStorage& TableStorage::operator=(TableStorage&&) noexcept = default;
TableStorage::~TableStorage() = default;

TableStorage::TableStorage(const TableStorage& other)
    : style(other.style)
    , hasBom(other.hasBom)
    , multilineInline(other.multilineInline)
    , isSealed(other.isSealed)
    , isExplicitlyDefined(other.isExplicitlyDefined)
    , dottedKeyScopeId(other.dottedKeyScopeId)
    , sectionOrder(other.sectionOrder)
    , nextEntryOrder(other.nextEntryOrder)
    , headerDecor(other.headerDecor)
    , headerKeyPath(other.headerKeyPath)
    , trailing(other.trailing)
    , hashDirty(true)
{
    entries.reserve(other.entries.size());
    for (const auto& pEntry : other.entries) {
        entries.push_back(std::make_unique<TableEntry>(*pEntry));
    }
}

TableStorage& TableStorage::operator=(const TableStorage& other)
{
    if (this != &other) {
        TableStorage copy(other);
        *this = std::move(copy);
    }
    return *this;
}

void TableStorage::rebuildIndexIfNeeded() const
{
    if (!hashDirty) {
        return;
    }
    hashDirty = false;
    if (entries.size() <= kHashIndexThreshold) {
        hashSlots.clear();
        return;
    }

    const size_t slotCount = std::bit_ceil(entries.size() * 2);
    hashSlots.assign(slotCount, 0);
    const size_t mask = slotCount - 1;

    for (size_t i = 0; i < entries.size(); ++i) {
        size_t slot = (size_t)hashStringView(entries[i]->key.name()) & mask;
        while (hashSlots[slot] != 0) {
            slot = (slot + 1) & mask;
        }
        hashSlots[slot] = i + 1;
    }
}

size_t TableStorage::findIndex(std::string_view keyName) const noexcept
{
    if (entries.size() <= kHashIndexThreshold) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i]->key.name() == keyName) {
                return i;
            }
        }
        return std::string_view::npos;
    }

    rebuildIndexIfNeeded();
    const size_t mask = hashSlots.size() - 1;
    size_t slot = (size_t)hashStringView(keyName) & mask;
    while (hashSlots[slot] != 0) {
        const size_t idx = hashSlots[slot] - 1;
        if (entries[idx]->key.name() == keyName) {
            return idx;
        }
        slot = (slot + 1) & mask;
    }
    return std::string_view::npos;
}

// ==================== Item Core ====================

Item::Item() noexcept = default;
Item::~Item() = default;

Item::Item(const Item& other)
    : m_decor(other.m_decor)
    , m_rawRepr(other.m_rawRepr)
    , m_stringStyle(other.m_stringStyle)
    , m_intRadix(other.m_intRadix)
    , m_pOwningKey(nullptr)
{
    std::visit([this]<typename T>(const T& val)
        {
            if constexpr (std::same_as<T, std::unique_ptr<ArrayStorage>>) {
                m_payload = val ? std::make_unique<ArrayStorage>(*val) : std::make_unique<ArrayStorage>();
            }
            else if constexpr (std::same_as<T, std::unique_ptr<TableStorage>>) {
                m_payload = val ? std::make_unique<TableStorage>(*val) : std::make_unique<TableStorage>();
            }
            else {
                m_payload = val;
            }
        }, other.m_payload);
}

Item::Item(Item&& other) noexcept
    : m_payload(std::move(other.m_payload))
    , m_decor(std::move(other.m_decor))
    , m_rawRepr(std::move(other.m_rawRepr))
    , m_stringStyle(other.m_stringStyle)
    , m_intRadix(other.m_intRadix)
    , m_pOwningKey(nullptr)
{
    other.m_payload = std::monostate{};
}

Item& Item::operator=(const Item& other)
{
    if (this != &other) {
        Item copy(other);
        assignPreservingDecor(std::move(copy));
    }
    return *this;
}

Item& Item::operator=(Item&& other) noexcept
{
    if (this != &other) {
        assignPreservingDecor(std::move(other));
    }
    return *this;
}

void Item::assignPreservingDecor(Item&& replacement)
{
    Key* const pSavedOwner = m_pOwningKey;
    const bool lhsHasDecor = !m_decor.prefix.empty() || !m_decor.suffix.empty();
    const bool rhsHasDecor = !replacement.m_decor.prefix.empty() || !replacement.m_decor.suffix.empty();

    m_payload = std::move(replacement.m_payload);
    m_rawRepr = std::move(replacement.m_rawRepr);
    m_stringStyle = replacement.m_stringStyle;
    m_intRadix = replacement.m_intRadix;
    m_pOwningKey = pSavedOwner;

    if (rhsHasDecor || !lhsHasDecor) {
        m_decor = std::move(replacement.m_decor);
    }
}

Item::Item(bool val) noexcept
    : m_payload(val)
{ }

Item::Item(const char* str)
    : m_payload(std::string(str ? str : ""))
{ }

Item::Item(std::string_view sv)
    : m_payload(std::string(sv))
{ }

Item::Item(std::string str)
    : m_payload(std::move(str))
{ }

Item::Item(LocalDate val) noexcept
    : m_payload(val)
{ }

Item::Item(LocalTime val) noexcept
    : m_payload(val)
{ }

Item::Item(LocalDateTime val) noexcept
    : m_payload(val)
{ }

Item::Item(OffsetDateTime val) noexcept
    : m_payload(val)
{ }

Item::Item(std::initializer_list<Item> arrayElements)
{
    auto pArr = std::make_unique<ArrayStorage>();
    pArr->elements.reserve(arrayElements.size());
    for (const Item& elem : arrayElements) {
        pArr->elements.emplace_back(elem);
    }
    m_payload = std::move(pArr);
}

Item Item::document()
{
    return table(TableStyle::DocumentRoot);
}

Item Item::array(std::initializer_list<Item> init)
{
    return Item(init);
}

Item Item::arrayOfTables()
{
    Item item;
    auto pArr = std::make_unique<ArrayStorage>();
    pArr->style = ArrayStyle::ArrayOfTables;
    item.m_payload = std::move(pArr);
    return item;
}

Item Item::table(TableStyle style)
{
    Item item;
    item.m_payload = std::make_unique<TableStorage>(style);
    return item;
}

Item Item::table(std::initializer_list<std::pair<std::string_view, Item>> init, TableStyle style)
{
    Item item = table(style);
    for (const auto& [k, v] : init) {
        item[k] = v;
    }
    return item;
}

Item Item::inlineTable(std::initializer_list<std::pair<std::string_view, Item>> init)
{
    return table(init, TableStyle::Inline);
}

ItemKind Item::kind() const noexcept
{
    return (ItemKind)m_payload.index();
}

bool Item::isInlineTable() const noexcept
{
    if (const auto* pTbl = tableStorage()) {
        return pTbl->style == TableStyle::Inline;
    }
    return false;
}

bool Item::isArrayOfTables() const noexcept
{
    if (const auto* pArr = arrayStorage()) {
        return pArr->style == ArrayStyle::ArrayOfTables;
    }
    return false;
}

void Item::ensureKind(ItemKind expected) const
{
    if (kind() != expected) {
        throw TypeError(std::format(
            "TOML type mismatch: expected '{}', but actual kind is '{}'",
            itemKindName(expected),
            kindName()
        ));
    }
}

double Item::asNumber() const
{
    if (isFloat()) {
        return std::get<double>(m_payload);
    }
    if (isInteger()) {
        return (double)std::get<int64_t>(m_payload);
    }
    throw TypeError(std::format("TOML type mismatch: expected numeric kind, got '{}'", kindName()));
}

ArrayStorage* Item::arrayStorage() noexcept
{
    if (auto* pPtr = std::get_if<std::unique_ptr<ArrayStorage>>(&m_payload)) {
        return pPtr->get();
    }
    return nullptr;
}

const ArrayStorage* Item::arrayStorage() const noexcept
{
    if (const auto* pPtr = std::get_if<std::unique_ptr<ArrayStorage>>(&m_payload)) {
        return pPtr->get();
    }
    return nullptr;
}

TableStorage* Item::tableStorage() noexcept
{
    if (auto* pPtr = std::get_if<std::unique_ptr<TableStorage>>(&m_payload)) {
        return pPtr->get();
    }
    return nullptr;
}

const TableStorage* Item::tableStorage() const noexcept
{
    if (const auto* pPtr = std::get_if<std::unique_ptr<TableStorage>>(&m_payload)) {
        return pPtr->get();
    }
    return nullptr;
}

bool operator==(const Item& lhs, const Item& rhs) noexcept
{
    if (lhs.kind() != rhs.kind()) {
        return false;
    }

    switch (lhs.kind())
    {
    case ItemKind::Empty:
        return true;
    case ItemKind::Boolean:
        return std::get<bool>(lhs.m_payload) == std::get<bool>(rhs.m_payload);
    case ItemKind::Integer:
        return std::get<int64_t>(lhs.m_payload) == std::get<int64_t>(rhs.m_payload);
    case ItemKind::Float:
    {
        const double a = std::get<double>(lhs.m_payload);
        const double b = std::get<double>(rhs.m_payload);
        if (std::isnan(a) && std::isnan(b)) {
            return true;
        }
        return a == b;
    }
    case ItemKind::String:
        return std::get<std::string>(lhs.m_payload) == std::get<std::string>(rhs.m_payload);
    case ItemKind::LocalDate:
        return std::get<LocalDate>(lhs.m_payload) == std::get<LocalDate>(rhs.m_payload);
    case ItemKind::LocalTime:
        return std::get<LocalTime>(lhs.m_payload) == std::get<LocalTime>(rhs.m_payload);
    case ItemKind::LocalDateTime:
        return std::get<LocalDateTime>(lhs.m_payload) == std::get<LocalDateTime>(rhs.m_payload);
    case ItemKind::OffsetDateTime:
        return std::get<OffsetDateTime>(lhs.m_payload) == std::get<OffsetDateTime>(rhs.m_payload);
    case ItemKind::Array:
    {
        const auto* pA = lhs.arrayStorage();
        const auto* pB = rhs.arrayStorage();
        if (pA->elements.size() != pB->elements.size()) {
            return false;
        }
        for (size_t i = 0; i < pA->elements.size(); ++i) {
            if (*pA->elements[i].value != *pB->elements[i].value) {
                return false;
            }
        }
        return true;
    }
    case ItemKind::Table:
    {
        const auto* pA = lhs.tableStorage();
        const auto* pB = rhs.tableStorage();
        if (pA->entries.size() != pB->entries.size()) {
            return false;
        }
        for (const auto& pEntry : pA->entries) {
            const Item* pOtherVal = rhs.find(pEntry->key.name());
            if (pOtherVal == nullptr || *pEntry->value != *pOtherVal) {
                return false;
            }
        }
        return true;
    }
    default:
        TKP_UNREACHABLE();
    }
}

NAMESPACE_END(tomlkit)
