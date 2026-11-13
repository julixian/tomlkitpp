module;

#include "TKPMacros.hpp"

module tomlkitpp;

import :Emitter;

namespace fs = std::filesystem;

NAMESPACE_BEGIN(tomlkit)

namespace
{
    struct FlattenedBodyEntry
    {
        std::vector<KeySegment> fallbackDottedPrefix;
        const TableEntry* pEntry = nullptr;
        uint32_t primaryOrder = 0;
        uint32_t secondaryOrder = 0;
    };

    struct SectionTask
    {
        const TableStorage* pTable = nullptr;
        bool isAotElement = false;
        std::vector<KeySegment> fallbackPath;
        uint32_t anchorOrder = 0;
        uint32_t discoveryOrder = 0;
    };

    bool isSectionLevelChild(const Item& child) noexcept
    {
        if (const auto* pArr = child.arrayStorage()) {
            return pArr->style == ArrayStyle::ArrayOfTables;
        }
        if (const auto* pTbl = child.tableStorage()) {
            return pTbl->style == TableStyle::Standard
                || pTbl->style == TableStyle::ImplicitSuper
                || pTbl->style == TableStyle::ArrayOfTablesElement
                || pTbl->style == TableStyle::DocumentRoot;
        }
        return false;
    }

    class EmitterImpl
    {
    public:
        explicit EmitterImpl(FormatOptions options) noexcept
            : m_options(options)
        { }

        std::string emitTopLevel(const Item& item)
        {
            if (!item.isTable()) {
                std::string out;
                out.append(item.decor().prefix);
                emitValue(out, item);
                out.append(item.decor().suffix);
                return out;
            }

            const TableStorage* const pRoot = item.tableStorage();
            if (pRoot->style == TableStyle::Inline) {
                std::string out;
                out.append(item.decor().prefix);
                emitInlineTable(out, pRoot);
                out.append(item.decor().suffix);
                return out;
            }

            std::string out;
            if (pRoot->hasBom) {
                out.append("\xEF\xBB\xBF");
            }

            // 若传入的是单个带表头的非根 Standard 表（非 DocumentRoot），先输出其自身表头
            if (pRoot->style == TableStyle::Standard && !pRoot->headerKeyPath.empty()) {
                emitSectionHeader(out, *pRoot, false, pRoot->headerKeyPath);
                emitSectionBody(out, *pRoot, pRoot->headerKeyPath);
            }
            else {
                out.append(pRoot->headerDecor.prefix);
                emitSectionBody(out, *pRoot, {});
            }

            std::vector<SectionTask> sections;
            uint32_t discoveryCounter = 1;
            std::vector<KeySegment> curPath;
            collectSections(*pRoot, curPath, std::numeric_limits<uint32_t>::max() - 1u, discoveryCounter, sections);

            std::ranges::stable_sort(sections, [](const SectionTask& a, const SectionTask& b)
                {
                    if (a.anchorOrder != b.anchorOrder) {
                        return a.anchorOrder < b.anchorOrder;
                    }
                    return a.discoveryOrder < b.discoveryOrder;
                });

            for (const SectionTask& task : sections) {
                const bool isNewlyCreated = (task.pTable->sectionOrder == 0);
                if (!out.empty() && !out.ends_with('\n')) {
                    out.append(m_options.newline);
                }
                if (isNewlyCreated && task.pTable->headerDecor.prefix.empty() && !out.empty()) {
                    if (!out.ends_with("\n\n") && !out.ends_with("\r\n\r\n")) {
                        out.append(m_options.newline);
                    }
                }

                const auto& pathToUse = task.pTable->headerKeyPath.empty()
                    ? task.fallbackPath
                    : task.pTable->headerKeyPath;

                emitSectionHeader(out, *task.pTable, task.isAotElement, pathToUse);
                emitSectionBody(out, *task.pTable, pathToUse);
            }

            if (!pRoot->trailing.empty()) {
                if (!out.empty() && !out.ends_with('\n') && !pRoot->trailing.starts_with('\n') && !pRoot->trailing.starts_with('\r')) {
                    out.append(m_options.newline);
                }
                out.append(pRoot->trailing);
            }

            return out;
        }

