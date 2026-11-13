module;

#include "TKPMacros.hpp"

module tomlkitpp;

import :Item;

NAMESPACE_BEGIN(tomlkit)

size_t Item::size() const noexcept
{
    if (const auto* pArr = arrayStorage()) {
        return pArr->elements.size();
    }
    if (const auto* pTbl = tableStorage()) {
        return pTbl->entries.size();
    }
    if (isEmpty()) {
        return 0;
    }
    return 1;
}

bool Item::empty() const noexcept
{
    if (const auto* pArr = arrayStorage()) {
        return pArr->elements.empty();
    }
    if (const auto* pTbl = tableStorage()) {
        return pTbl->entries.empty();
    }
    return isEmpty();
}

void Item::clear()
{
    m_rawRepr.clear();
    if (auto* pArr = arrayStorage()) {
        pArr->elements.clear();
        pArr->trailing.clear();
        return;
    }
    if (auto* pTbl = tableStorage()) {
        pTbl->entries.clear();
        pTbl->trailing.clear();
        pTbl->invalidateIndex();
        return;
    }
    m_payload = std::monostate{};
}

bool Item::contains(std::string_view key) const noexcept
{
    return find(key) != nullptr;
}

Item* Item::find(std::string_view key) noexcept
{
    auto* pTbl = tableStorage();
    if (pTbl == nullptr) {
        return nullptr;
    }
    const size_t idx = pTbl->findIndex(key);
    return (idx == std::string_view::npos) ? nullptr : pTbl->entries[idx]->value.get();
}

const Item* Item::find(std::string_view key) const noexcept
{
    const auto* pTbl = tableStorage();
    if (pTbl == nullptr) {
        return nullptr;
    }
    const size_t idx = pTbl->findIndex(key);
    return (idx == std::string_view::npos) ? nullptr : pTbl->entries[idx]->value.get();
}

Item* Item::find(size_t index) noexcept
{
    auto* pArr = arrayStorage();
    if (pArr == nullptr || index >= pArr->elements.size()) {
        return nullptr;
    }
    return pArr->elements[index].value.get();
}

const Item* Item::find(size_t index) const noexcept
{
    const auto* pArr = arrayStorage();
    if (pArr == nullptr || index >= pArr->elements.size()) {
        return nullptr;
    }
    return pArr->elements[index].value.get();
}

Item& Item::at(std::string_view key)
{
    if (!isTable()) {
        throw TypeError(std::format("Cannot lookup key '{}' on non-table item of kind '{}'", key, kindName()));
    }
    if (Item* pFound = find(key)) {
        return *pFound;
    }
    throw OutOfRangeError(std::format("Key '{}' not found in TOML table", key));
}

const Item& Item::at(std::string_view key) const
{
    if (!isTable()) {
        throw TypeError(std::format("Cannot lookup key '{}' on non-table item of kind '{}'", key, kindName()));
    }
    if (const Item* pFound = find(key)) {
        return *pFound;
    }
    throw OutOfRangeError(std::format("Key '{}' not found in TOML table", key));
}

Item& Item::at(size_t index)
{
    if (!isArray()) {
        throw TypeError(std::format("Cannot index [{}] on non-array item of kind '{}'", index, kindName()));
    }
    if (Item* pFound = find(index)) {
        return *pFound;
    }
    throw OutOfRangeError(std::format("Array index {} out of bounds (size = {})", index, size()));
}

const Item& Item::at(size_t index) const
{
    if (!isArray()) {
        throw TypeError(std::format("Cannot index [{}] on non-array item of kind '{}'", index, kindName()));
    }
    if (const Item* pFound = find(index)) {
        return *pFound;
    }
    throw OutOfRangeError(std::format("Array index {} out of bounds (size = {})", index, size()));
}

