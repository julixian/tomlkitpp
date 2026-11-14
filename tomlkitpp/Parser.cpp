module;

#include "TKPMacros.hpp"

module tomlkitpp;

import :Lexer;
import :Parser;

namespace fs = std::filesystem;

NAMESPACE_BEGIN(tomlkit)

namespace
{
    void sealTableRecursively(Item& item) noexcept
    {
        if (auto* pTbl = item.tableStorage()) {
            pTbl->isSealed = true;
            for (auto& pEntry : pTbl->entries) {
                sealTableRecursively(*pEntry->value);
            }
        }
        else if (auto* pArr = item.arrayStorage()) {
            for (auto& elem : pArr->elements) {
                sealTableRecursively(*elem.value);
            }
        }
    }

    class ParserImpl
    {
    public:
        ParserImpl(std::string_view source, ParseOptions options, std::string_view sourcePath) noexcept
            : m_lexer(source, options, sourcePath)
            , m_options(options)
        { }

        std::expected<Item, ParseError> parseDocument()
        {
            Item doc = Item::document();
            TableStorage* const pRoot = doc.tableStorage();
            pRoot->isExplicitlyDefined = true;
            pRoot->dottedKeyScopeId = 1;

            const auto bomRes = m_lexer.validateUtf8AndConsumeBom();
            if (!bomRes) {
                return std::unexpected(std::move(bomRes.error()));
            }
            pRoot->hasBom = *bomRes;

            TableStorage* pCurrentSection = pRoot;
            uint32_t currentScopeId = 1;

            while (!m_lexer.isEof()) {
                const auto leadTriviaRes = m_lexer.consumeWsCommentNewlines();
                if (!leadTriviaRes) {
                    return std::unexpected(std::move(leadTriviaRes.error()));
                }

                if (m_lexer.isEof()) {
                    if (pRoot->entries.empty() && m_nextSectionOrder == 1) {
                        pRoot->headerDecor.prefix = std::string(*leadTriviaRes);
                    }
                    else {
                        pRoot->trailing = std::string(*leadTriviaRes);
                    }
                    break;
                }

                if (m_lexer.peek() == '[') {
                    auto secRes = parseSectionHeader(pRoot, *leadTriviaRes);
                    if (!secRes) {
                        return std::unexpected(std::move(secRes.error()));
                    }
                    pCurrentSection = *secRes;
                    currentScopeId = m_nextScopeId++;
                }
                else {
                    if (auto kvRes = parseSectionKeyValue(pCurrentSection, currentScopeId, *leadTriviaRes); !kvRes) {
                        return std::unexpected(std::move(kvRes.error()));
                    }
                }
            }

            return doc;
        }