    private:
        static bool hasBodyEntries(const TableStorage& tbl) noexcept
        {
            for (const auto& pEntry : tbl.entries) {
                const Item& val = *pEntry->value;
                if (isSectionLevelChild(val)) {
                    continue;
                }
                if (const auto* pSubTbl = val.tableStorage(); pSubTbl != nullptr && pSubTbl->style == TableStyle::Dotted) {
                    if (hasBodyEntries(*pSubTbl)) {
                        return true;
                    }
                    continue;
                }
                return true;
            }
            return false;
        }

        static uint32_t maxParsedEntryOrderInSubtree(const TableStorage& tbl, uint32_t fallbackOrder) noexcept
        {
            uint32_t best = fallbackOrder;
            for (const auto& pEntry : tbl.entries) {
                const Item& val = *pEntry->value;
                if (isSectionLevelChild(val)) {
                    continue;
                }
                if (const auto* pSubTbl = val.tableStorage(); pSubTbl != nullptr && pSubTbl->style == TableStyle::Dotted) {
                    best = std::max(best, maxParsedEntryOrderInSubtree(*pSubTbl, pEntry->entryOrder));
                }
                else {
                    best = std::max(best, pEntry->entryOrder);
                }
            }
            return best;
        }

        void collectBodyEntries(
            const TableStorage& tbl,
            std::vector<KeySegment>& prefixAccum,
            uint32_t inheritedPrimaryOrder,
            uint32_t& seqCounter,
            std::vector<FlattenedBodyEntry>& outEntries,
            bool insideInlineTable = false
        ) const
        {
            for (const auto& pEntry : tbl.entries) {
                const Item& val = *pEntry->value;
                if (!insideInlineTable && isSectionLevelChild(val)) {
                    continue;
                }

                if (const auto* pSubTbl = val.tableStorage(); pSubTbl != nullptr && pSubTbl->style == TableStyle::Dotted) {
                    prefixAccum.push_back(pEntry->key.leaf());
                    const uint32_t subAnchor = maxParsedEntryOrderInSubtree(*pSubTbl, pEntry->entryOrder);
                    collectBodyEntries(*pSubTbl, prefixAccum, subAnchor, seqCounter, outEntries, insideInlineTable);
                    prefixAccum.pop_back();
                    continue;
                }

                // 若为解析阶段生成的叶节点，其 `entryOrder` 直接记录了所属节内的行序；
                // 若为后续通过 `doc["a"]["b"]["newKey"]` 追加的叶节点（`dottedPrefix` 为空但 `prefixAccum` 非空），归入对应点分前缀组末尾
                const bool isProgrammaticDottedChild = !prefixAccum.empty() && pEntry->key.dottedPrefix().empty();
                const uint32_t primary = isProgrammaticDottedChild ? inheritedPrimaryOrder : pEntry->entryOrder;

                outEntries.push_back(FlattenedBodyEntry{
                    .fallbackDottedPrefix = prefixAccum,
                    .pEntry = pEntry.get(),
                    .primaryOrder = primary,
                    .secondaryOrder = seqCounter++
                });
            }
        }

        static uint32_t maxParsedSectionOrderInSubtree(const TableStorage& tbl) noexcept
        {
            uint32_t maxOrd = tbl.sectionOrder;
            for (const auto& pEntry : tbl.entries) {
                const Item& val = *pEntry->value;
                if (const auto* pSubTbl = val.tableStorage()) {
                    if (pSubTbl->style != TableStyle::Inline) {
                        maxOrd = std::max(maxOrd, maxParsedSectionOrderInSubtree(*pSubTbl));
                    }
                }
                else if (const auto* pArr = val.arrayStorage(); pArr != nullptr && pArr->style == ArrayStyle::ArrayOfTables) {
                    for (const auto& elem : pArr->elements) {
                        if (const auto* pElemTbl = elem.value->tableStorage()) {
                            maxOrd = std::max(maxOrd, maxParsedSectionOrderInSubtree(*pElemTbl));
                        }
                    }
                }
            }
            return maxOrd;
        }