Item& Item::operator[](std::string_view key)
{
    if (isEmpty()) {
        m_payload = std::make_unique<TableStorage>(TableStyle::Standard);
        m_rawRepr.clear();
    }
    ensureKind(ItemKind::Table);

    auto* pTbl = tableStorage();
    if (const size_t idx = pTbl->findIndex(key); idx != std::string_view::npos) {
        return *pTbl->entries[idx]->value;
    }

    Key newKey{ std::string(key), KeyStyle::Bare };
    Item newVal{};

    if (pTbl->style == TableStyle::Inline) {
        // 为内联表追加新键值对时，补齐上一项的逗号与间隔空格
        if (!pTbl->entries.empty()) {
            auto& prevEntry = *pTbl->entries.back();
            if (!prevEntry.hasComma) {
                prevEntry.hasComma = true;
                if (pTbl->multilineInline) {
                    prevEntry.afterComma = "\n";
                }
                else if (prevEntry.afterComma.empty()) {
                    prevEntry.afterComma = " ";
                }
            }
        }
        else if (!pTbl->multilineInline) {
            newKey.leaf().decor.prefix = " ";
            if (pTbl->trailing.empty()) {
                pTbl->trailing = " ";
            }
        }

        if (pTbl->multilineInline) {
            newKey.leaf().decor.prefix = "  ";
        }
        newKey.leaf().decor.suffix = " ";
        newVal.decor().prefix = " ";
    }
    else {
        newKey.leaf().decor.suffix = " ";
        newVal.decor().prefix = " ";
        newVal.decor().suffix = "\n";
    }

    const uint32_t order = pTbl->nextEntryOrder++;
    pTbl->entries.push_back(std::make_unique<TableEntry>(std::move(newKey), std::move(newVal), order));
    pTbl->invalidateIndex();
    return *pTbl->entries.back()->value;
}

std::pair<Item&, bool> Item::insertOrAssign(std::string_view key, Item val)
{
    if (isEmpty()) {
        m_payload = std::make_unique<TableStorage>(TableStyle::Standard);
        m_rawRepr.clear();
    }
    ensureKind(ItemKind::Table);

    auto* pTbl = tableStorage();
    if (const size_t idx = pTbl->findIndex(key); idx != std::string_view::npos) {
        *pTbl->entries[idx]->value = std::move(val);
        return { *pTbl->entries[idx]->value, false };
    }

    Item& ref = (*this)[key];
    ref = std::move(val);
    return { ref, true };
}

void Item::push_back(const Item& elem)
{
    // 先拷贝副本，防止 `arr.push_back(arr[0])` 因底层扩容导致悬垂引用
    Item copy(elem);
    push_back(std::move(copy));
}