    private:
        std::expected<TableStorage*, ParseError> parseSectionHeader(TableStorage* pRoot, std::string_view leadTrivia)
        {
            const size_t headerOffset = m_lexer.position();
            m_lexer.advance(); // 消费首个 '['

            bool isAot = false;
            if (m_lexer.peek() == '[') {
                isAot = true;
                m_lexer.advance(); // 消费第二个 '['
            }

            const std::string_view afterOpenWs = m_lexer.consumeHorizontalWs();
            if (m_lexer.peek() == ']') {
                return std::unexpected(m_lexer.errorAt(
                    ErrorCategory::InvalidKey,
                    headerOffset,
                    (uint32_t)(m_lexer.position() - headerOffset + 1),
                    "Empty table header '[]' is not allowed"
                ));
            }

            auto keySegsRes = m_lexer.lexDottedKey();
            if (!keySegsRes) {
                return std::unexpected(std::move(keySegsRes.error()));
            }
            auto keySegs = std::move(*keySegsRes);
            keySegs.front().decor.prefix = std::string(afterOpenWs);

            if (isAot) {
                if (m_lexer.peek(0) != ']' || m_lexer.peek(1) != ']') {
                    return std::unexpected(m_lexer.errorHere(
                        ErrorCategory::UnexpectedCharacter,
                        "Expected closing ']]' for array-of-tables header"
                    ));
                }
                m_lexer.advance();
                m_lexer.advance();
            }
            else {
                if (m_lexer.peek() != ']') {
                    return std::unexpected(m_lexer.errorHere(
                        ErrorCategory::UnexpectedCharacter,
                        "Expected closing ']' for table header"
                    ));
                }
                m_lexer.advance();
            }

            const auto lineTrailRes = m_lexer.consumeLineTrail();
            if (!lineTrailRes) {
                return std::unexpected(std::move(lineTrailRes.error()));
            }

            // 沿路径前 N-1 段向下定位或创建隐式父表
            TableStorage* pCur = pRoot;
            for (size_t i = 0; i + 1 < keySegs.size(); ++i) {
                const std::string_view segName = keySegs[i].name;
                const size_t foundIdx = pCur->findIndex(segName);
                if (foundIdx == std::string_view::npos) {
                    Item implicitTbl = Item::table(TableStyle::ImplicitSuper);
                    TableStorage* const pSub = implicitTbl.tableStorage();
                    pSub->isExplicitlyDefined = false;
                    pSub->dottedKeyScopeId = 0;

                    const uint32_t order = pCur->nextEntryOrder++;
                    pCur->entries.push_back(std::make_unique<TableEntry>(
                        Key{ std::string(segName), keySegs[i].style },
                        std::move(implicitTbl),
                        order
                    ));
                    pCur->invalidateIndex();
                    pCur = pSub;
                }
                else {
                    Item& existing = *pCur->entries[foundIdx]->value;
                    if (auto* pSubTbl = existing.tableStorage()) {
                        if (pSubTbl->isSealed) {
                            return std::unexpected(m_lexer.errorAt(
                                ErrorCategory::InlineTableImmutable,
                                headerOffset,
                                (uint32_t)(m_lexer.position() - headerOffset),
                                std::format("Cannot extend inline table '{}' via section header", segName)
                            ));
                        }
                        pCur = pSubTbl;
                    }
                    else if (auto* pArr = existing.arrayStorage()) {
                        if (pArr->style != ArrayStyle::ArrayOfTables || pArr->elements.empty()) {
                            return std::unexpected(m_lexer.errorAt(
                                ErrorCategory::StaticArrayConflict,
                                headerOffset,
                                (uint32_t)(m_lexer.position() - headerOffset),
                                std::format("Cannot append sub-table under static array '{}'", segName)
                            ));
                        }
                        pCur = pArr->elements.back().value->tableStorage();
                    }
                    else {
                        return std::unexpected(m_lexer.errorAt(
                            ErrorCategory::TableRedefinition,
                            headerOffset,
                            (uint32_t)(m_lexer.position() - headerOffset),
                            std::format("Cannot redefine non-table key '{}' as super-table", segName)
                        ));
                    }
                }
            }

            const KeySegment& leafSeg = keySegs.back();
            const size_t leafIdx = pCur->findIndex(leafSeg.name);

            if (!isAot) {
                TableStorage* pTarget = nullptr;
                if (leafIdx == std::string_view::npos) {
                    Item stdTbl = Item::table(TableStyle::Standard);
                    pTarget = stdTbl.tableStorage();
                    const uint32_t order = pCur->nextEntryOrder++;
                    pCur->entries.push_back(std::make_unique<TableEntry>(
                        Key{ leafSeg.name, leafSeg.style },
                        std::move(stdTbl),
                        order
                    ));
                    pCur->invalidateIndex();
                }
                else {
                    Item& existing = *pCur->entries[leafIdx]->value;
                    auto* pExistingTbl = existing.tableStorage();
                    if (pExistingTbl == nullptr || pExistingTbl->isSealed || pExistingTbl->isExplicitlyDefined) {
                        return std::unexpected(m_lexer.errorAt(
                            ErrorCategory::TableRedefinition,
                            headerOffset,
                            (uint32_t)(m_lexer.position() - headerOffset),
                            std::format("Table '{}' cannot be defined more than once", leafSeg.name)
                        ));
                    }
                    pExistingTbl->style = TableStyle::Standard;
                    pTarget = pExistingTbl;
                }

                pTarget->isExplicitlyDefined = true;
                pTarget->dottedKeyScopeId = 0;
                pTarget->sectionOrder = m_nextSectionOrder++;
                pTarget->headerDecor.prefix = std::string(leadTrivia);
                pTarget->headerDecor.suffix = std::string(*lineTrailRes);
                pTarget->headerKeyPath = std::move(keySegs);
                return pTarget;
            }

            // `[[array_of_tables]]`
            ArrayStorage* pAot = nullptr;
            if (leafIdx == std::string_view::npos) {
                Item aotItem = Item::arrayOfTables();
                pAot = aotItem.arrayStorage();
                const uint32_t order = pCur->nextEntryOrder++;
                pCur->entries.push_back(std::make_unique<TableEntry>(
                    Key{ leafSeg.name, leafSeg.style },
                    std::move(aotItem),
                    order
                ));
                pCur->invalidateIndex();
            }
            else {
                Item& existing = *pCur->entries[leafIdx]->value;
                pAot = existing.arrayStorage();
                if (pAot == nullptr || pAot->style != ArrayStyle::ArrayOfTables) {
                    return std::unexpected(m_lexer.errorAt(
                        ErrorCategory::StaticArrayConflict,
                        headerOffset,
                        (uint32_t)(m_lexer.position() - headerOffset),
                        std::format("Cannot define array-of-tables '[[{}]]'; key is already defined as {}", leafSeg.name, existing.kindName())
                    ));
                }
            }

            Item elemTbl = Item::table(TableStyle::ArrayOfTablesElement);
            TableStorage* const pTarget = elemTbl.tableStorage();
            pTarget->isExplicitlyDefined = true;
            pTarget->dottedKeyScopeId = 0;
            pTarget->sectionOrder = m_nextSectionOrder++;
            pTarget->headerDecor.prefix = std::string(leadTrivia);
            pTarget->headerDecor.suffix = std::string(*lineTrailRes);
            pTarget->headerKeyPath = std::move(keySegs);

            pAot->elements.emplace_back(std::move(elemTbl));
            return pTarget;
        }