        void collectSections(
            const TableStorage& parentTbl,
            std::vector<KeySegment>& currentPath,
            uint32_t parentAnchorOrder,
            uint32_t& discoveryCounter,
            std::vector<SectionTask>& outSections
        ) const
        {
            for (const auto& pEntry : parentTbl.entries) {
                const Item& val = *pEntry->value;

                if (const auto* pSubTbl = val.tableStorage()) {
                    if (pSubTbl->style == TableStyle::Inline) {
                        continue;
                    }

                    currentPath.emplace_back(pEntry->key.leaf().name, pEntry->key.leaf().style);

                    if (pSubTbl->style == TableStyle::Dotted) {
                        // 点分表自身不输出 `[header]`，但内部可能挂载了 `[[fruit.apple.seeds]]` 等子节
                        collectSections(*pSubTbl, currentPath, parentAnchorOrder, discoveryCounter, outSections);
                    }
                    else if (pSubTbl->style == TableStyle::ImplicitSuper && !hasBodyEntries(*pSubTbl)) {
                        // 纯隐式父表无自身键值时不输出 `[header]`，继续递归其子表
                        const uint32_t subAnchor = maxParsedSectionOrderInSubtree(*pSubTbl);
                        collectSections(
                            *pSubTbl,
                            currentPath,
                            subAnchor > 0 ? subAnchor : parentAnchorOrder,
                            discoveryCounter,
                            outSections
                        );
                    }
                    else {
                        const uint32_t myAnchor = (pSubTbl->sectionOrder > 0) ? pSubTbl->sectionOrder : parentAnchorOrder;
                        const uint32_t myDisc = (pSubTbl->sectionOrder > 0) ? 0u : discoveryCounter++;

                        outSections.push_back(SectionTask{
                            .pTable = pSubTbl,
                            .isAotElement = false,
                            .fallbackPath = currentPath,
                            .anchorOrder = myAnchor,
                            .discoveryOrder = myDisc
                        });

                        collectSections(*pSubTbl, currentPath, myAnchor, discoveryCounter, outSections);
                    }

                    currentPath.pop_back();
                }
                else if (const auto* pArr = val.arrayStorage(); pArr != nullptr && pArr->style == ArrayStyle::ArrayOfTables) {
                    currentPath.emplace_back(pEntry->key.leaf().name, pEntry->key.leaf().style);
                    uint32_t runningAnchor = parentAnchorOrder;

                    for (const auto& elem : pArr->elements) {
                        const auto* pElemTbl = elem.value->tableStorage();
                        if (pElemTbl == nullptr) {
                            continue;
                        }

                        const uint32_t myAnchor = (pElemTbl->sectionOrder > 0) ? pElemTbl->sectionOrder : runningAnchor;
                        const uint32_t myDisc = (pElemTbl->sectionOrder > 0) ? 0u : discoveryCounter++;
                        runningAnchor = myAnchor;

                        outSections.push_back(SectionTask{
                            .pTable = pElemTbl,
                            .isAotElement = true,
                            .fallbackPath = currentPath,
                            .anchorOrder = myAnchor,
                            .discoveryOrder = myDisc
                        });

                        collectSections(*pElemTbl, currentPath, myAnchor, discoveryCounter, outSections);
                    }

                    currentPath.pop_back();
                }
            }
        }