void Item::push_back(Item&& elem)
{
    if (isEmpty()) {
        m_payload = std::make_unique<ArrayStorage>();
        m_rawRepr.clear();
    }
    ensureKind(ItemKind::Array);

    auto* pArr = arrayStorage();
    if (pArr->style == ArrayStyle::ArrayOfTables) {
        if (elem.isEmpty()) {
            elem = Item::table(TableStyle::ArrayOfTablesElement);
        }
        else if (elem.isTable()) {
            elem.tableStorage()->style = TableStyle::ArrayOfTablesElement;
        }
        else {
            throw TypeError("Array-of-Tables can only contain Table elements");
        }
        pArr->elements.emplace_back(std::move(elem));
        return;
    }

    // 标准数组内的表元素在 TOML 语法中只能以内联表 `{ ... }` 形式表达：若调用方传入新建的 Standard 表，自动归一化为单行内联表布局
    if (auto* pElemTbl = elem.tableStorage(); pElemTbl != nullptr && pElemTbl->style == TableStyle::Standard && pElemTbl->sectionOrder == 0) {
        pElemTbl->style = TableStyle::Inline;
        if (!pElemTbl->multilineInline) {
            for (size_t i = 0; i < pElemTbl->entries.size(); ++i) {
                TableEntry& te = *pElemTbl->entries[i];
                if (i == 0 && te.key.leaf().decor.prefix.empty()) {
                    te.key.leaf().decor.prefix = " ";
                }
                if (te.value->decor().suffix == "\n") {
                    te.value->decor().suffix.clear();
                }
                if (i + 1 < pElemTbl->entries.size()) {
                    te.hasComma = true;
                    if (te.afterComma.empty()) {
                        te.afterComma = " ";
                    }
                }
            }
            if (!pElemTbl->entries.empty() && pElemTbl->trailing.empty()) {
                pElemTbl->trailing = " ";
            }
        }
    }

    // 标准数组追加：保持与现有单行或多行风格一致
    if (!pArr->elements.empty()) {
        ArrayElement& prev = pArr->elements.back();
        if (!prev.hasComma) {
            prev.hasComma = true;
            if (pArr->multiline || prev.value->decor().prefix.contains('\n')) {
                // 若此前无逗号且末尾行尾注释存放在 `beforeComma` 中，将其迁移到 `afterComma`
                if (!prev.beforeComma.empty() && prev.beforeComma.contains('\n')) {
                    prev.afterComma = std::move(prev.beforeComma);
                    prev.beforeComma.clear();
                }
                else {
                    prev.afterComma = "\n";
                }
            }
            else if (prev.afterComma.empty()) {
                prev.afterComma = " ";
            }
        }
    }

    const bool isMulti = pArr->multiline
        || (!pArr->elements.empty() && (pArr->elements.front().value->decor().prefix.contains('\n') || pArr->elements.front().afterComma.contains('\n')));

    if (isMulti && elem.decor().prefix.empty()) {
        if (pArr->elements.empty()) {
            elem.decor().prefix = "\n  ";
            if (pArr->trailing.empty()) {
                pArr->trailing = "\n";
            }
        }
        else {
            elem.decor().prefix = "  ";
        }
    }

    ArrayElement wrapped(std::move(elem));
    if (isMulti) {
        wrapped.hasComma = true;
        wrapped.afterComma = "\n";
    }
    pArr->elements.push_back(std::move(wrapped));
}

Item& Item::front()
{
    return at((size_t)0);
}

const Item& Item::front() const
{
    return at((size_t)0);
}

Item& Item::back()
{
    ensureKind(ItemKind::Array);
    const size_t n = size();
    if (n == 0) {
        throw OutOfRangeError("Called back() on empty TOML array");
    }
    return at(n - 1);
}

const Item& Item::back() const
{
    ensureKind(ItemKind::Array);
    const size_t n = size();
    if (n == 0) {
        throw OutOfRangeError("Called back() on empty TOML array");
    }
    return at(n - 1);
}

bool Item::erase(std::string_view key)
{
    auto* pTbl = tableStorage();
    if (pTbl == nullptr) {
        return false;
    }
    const size_t idx = pTbl->findIndex(key);
    if (idx == std::string_view::npos) {
        return false;
    }

    pTbl->entries.erase(pTbl->entries.begin() + (ptrdiff_t)idx);
    pTbl->invalidateIndex();

    // 单行内联表删除末尾项后，清理新的末尾项多余的尾随逗号（若原本无换行）
    if (pTbl->style == TableStyle::Inline && !pTbl->multilineInline && !pTbl->entries.empty()) {
        auto& lastEntry = *pTbl->entries.back();
        if (lastEntry.hasComma && !lastEntry.afterComma.contains('\n') && !lastEntry.afterComma.contains('#')) {
            lastEntry.hasComma = false;
            if (pTbl->trailing.empty()) {
                pTbl->trailing = std::move(lastEntry.afterComma);
            }
            lastEntry.afterComma.clear();
        }
    }

    return true;
}