        std::expected<TableEntry*, ParseError> insertDottedKeyValue(
            TableStorage* pSectionTable,
            uint32_t scopeId,
            std::vector<KeySegment> keySegs,
            Item value,
            size_t statementOffset
        )
        {
            const uint32_t sectionBodyOrder = pSectionTable->nextEntryOrder++;
            TableStorage* pCur = pSectionTable;

            for (size_t i = 0; i + 1 < keySegs.size(); ++i) {
                const std::string_view segName = keySegs[i].name;
                const size_t foundIdx = pCur->findIndex(segName);

                if (foundIdx == std::string_view::npos) {
                    Item dottedTbl = Item::table(TableStyle::Dotted);
                    TableStorage* const pSub = dottedTbl.tableStorage();
                    pSub->isExplicitlyDefined = true;
                    pSub->dottedKeyScopeId = scopeId;

                    pCur->entries.push_back(std::make_unique<TableEntry>(
                        Key{ std::string(segName), keySegs[i].style },
                        std::move(dottedTbl),
                        sectionBodyOrder
                    ));
                    pCur->invalidateIndex();
                    pCur = pSub;
                }
                else {
                    Item& existing = *pCur->entries[foundIdx]->value;
                    auto* pSubTbl = existing.tableStorage();
                    if (pSubTbl == nullptr) {
                        return std::unexpected(m_lexer.errorAt(
                            ErrorCategory::DottedKeyConflict,
                            statementOffset,
                            1,
                            std::format("Cannot use dotted key through non-table key '{}'", segName)
                        ));
                    }
                    if (pSubTbl->isSealed) {
                        return std::unexpected(m_lexer.errorAt(
                            ErrorCategory::InlineTableImmutable,
                            statementOffset,
                            1,
                            std::format("Cannot append keys to closed inline table '{}'", segName)
                        ));
                    }
                    if (pSubTbl->isExplicitlyDefined && pSubTbl->dottedKeyScopeId != scopeId) {
                        return std::unexpected(m_lexer.errorAt(
                            ErrorCategory::DottedKeyConflict,
                            statementOffset,
                            1,
                            std::format("Cannot use dotted keys to append into table '{}' defined outside this block", segName)
                        ));
                    }
                    // 若此前仅作为后代 `[section]` 的隐式父表创建（`ImplicitSuper`），
                    // 在祖先表内首次通过点分键穿透赋值时需转为 `Dotted`，避免序列化时重复拆出独立 `[header]`
                    if (pSubTbl->style == TableStyle::ImplicitSuper) {
                        pSubTbl->style = TableStyle::Dotted;
                    }
                    pSubTbl->isExplicitlyDefined = true;
                    pSubTbl->dottedKeyScopeId = scopeId;
                    pCur = pSubTbl;
                }
            }

            KeySegment leafSeg = std::move(keySegs.back());
            keySegs.pop_back();

            if (pCur->findIndex(leafSeg.name) != std::string_view::npos) {
                return std::unexpected(m_lexer.errorAt(
                    ErrorCategory::DuplicateKey,
                    statementOffset,
                    (uint32_t)std::max<size_t>(1, leafSeg.rawRepr.size()),
                    std::format("Duplicate key '{}'", leafSeg.name)
                ));
            }

            Key entryKey{ std::move(keySegs), std::move(leafSeg) };
            pCur->entries.push_back(std::make_unique<TableEntry>(
                std::move(entryKey),
                std::move(value),
                sectionBodyOrder
            ));
            pCur->invalidateIndex();
            return pCur->entries.back().get();
        }