        void emitKeyPath(std::string& out, std::span<const KeySegment> segments, bool includeFirstPrefix) const
        {
            for (size_t i = 0; i < segments.size(); ++i) {
                if (i > 0) {
                    out.push_back('.');
                }
                if (i > 0 || includeFirstPrefix) {
                    out.append(segments[i].decor.prefix);
                }
                out.append(formatKeySegment(segments[i], m_options.spec));
                out.append(segments[i].decor.suffix);
            }
        }

        void emitEntryKey(std::string& out, const FlattenedBodyEntry& flatEntry) const
        {
            const Key& k = flatEntry.pEntry->key;
            if (!k.dottedPrefix().empty()) {
                emitKeyPath(out, k.dottedPrefix(), true);
                out.push_back('.');
                out.append(k.leaf().decor.prefix);
                out.append(formatKeySegment(k.leaf(), m_options.spec));
                out.append(k.leaf().decor.suffix);
                return;
            }

            if (!flatEntry.fallbackDottedPrefix.empty()) {
                // 程序通过 `doc["a"]["b"]["newKey"]` 在点分表下动态新建的键：前导注释挂在 `k.leaf().decor.prefix`
                out.append(k.leaf().decor.prefix);
                for (const KeySegment& prefixSeg : flatEntry.fallbackDottedPrefix) {
                    KeySegment cleanSeg(prefixSeg.name, prefixSeg.style);
                    out.append(formatKeySegment(cleanSeg, m_options.spec));
                    out.push_back('.');
                }
                out.append(formatKeySegment(k.leaf(), m_options.spec));
                out.append(k.leaf().decor.suffix);
                return;
            }

            out.append(k.leaf().decor.prefix);
            out.append(formatKeySegment(k.leaf(), m_options.spec));
            out.append(k.leaf().decor.suffix);
        }

        void emitSectionHeader(
            std::string& out,
            const TableStorage& sec,
            bool isAot,
            std::span<const KeySegment> keyPath
        ) const
        {
            out.append(sec.headerDecor.prefix);
            out.append(isAot ? "[[" : "[");
            emitKeyPath(out, keyPath, true);
            out.append(isAot ? "]]" : "]");
            if (!sec.headerDecor.suffix.empty()) {
                out.append(sec.headerDecor.suffix);
            }
            else if (sec.sectionOrder == 0 || hasBodyEntries(sec)) {
                out.append(m_options.newline);
            }
        }

        std::string formatDiagnosticKeyPath(
            std::span<const KeySegment> sectionPath,
            const FlattenedBodyEntry& fe
        ) const
        {
            std::string pathStr;
            auto appendSeg = [&](const KeySegment& seg)
                {
                    if (!pathStr.empty()) {
                        pathStr.push_back('.');
                    }
                    pathStr.append(formatKeySegment(seg, m_options.spec));
                };

            for (const KeySegment& seg : sectionPath) {
                appendSeg(seg);
            }
            const Key& k = fe.pEntry->key;
            if (!k.dottedPrefix().empty()) {
                for (const KeySegment& seg : k.dottedPrefix()) {
                    appendSeg(seg);
                }
            }
            else {
                for (const KeySegment& seg : fe.fallbackDottedPrefix) {
                    appendSeg(seg);
                }
            }
            appendSeg(k.leaf());
            return pathStr;
        }