void Item::erase(size_t index)
{
    ensureKind(ItemKind::Array);
    auto* pArr = arrayStorage();
    if (index >= pArr->elements.size()) {
        throw OutOfRangeError(std::format("Array erase index {} out of bounds (size = {})", index, pArr->elements.size()));
    }

    pArr->elements.erase(pArr->elements.begin() + (ptrdiff_t)index);

    // 若为单行数组且删除了原本的最后一项，清理新末尾项的尾随逗号
    if (!pArr->multiline && !pArr->elements.empty()) {
        ArrayElement& lastElem = pArr->elements.back();
        if (lastElem.hasComma && !lastElem.afterComma.contains('\n') && !lastElem.afterComma.contains('#')) {
            lastElem.hasComma = false;
            lastElem.afterComma.clear();
        }
    }
}

Item::iterator Item::begin()
{
    ensureKind(ItemKind::Array);
    return iterator(arrayStorage()->elements.begin());
}

Item::iterator Item::end()
{
    ensureKind(ItemKind::Array);
    return iterator(arrayStorage()->elements.end());
}

Item::const_iterator Item::begin() const
{
    ensureKind(ItemKind::Array);
    return const_iterator(arrayStorage()->elements.begin());
}

Item::const_iterator Item::end() const
{
    ensureKind(ItemKind::Array);
    return const_iterator(arrayStorage()->elements.end());
}

TableItemsRange<false> Item::tableItems()
{
    ensureKind(ItemKind::Table);
    return TableItemsRange<false>(&tableStorage()->entries);
}

TableItemsRange<true> Item::tableItems() const
{
    ensureKind(ItemKind::Table);
    return TableItemsRange<true>(&tableStorage()->entries);
}

// ==================== 注释与样式 API ====================

std::optional<std::string> Item::inlineComment() const
{
    if (const auto* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::Standard || pTbl->style == TableStyle::ArrayOfTablesElement) {
            return pTbl->headerDecor.inlineComment();
        }
    }
    return m_decor.inlineComment();
}

Item& Item::setInlineComment(std::string_view comment)
{
    if (auto* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::Standard || pTbl->style == TableStyle::ArrayOfTablesElement) {
            if (pTbl->headerDecor.suffix.empty()) {
                pTbl->headerDecor.suffix = "\n";
            }
            pTbl->headerDecor.setInlineComment(comment);
            return *this;
        }
    }
    m_decor.setInlineComment(comment);
    return *this;
}

Item& Item::clearInlineComment()
{
    if (auto* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::Standard || pTbl->style == TableStyle::ArrayOfTablesElement) {
            pTbl->headerDecor.clearInlineComment();
            return *this;
        }
    }
    m_decor.clearInlineComment();
    return *this;
}

std::vector<std::string> Item::leadingComments() const
{
    if (const auto* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::DocumentRoot
            || pTbl->style == TableStyle::Standard
            || pTbl->style == TableStyle::ArrayOfTablesElement)
        {
            return pTbl->headerDecor.leadingComments();
        }
    }
    if (m_pOwningKey != nullptr) {
        return m_pOwningKey->leadingComments();
    }
    return m_decor.leadingComments();
}

Item& Item::addLeadingComment(std::string_view comment, std::string_view newline)
{
    if (auto* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::DocumentRoot
            || pTbl->style == TableStyle::Standard
            || pTbl->style == TableStyle::ArrayOfTablesElement)
        {
            pTbl->headerDecor.addLeadingComment(comment, newline);
            return *this;
        }
    }
    if (m_pOwningKey != nullptr) {
        m_pOwningKey->addLeadingComment(comment, newline);
        return *this;
    }
    m_decor.addLeadingComment(comment, newline);
    return *this;
}

Item& Item::clearLeadingComments()
{
    if (auto* pTbl = tableStorage()) {
        if (pTbl->style == TableStyle::DocumentRoot
            || pTbl->style == TableStyle::Standard
            || pTbl->style == TableStyle::ArrayOfTablesElement)
        {
            pTbl->headerDecor.clearLeadingComments();
            return *this;
        }
    }
    if (m_pOwningKey != nullptr) {
        m_pOwningKey->clearLeadingComments();
        return *this;
    }
    m_decor.clearLeadingComments();
    return *this;
}