        std::expected<void, ParseError> parseSectionKeyValue(
            TableStorage* pSectionTable,
            uint32_t scopeId,
            std::string_view leadTrivia
        )
        {
            const size_t stmtOffset = m_lexer.position();
            auto keySegsRes = m_lexer.lexDottedKey();
            if (!keySegsRes) {
                return std::unexpected(std::move(keySegsRes.error()));
            }
            auto keySegs = std::move(*keySegsRes);
            keySegs.front().decor.prefix = std::string(leadTrivia);

            if (m_lexer.peek() != '=') {
                return std::unexpected(m_lexer.errorHere(
                    ErrorCategory::UnexpectedCharacter,
                    "Expected '=' after key"
                ));
            }
            m_lexer.advance(); // 消费 '='

            const std::string_view afterEqWs = m_lexer.consumeHorizontalWs();
            auto valRes = parseValue(1);
            if (!valRes) {
                return std::unexpected(std::move(valRes.error()));
            }

            const auto lineTrailRes = m_lexer.consumeLineTrail();
            if (!lineTrailRes) {
                return std::unexpected(std::move(lineTrailRes.error()));
            }

            valRes->decor().prefix = std::string(afterEqWs);
            valRes->decor().suffix = std::string(*lineTrailRes);

            auto insertRes = insertDottedKeyValue(pSectionTable, scopeId, std::move(keySegs), std::move(*valRes), stmtOffset);
            if (!insertRes) {
                return std::unexpected(std::move(insertRes.error()));
            }
            return {};
        }

        std::expected<Item, ParseError> parseValue(uint32_t depth)
        {
            if (depth > m_options.maxNestingDepth) {
                return std::unexpected(m_lexer.errorHere(
                    ErrorCategory::NestingLimitExceeded,
                    std::format("Value exceeds maximum nesting depth ({})", m_options.maxNestingDepth)
                ));
            }

            if (m_lexer.isEof()) {
                return std::unexpected(m_lexer.errorHere(
                    ErrorCategory::UnexpectedEof,
                    "Unexpected end of file while expecting a value"
                ));
            }

            const char ch = m_lexer.peek();
            if (ch == '"' || ch == '\'') {
                auto strRes = m_lexer.lexString();
                if (!strRes) {
                    return std::unexpected(std::move(strRes.error()));
                }
                Item item(std::move(strRes->value));
                item.setStringStyle(strRes->style);
                item.setRawRepr(std::string(strRes->rawRepr));
                return item;
            }

            if (ch == '[') {
                return parseArray(depth + 1);
            }

            if (ch == '{') {
                return parseInlineTable(depth + 1);
            }

            auto scalarRes = m_lexer.lexScalar();
            if (!scalarRes) {
                return std::unexpected(std::move(scalarRes.error()));
            }

            Item item = std::visit([](auto&& v) -> Item
                {
                    return Item(std::forward<decltype(v)>(v));
                }, scalarRes->payload);
            item.setIntegerRadix(scalarRes->integerRadix);
            item.setRawRepr(std::string(scalarRes->rawRepr));
            return item;
        }