        void emitSectionBody(
            std::string& out,
            const TableStorage& sec,
            std::span<const KeySegment> sectionPath
        ) const
        {
            std::vector<FlattenedBodyEntry> bodyEntries;
            std::vector<KeySegment> prefixAccum;
            uint32_t seqCounter = 1;
            collectBodyEntries(sec, prefixAccum, 0, seqCounter, bodyEntries);

            std::ranges::stable_sort(bodyEntries, [](const FlattenedBodyEntry& a, const FlattenedBodyEntry& b)
                {
                    if (a.primaryOrder != b.primaryOrder) {
                        return a.primaryOrder < b.primaryOrder;
                    }
                    return a.secondaryOrder < b.secondaryOrder;
                });

            for (size_t i = 0; i < bodyEntries.size(); ++i) {
                if (i > 0 && !out.empty() && !out.ends_with('\n')) {
                    out.append(m_options.newline);
                }

                const FlattenedBodyEntry& fe = bodyEntries[i];
                const Item& val = *fe.pEntry->value;
                if (val.isEmpty()) {
                    throw TypeError(std::format(
                        "Cannot serialize uninitialized 'empty' TOML item at key '{}'",
                        formatDiagnosticKeyPath(sectionPath, fe)
                    ));
                }

                emitEntryKey(out, fe);
                out.push_back('=');
                out.append(val.decor().prefix);
                emitValue(out, val);
                out.append(val.decor().suffix);
            }
        }

        void emitValue(std::string& out, const Item& val) const
        {
            switch (val.kind())
            {
            case ItemKind::Empty:
                throw TypeError("Cannot serialize uninitialized 'empty' TOML item");
            case ItemKind::Array:
                emitStandardArray(out, *val.arrayStorage());
                return;
            case ItemKind::Table:
                emitInlineTable(out, val.tableStorage());
                return;
            default:
                break;
            }

            // 未修改过的标量字面量直接原样回放
            if (!val.rawRepr().empty()) {
                // 若强制以 v1.0 输出，需检查是否存在 v1.1 专属字面量（如省略秒的时间、`\\e`、`\\x`），若无则原样回放
                bool canReplayRaw = true;
                if (m_options.spec == SpecVersion::v1_0) {
                    if (val.isLocalTime() && !val.asLocalTime().hasSeconds) {
                        canReplayRaw = false;
                    }
                    else if (val.isLocalDateTime() && !val.asLocalDateTime().time.hasSeconds) {
                        canReplayRaw = false;
                    }
                    else if (val.isOffsetDateTime() && !val.asOffsetDateTime().time.hasSeconds) {
                        canReplayRaw = false;
                    }
                    else if (val.isString() && (val.rawRepr().contains("\\e") || val.rawRepr().contains("\\x"))) {
                        canReplayRaw = false;
                    }
                }
                if (canReplayRaw) {
                    out.append(val.rawRepr());
                    return;
                }
            }

            switch (val.kind())
            {
            case ItemKind::Boolean:
                out.append(val.asBoolean() ? "true" : "false");
                break;
            case ItemKind::Integer:
                emitInteger(out, val.asInteger(), val.integerRadix());
                break;
            case ItemKind::Float:
                emitFloat(out, val.asFloat());
                break;
            case ItemKind::String:
                emitString(out, val.asString(), val.stringStyle());
                break;
            case ItemKind::LocalDate:
                out.append(val.asLocalDate().toString());
                break;
            case ItemKind::LocalTime:
                out.append(val.asLocalTime().toString(m_options.spec == SpecVersion::v1_0));
                break;
            case ItemKind::LocalDateTime:
                out.append(val.asLocalDateTime().toString(m_options.spec == SpecVersion::v1_0));
                break;
            case ItemKind::OffsetDateTime:
                out.append(val.asOffsetDateTime().toString(m_options.spec == SpecVersion::v1_0));
                break;
            default:
                TKP_UNREACHABLE();
            }
        }

        static void emitInteger(std::string& out, int64_t v, IntegerRadix radix)
        {
            if (v >= 0) {
                switch (radix)
                {
                case IntegerRadix::Hexadecimal:
                    out += std::format("0x{:x}", (uint64_t)v);
                    return;
                case IntegerRadix::Octal:
                    out += std::format("0o{:o}", (uint64_t)v);
                    return;
                case IntegerRadix::Binary:
                    out += std::format("0b{:b}", (uint64_t)v);
                    return;
                case IntegerRadix::Decimal:
                    break;
                }
            }
            out += std::format("{}", v);
        }