Key& Item::keyAt(std::string_view key)
{
    ensureKind(ItemKind::Table);
    auto* pTbl = tableStorage();
    const size_t idx = pTbl->findIndex(key);
    if (idx == std::string_view::npos) {
        throw OutOfRangeError(std::format("Key '{}' not found in TOML table", key));
    }
    return pTbl->entries[idx]->key;
}

const Key& Item::keyAt(std::string_view key) const
{
    ensureKind(ItemKind::Table);
    const auto* pTbl = tableStorage();
    const size_t idx = pTbl->findIndex(key);
    if (idx == std::string_view::npos) {
        throw OutOfRangeError(std::format("Key '{}' not found in TOML table", key));
    }
    return pTbl->entries[idx]->key;
}

Item& Item::setStringStyle(StringStyle style) noexcept
{
    m_stringStyle = style;
    m_rawRepr.clear();
    return *this;
}

Item& Item::setIntegerRadix(IntegerRadix radix) noexcept
{
    m_intRadix = radix;
    m_rawRepr.clear();
    return *this;
}

TableStyle Item::tableStyle() const noexcept
{
    if (const auto* pTbl = tableStorage()) {
        return pTbl->style;
    }
    return TableStyle::Standard;
}

Item& Item::setTableStyle(TableStyle style)
{
    ensureKind(ItemKind::Table);
    tableStorage()->style = style;
    return *this;
}

ArrayStyle Item::arrayStyle() const noexcept
{
    if (const auto* pArr = arrayStorage()) {
        return pArr->style;
    }
    return ArrayStyle::Standard;
}

Item& Item::setArrayStyle(ArrayStyle style)
{
    ensureKind(ItemKind::Array);
    arrayStorage()->style = style;
    return *this;
}

Item& Item::setMultilineArray(bool multiline)
{
    ensureKind(ItemKind::Array);
    auto* pArr = arrayStorage();
    pArr->multiline = multiline;
    pArr->hasExplicitLayout = true;

    if (pArr->style == ArrayStyle::Standard && !pArr->elements.empty()) {
        // 仅在现有元素未包含注释时自动重排单行/多行空白，避免破坏已有注释排版
        bool hasAnyComments = pArr->trailing.contains('#');
        for (const ArrayElement& el : pArr->elements) {
            if (el.value->decor().prefix.contains('#')
                || el.beforeComma.contains('#')
                || el.afterComma.contains('#'))
            {
                hasAnyComments = true;
                break;
            }
        }

        if (!hasAnyComments) {
            if (multiline) {
                for (size_t i = 0; i < pArr->elements.size(); ++i) {
                    ArrayElement& el = pArr->elements[i];
                    el.value->decor().prefix = (i == 0) ? "\n  " : "  ";
                    el.beforeComma.clear();
                    el.hasComma = true;
                    el.afterComma = "\n";
                }
                pArr->trailing.clear();
            }
            else {
                for (size_t i = 0; i < pArr->elements.size(); ++i) {
                    ArrayElement& el = pArr->elements[i];
                    el.value->decor().prefix.clear();
                    el.beforeComma.clear();
                    el.hasComma = (i + 1 < pArr->elements.size());
                    el.afterComma = (i + 1 < pArr->elements.size()) ? " " : "";
                }
                pArr->trailing.clear();
            }
        }
    }

    return *this;
}

Item& Item::setMultilineInlineTable(bool multiline)
{
    ensureKind(ItemKind::Table);
    auto* pTbl = tableStorage();
    pTbl->style = TableStyle::Inline;
    pTbl->multilineInline = multiline;
    return *this;
}

NAMESPACE_END(tomlkit)