        std::expected<Item, ParseError> parseArray(uint32_t depth)
        {
            const size_t openOffset = m_lexer.position();
            m_lexer.advance(); // 消费 '['

            Item arrItem = Item::array();
            ArrayStorage* const pArr = arrItem.arrayStorage();
            pArr->hasExplicitLayout = true;

            while (true) {
                const auto prefixRes = m_lexer.consumeWsCommentNewlines();
                if (!prefixRes) {
                    return std::unexpected(std::move(prefixRes.error()));
                }

                if (m_lexer.isEof()) {
                    return std::unexpected(m_lexer.errorAt(
                        ErrorCategory::UnexpectedEof,
                        openOffset,
                        1,
                        "Unterminated array '['"
                    ));
                }

                if (m_lexer.peek() == ']') {
                    m_lexer.advance();
                    pArr->trailing = std::string(*prefixRes);
                    if (pArr->trailing.contains('\n')) {
                        pArr->multiline = true;
                    }
                    return arrItem;
                }

                if (m_lexer.peek() == ',') {
                    return std::unexpected(m_lexer.errorHere(
                        ErrorCategory::UnexpectedCharacter,
                        "Unexpected comma ',' in array without preceding element"
                    ));
                }

                if (prefixRes->contains('\n')) {
                    pArr->multiline = true;
                }

                auto elemValRes = parseValue(depth);
                if (!elemValRes) {
                    return std::unexpected(std::move(elemValRes.error()));
                }
                elemValRes->decor().prefix = std::string(*prefixRes);

                const auto postValTriviaRes = m_lexer.consumeWsCommentNewlines();
                if (!postValTriviaRes) {
                    return std::unexpected(std::move(postValTriviaRes.error()));
                }

                if (m_lexer.isEof()) {
                    return std::unexpected(m_lexer.errorAt(
                        ErrorCategory::UnexpectedEof,
                        openOffset,
                        1,
                        "Unterminated array '['"
                    ));
                }

                ArrayElement elem(std::move(*elemValRes));

                if (m_lexer.peek() == ',') {
                    m_lexer.advance();
                    elem.hasComma = true;
                    elem.beforeComma = std::string(*postValTriviaRes);

                    const auto afterCommaRes = m_lexer.consumeAfterCommaSameLine();
                    if (!afterCommaRes) {
                        return std::unexpected(std::move(afterCommaRes.error()));
                    }
                    elem.afterComma = std::string(*afterCommaRes);
                    if (elem.beforeComma.contains('\n') || elem.afterComma.contains('\n')) {
                        pArr->multiline = true;
                    }
                    pArr->elements.push_back(std::move(elem));
                    continue;
                }

                if (m_lexer.peek() == ']') {
                    m_lexer.advance();
                    elem.hasComma = false;

                    // 若 postValTrivia 含多行，首行（紧跟元素的行尾注释 + 首个 `\n`）归属该元素 `beforeComma`，后续行归属容器 `trailing`
                    const std::string_view postSv = *postValTriviaRes;
                    if (const size_t firstNl = postSv.find('\n'); firstNl != std::string_view::npos) {
                        pArr->multiline = true;
                        elem.beforeComma = std::string(postSv.substr(0, firstNl + 1));
                        pArr->trailing = std::string(postSv.substr(firstNl + 1));
                    }
                    else {
                        pArr->trailing = std::string(postSv);
                    }

                    pArr->elements.push_back(std::move(elem));
                    return arrItem;
                }

                return std::unexpected(m_lexer.errorHere(
                    ErrorCategory::UnexpectedCharacter,
                    std::format("Expected ',' or ']' in array, found '{}'", m_lexer.peek())
                ));
            }
        }