        static void emitFloat(std::string& out, double v)
        {
            if (std::isnan(v)) {
                out.append(std::signbit(v) ? "-nan" : "nan");
                return;
            }
            if (std::isinf(v)) {
                out.append(v < 0.0 ? "-inf" : "inf");
                return;
            }

            char buf[64]{};
            const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
            if (ec != std::errc{}) {
                out.append("0.0");
                return;
            }
            const std::string_view sv(buf, (size_t)(ptr - buf));
            out.append(sv);
            if (!sv.contains('.') && !sv.contains('e') && !sv.contains('E')) {
                out.append(".0");
            }
        }

        void emitString(std::string& out, std::string_view s, StringStyle style) const
        {
            if (style == StringStyle::Literal) {
                bool validLiteral = !s.contains('\'') && !s.contains('\n') && !s.contains('\r');
                if (validLiteral) {
                    for (const char ch : s) {
                        const uint8_t b = (uint8_t)ch;
                        if ((b < 0x20u && ch != '\t') || b == 0x7Fu) {
                            validLiteral = false;
                            break;
                        }
                    }
                }
                if (validLiteral) {
                    out.push_back('\'');
                    out.append(s);
                    out.push_back('\'');
                    return;
                }
                style = StringStyle::Basic;
            }
            else if (style == StringStyle::MultilineLiteral) {
                bool validMlLiteral = !s.contains("'''") && !s.ends_with('\'') && !s.contains('\r');
                if (validMlLiteral) {
                    for (const char ch : s) {
                        const uint8_t b = (uint8_t)ch;
                        if ((b < 0x20u && ch != '\t' && ch != '\n') || b == 0x7Fu) {
                            validMlLiteral = false;
                            break;
                        }
                    }
                }
                if (validMlLiteral) {
                    out.append("'''");
                    if (s.starts_with('\n')) {
                        out.push_back('\n');
                    }
                    out.append(s);
                    out.append("'''");
                    return;
                }
                style = StringStyle::MultilineBasic;
            }

            if (style == StringStyle::MultilineBasic) {
                out.append("\"\"\"");
                if (s.starts_with('\n')) {
                    out.push_back('\n');
                }
                out.append(escapeBasicString(s, true, m_options.spec));
                out.append("\"\"\"");
                return;
            }

            out.push_back('"');
            out.append(escapeBasicString(s, false, m_options.spec));
            out.push_back('"');
        }

        void emitStandardArray(std::string& out, const ArrayStorage& arr) const
        {
            out.push_back('[');
            const size_t n = arr.elements.size();
            for (size_t i = 0; i < n; ++i) {
                const ArrayElement& elem = arr.elements[i];
                if (elem.value->isEmpty()) {
                    throw TypeError(std::format(
                        "Cannot serialize uninitialized 'empty' TOML item at array index {}",
                        i
                    ));
                }
                out.append(elem.value->decor().prefix);
                emitValue(out, *elem.value);
                out.append(elem.beforeComma);

                if (i + 1 < n) {
                    out.push_back(',');
                    if (elem.hasComma) {
                        out.append(elem.afterComma);
                    }
                    else if (elem.afterComma.empty() && arr.elements[i + 1].value->decor().prefix.empty()) {
                        out.push_back(' ');
                    }
                }
                else if (elem.hasComma) {
                    out.push_back(',');
                    out.append(elem.afterComma);
                }
            }
            out.append(arr.trailing);
            out.push_back(']');
        }