        std::expected<Item, ParseError> parseInlineTable(uint32_t depth)
        {
            const size_t openOffset = m_lexer.position();
            m_lexer.advance(); // 消费 '{'

            Item tblItem = Item::inlineTable();
            TableStorage* const pTbl = tblItem.tableStorage();
            const uint32_t inlineScopeId = m_nextScopeId++;
            pTbl->isExplicitlyDefined = true;
            pTbl->dottedKeyScopeId = inlineScopeId;

            const auto checkV10TriviaFunc = [&](std::string_view trivia, size_t offset) -> std::expected<void, ParseError>
                {
                    if (m_options.spec == SpecVersion::v1_0 && (trivia.contains('\n') || trivia.contains('#'))) {
                        return std::unexpected(m_lexer.errorAt(
                            ErrorCategory::SpecRestriction,
                            offset,
                            (uint32_t)std::max<size_t>(1, trivia.size()),
                            "Newlines and comments inside inline tables require TOML v1.1"
                        ));
                    }
                    return {};
                };

            bool afterTrailingComma = false;

            while (true) {
                const size_t triviaStart = m_lexer.position();
                const auto prefixRes = m_lexer.consumeWsCommentNewlines();
                if (!prefixRes) {
                    return std::unexpected(std::move(prefixRes.error()));
                }
                if (auto v10Res = checkV10TriviaFunc(*prefixRes, triviaStart); !v10Res) {
                    return std::unexpected(std::move(v10Res.error()));
                }

                if (m_lexer.isEof()) {
                    return std::unexpected(m_lexer.errorAt(
                        ErrorCategory::UnexpectedEof,
                        openOffset,
                        1,
                        "Unterminated inline table '{'"
                    ));
                }

                if (m_lexer.peek() == '}') {
                    if (afterTrailingComma && m_options.spec == SpecVersion::v1_0) {
                        return std::unexpected(m_lexer.errorHere(
                            ErrorCategory::SpecRestriction,
                            "Trailing comma in inline table requires TOML v1.1"
                        ));
                    }
                    m_lexer.advance();
                    pTbl->trailing = std::string(*prefixRes);
                    if (pTbl->trailing.contains('\n')) {
                        pTbl->multilineInline = true;
                    }
                    sealTableRecursively(tblItem);
                    return tblItem;
                }

                if (m_lexer.peek() == ',') {
                    return std::unexpected(m_lexer.errorHere(
                        ErrorCategory::UnexpectedCharacter,
                        "Unexpected comma ',' in inline table"
                    ));
                }

                if (prefixRes->contains('\n')) {
                    pTbl->multilineInline = true;
                }

                const size_t kvOffset = m_lexer.position();
                auto keySegsRes = m_lexer.lexDottedKey();
                if (!keySegsRes) {
                    return std::unexpected(std::move(keySegsRes.error()));
                }
                auto keySegs = std::move(*keySegsRes);
                keySegs.front().decor.prefix = std::string(*prefixRes);

                if (m_lexer.peek() != '=') {
                    return std::unexpected(m_lexer.errorHere(
                        ErrorCategory::UnexpectedCharacter,
                        "Expected '=' after key in inline table"
                    ));
                }
                m_lexer.advance(); // 消费 '='

                const std::string_view afterEqWs = m_lexer.consumeHorizontalWs();
                auto valRes = parseValue(depth);
                if (!valRes) {
                    return std::unexpected(std::move(valRes.error()));
                }
                valRes->decor().prefix = std::string(afterEqWs);

                const size_t postTriviaStart = m_lexer.position();
                const auto postValTriviaRes = m_lexer.consumeWsCommentNewlines();
                if (!postValTriviaRes) {
                    return std::unexpected(std::move(postValTriviaRes.error()));
                }
                if (auto v10Res = checkV10TriviaFunc(*postValTriviaRes, postTriviaStart); !v10Res) {
                    return std::unexpected(std::move(v10Res.error()));
                }

                if (m_lexer.isEof()) {
                    return std::unexpected(m_lexer.errorAt(
                        ErrorCategory::UnexpectedEof,
                        openOffset,
                        1,
                        "Unterminated inline table '{'"
                    ));
                }

                auto entryRes = insertDottedKeyValue(pTbl, inlineScopeId, std::move(keySegs), std::move(*valRes), kvOffset);
                if (!entryRes) {
                    return std::unexpected(std::move(entryRes.error()));
                }
                TableEntry* const pEntry = *entryRes;

                if (m_lexer.peek() == ',') {
                    const size_t commaPos = m_lexer.position();
                    m_lexer.advance();
                    pEntry->hasComma = true;
                    pEntry->beforeComma = std::string(*postValTriviaRes);

                    const size_t afterCommaStart = m_lexer.position();
                    const auto afterCommaRes = m_lexer.consumeAfterCommaSameLine();
                    if (!afterCommaRes) {
                        return std::unexpected(std::move(afterCommaRes.error()));
                    }
                    if (auto v10Res = checkV10TriviaFunc(*afterCommaRes, afterCommaStart); !v10Res) {
                        return std::unexpected(std::move(v10Res.error()));
                    }
                    pEntry->afterComma = std::string(*afterCommaRes);
                    if (pEntry->beforeComma.contains('\n') || pEntry->afterComma.contains('\n')) {
                        pTbl->multilineInline = true;
                    }
                    afterTrailingComma = true;
                    (void)commaPos;
                    continue;
                }

                if (m_lexer.peek() == '}') {
                    m_lexer.advance();
                    pEntry->hasComma = false;

                    const std::string_view postSv = *postValTriviaRes;
                    if (const size_t firstNl = postSv.find('\n'); firstNl != std::string_view::npos) {
                        pTbl->multilineInline = true;
                        pEntry->beforeComma = std::string(postSv.substr(0, firstNl + 1));
                        pTbl->trailing = std::string(postSv.substr(firstNl + 1));
                    }
                    else {
                        pTbl->trailing = std::string(postSv);
                    }

                    sealTableRecursively(tblItem);
                    return tblItem;
                }

                return std::unexpected(m_lexer.errorHere(
                    ErrorCategory::UnexpectedCharacter,
                    std::format("Expected ',' or '}}' in inline table, found '{}'", m_lexer.peek())
                ));
            }
        }

        detail::Lexer m_lexer;
        ParseOptions m_options{};
        uint32_t m_nextSectionOrder = 1;
        uint32_t m_nextScopeId = 2;
    };
}

std::expected<Item, ParseError> tryParse(
    std::string_view utf8Source,
    ParseOptions options,
    std::string_view sourcePath
)
{
    ParserImpl parser(utf8Source, options, sourcePath);
    return parser.parseDocument();
}

Item parse(
    std::string_view utf8Source,
    ParseOptions options,
    std::string_view sourcePath
)
{
    auto res = tryParse(utf8Source, options, sourcePath);
    if (!res) {
        throw SyntaxError(std::move(res.error()));
    }
    return std::move(*res);
}

namespace detail
{
    std::expected<Item, ParseError> tryParsePathImpl(
        const fs::path& filePath,
        ParseOptions options
    )
    {
        std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
        const std::string pathStr = pathToUtf8(filePath);
        if (!ifs.is_open()) {
            return std::unexpected(ParseError{
                .category = ErrorCategory::IoError,
                .location = SourceLocation{},
                .message = std::format("Failed to open TOML file '{}'", pathStr),
                .sourcePath = pathStr,
                .formattedDiagnostic = std::format("TOML I/O error: failed to open file '{}'", pathStr)
            });
        }

        const std::streamsize fileSize = ifs.tellg();
        if (fileSize < 0) {
            return std::unexpected(ParseError{
                .category = ErrorCategory::IoError,
                .location = SourceLocation{},
                .message = std::format("Failed to determine size of TOML file '{}'", pathStr),
                .sourcePath = pathStr,
                .formattedDiagnostic = std::format("TOML I/O error: failed to read size of '{}'", pathStr)
            });
        }

        std::string buffer((size_t)fileSize, '\0');
        ifs.seekg(0, std::ios::beg);
        if (fileSize > 0 && !ifs.read(buffer.data(), fileSize)) {
            return std::unexpected(ParseError{
                .category = ErrorCategory::IoError,
                .location = SourceLocation{},
                .message = std::format("Failed while reading TOML file '{}'", pathStr),
                .sourcePath = pathStr,
                .formattedDiagnostic = std::format("TOML I/O error: failed while reading '{}'", pathStr)
            });
        }

        return tryParse(buffer, options, pathStr);
    }

    Item parsePathImpl(
        const fs::path& filePath,
        ParseOptions options
    )
    {
        auto res = tryParsePathImpl(filePath, options);
        if (!res) {
            throw SyntaxError(std::move(res.error()));
        }
        return std::move(*res);
    }
}

std::expected<Item, ParseError> tryParseFile(
    std::string_view utf8FilePath,
    ParseOptions options
)
{
    return detail::tryParsePathImpl(utf8ToPath(utf8FilePath), options);
}

std::expected<Item, ParseError> tryParseFile(
    std::wstring_view wideFilePath,
    ParseOptions options
)
{
    return detail::tryParsePathImpl(fs::path(wideFilePath), options);
}

std::expected<Item, ParseError> tryParseFile(
    std::u8string_view u8FilePath,
    ParseOptions options
)
{
    return detail::tryParsePathImpl(fs::path(u8FilePath), options);
}

Item parseFile(
    std::string_view utf8FilePath,
    ParseOptions options
)
{
    return detail::parsePathImpl(utf8ToPath(utf8FilePath), options);
}

Item parseFile(
    std::wstring_view wideFilePath,
    ParseOptions options
)
{
    return detail::parsePathImpl(fs::path(wideFilePath), options);
}

Item parseFile(
    std::u8string_view u8FilePath,
    ParseOptions options
)
{
    return detail::parsePathImpl(fs::path(u8FilePath), options);
}

NAMESPACE_END(tomlkit)