        void emitInlineTable(std::string& out, const TableStorage* pTbl) const
        {
            out.push_back('{');
            if (pTbl == nullptr) {
                out.push_back('}');
                return;
            }

            std::vector<FlattenedBodyEntry> entries;
            std::vector<KeySegment> prefixAccum;
            uint32_t seqCounter = 1;
            collectBodyEntries(*pTbl, prefixAccum, 0, seqCounter, entries, true);

            std::ranges::stable_sort(entries, [](const FlattenedBodyEntry& a, const FlattenedBodyEntry& b)
                {
                    if (a.primaryOrder != b.primaryOrder) {
                        return a.primaryOrder < b.primaryOrder;
                    }
                    return a.secondaryOrder < b.secondaryOrder;
                });

            const size_t n = entries.size();
            for (size_t i = 0; i < n; ++i) {
                const FlattenedBodyEntry& fe = entries[i];
                const TableEntry& entry = *fe.pEntry;
                const Item& val = *entry.value;
                if (val.isEmpty()) {
                    throw TypeError(std::format(
                        "Cannot serialize uninitialized 'empty' TOML item at key '{}'",
                        formatDiagnosticKeyPath({}, fe)
                    ));
                }

                // 若在 v1.0 模式下序列化含换行的多行内联表，退化为单行空白以生成合法的 v1.0 文本
                if (m_options.spec == SpecVersion::v1_0 && pTbl->multilineInline) {
                    if (i == 0) {
                        out.push_back(' ');
                    }
                    if (!entry.key.dottedPrefix().empty()) {
                        for (const KeySegment& seg : entry.key.dottedPrefix()) {
                            out.append(formatKeySegment(seg, m_options.spec));
                            out.push_back('.');
                        }
                    }
                    else {
                        for (const KeySegment& seg : fe.fallbackDottedPrefix) {
                            out.append(formatKeySegment(seg, m_options.spec));
                            out.push_back('.');
                        }
                    }
                    out.append(formatKeySegment(entry.key.leaf(), m_options.spec));
                    out.append(" = ");
                    emitValue(out, val);
                    if (i + 1 < n) {
                        out.append(", ");
                    }
                    else {
                        out.push_back(' ');
                    }
                    continue;
                }

                emitEntryKey(out, fe);
                out.push_back('=');
                out.append(val.decor().prefix);
                emitValue(out, val);
                out.append(entry.beforeComma);

                if (i + 1 < n) {
                    out.push_back(',');
                    if (entry.hasComma) {
                        out.append(entry.afterComma);
                    }
                    else {
                        const Decor& nextLead = entries[i + 1].pEntry->key.lineLeadDecor();
                        if (entry.afterComma.empty() && nextLead.prefix.empty()) {
                            out.push_back(' ');
                        }
                    }
                }
                else if (entry.hasComma && m_options.spec != SpecVersion::v1_0) {
                    out.push_back(',');
                    out.append(entry.afterComma);
                }
            }

            if (!(m_options.spec == SpecVersion::v1_0 && pTbl->multilineInline)) {
                out.append(pTbl->trailing);
            }
            out.push_back('}');
        }

        FormatOptions m_options{};
    };
}

std::string format(const Item& item, FormatOptions options)
{
    EmitterImpl emitter(options);
    return emitter.emitTopLevel(item);
}

std::string Item::toToml(FormatOptions options) const
{
    return format(*this, options);
}

namespace detail
{
    std::expected<void, ParseError> trySavePathAtomicImpl(
        const fs::path& filePath,
        const Item& doc,
        FormatOptions options
    )
    {
        const std::string filePathUtf8 = pathToUtf8(filePath);
        std::string content;
        try {
            content = format(doc, options);
        }
        catch (const TypeError& ex) {
            return std::unexpected(ParseError{
                .category = ErrorCategory::TypeMismatch,
                .message = ex.what(),
                .sourcePath = filePathUtf8,
                .formattedDiagnostic = std::format("TOML type error: {}", ex.what())
            });
        }

        const fs::path parentDir = filePath.parent_path();
        std::error_code ec;

        if (!parentDir.empty() && !fs::exists(parentDir, ec)) {
            fs::create_directories(parentDir, ec);
            if (ec) {
                const std::string parentUtf8 = pathToUtf8(parentDir);
                const std::string errUtf8 = formatErrorCodeUtf8(ec);
                return std::unexpected(ParseError{
                    .category = ErrorCategory::IoError,
                    .message = std::format("Failed to create parent directory '{}': {}", parentUtf8, errUtf8),
                    .sourcePath = filePathUtf8,
                    .formattedDiagnostic = std::format("TOML I/O error: failed to create parent directory '{}'", parentUtf8)
                });
            }
        }

        fs::path tempPath = filePath;
#ifdef _WIN32
        tempPath += L".__TKP_TEMP__";
#else
        tempPath += ".__TKP_TEMP__";
#endif
        const std::string tempPathUtf8 = pathToUtf8(tempPath);

        {
            std::ofstream ofs(tempPath, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                return std::unexpected(ParseError{
                    .category = ErrorCategory::IoError,
                    .message = std::format("Failed to open temporary file '{}' for atomic write", tempPathUtf8),
                    .sourcePath = filePathUtf8,
                    .formattedDiagnostic = std::format("TOML I/O error: failed to open '{}'", tempPathUtf8)
                });
            }
            if (!content.empty()) {
                ofs.write(content.data(), (std::streamsize)content.size());
            }
            ofs.flush();
            if (!ofs.good()) {
                ofs.close();
                fs::remove(tempPath, ec);
                return std::unexpected(ParseError{
                    .category = ErrorCategory::IoError,
                    .message = std::format("Failed while writing temporary file '{}'", tempPathUtf8),
                    .sourcePath = filePathUtf8,
                    .formattedDiagnostic = std::format("TOML I/O error: write failed on '{}'", tempPathUtf8)
                });
            }
        }

        fs::rename(tempPath, filePath, ec);
        if (ec) {
            const std::string errUtf8 = formatErrorCodeUtf8(ec);
            fs::remove(tempPath, ec);
            return std::unexpected(ParseError{
                .category = ErrorCategory::IoError,
                .message = std::format("Failed to atomically rename temporary file onto '{}': {}", filePathUtf8, errUtf8),
                .sourcePath = filePathUtf8,
                .formattedDiagnostic = std::format("TOML I/O error: rename failed onto '{}'", filePathUtf8)
            });
        }

        return {};
    }

    void savePathAtomicImpl(
        const fs::path& filePath,
        const Item& doc,
        FormatOptions options
    )
    {
        if (auto res = trySavePathAtomicImpl(filePath, doc, options); !res) {
            if (res.error().category == ErrorCategory::TypeMismatch) {
                throw TypeError(res.error().message);
            }
            throw Exception(res.error().message);
        }
    }
}

void saveFileAtomic(
    std::string_view utf8FilePath,
    const Item& doc,
    FormatOptions options
)
{
    detail::savePathAtomicImpl(utf8ToPath(utf8FilePath), doc, options);
}

void saveFileAtomic(
    std::wstring_view wideFilePath,
    const Item& doc,
    FormatOptions options
)
{
    detail::savePathAtomicImpl(fs::path(wideFilePath), doc, options);
}

void saveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options
)
{
    detail::savePathAtomicImpl(fs::path(u8FilePath), doc, options);
}

std::expected<void, ParseError> trySaveFileAtomic(
    std::string_view utf8FilePath,
    const Item& doc,
    FormatOptions options
)
{
    return detail::trySavePathAtomicImpl(utf8ToPath(utf8FilePath), doc, options);
}

std::expected<void, ParseError> trySaveFileAtomic(
    std::wstring_view wideFilePath,
    const Item& doc,
    FormatOptions options
)
{
    return detail::trySavePathAtomicImpl(fs::path(wideFilePath), doc, options);
}

std::expected<void, ParseError> trySaveFileAtomic(
    std::u8string_view u8FilePath,
    const Item& doc,
    FormatOptions options
)
{
    return detail::trySavePathAtomicImpl(fs::path(u8FilePath), doc, options);
}

NAMESPACE_END(tomlkit)
