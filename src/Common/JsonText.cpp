//================================================================================================
// JsonText - JSON reading and writing helpers built on nlohmann::ordered_json
//================================================================================================

#include "JsonText.h"

#include <algorithm>

namespace Canvas
{

namespace
{

using Json = nlohmann::ordered_json;

constexpr size_t kLineWidth = 100;
constexpr size_t kIndent = 2;

// Thrown from the parse callback to stop parsing at a duplicate key.
struct DuplicateKey
{
    size_t ValueOffset;
};

bool IsUtf8Bom(std::string_view text)
{
    return text.size() >= 3
        && uint8_t(text[0]) == 0xEF && uint8_t(text[1]) == 0xBB && uint8_t(text[2]) == 0xBF;
}

// Columns a UTF-8 string takes: one per code point.
size_t Width(std::string_view text)
{
    return size_t(std::count_if(text.begin(), text.end(), [](char c) { return (uint8_t(c) & 0xC0) != 0x80; }));
}

bool IsContainer(const Json &value)
{
    return value.is_array() || value.is_object();
}

// Appends value's one-line form to *pOut. Stops early, returning false, once *pOut has grown by
// more than limit bytes; a UTF-8 code point is at least one byte, so a form longer than limit
// bytes may still fit, but one that stops early is wider than any line it would be checked for.
bool AppendInline(const Json &value, size_t limit, size_t start, std::string *pOut)
{
    if (pOut->size() - start > limit)
        return false;

    if (value.is_array())
    {
        if (value.empty())
        {
            *pOut += "[]";
            return true;
        }
        *pOut += '[';
        bool first = true;
        for (const Json &element : value)
        {
            if (!first)
                *pOut += ", ";
            first = false;
            if (!AppendInline(element, limit, start, pOut))
                return false;
        }
        *pOut += ']';
    }
    else if (value.is_object())
    {
        if (value.empty())
        {
            *pOut += "{}";
            return true;
        }
        *pOut += "{ ";
        bool first = true;
        for (const auto &[key, member] : value.items())
        {
            if (!first)
                *pOut += ", ";
            first = false;
            *pOut += Json(key).dump();
            *pOut += ": ";
            if (!AppendInline(member, limit, start, pOut))
                return false;
        }
        *pOut += " }";
    }
    else
    {
        *pOut += value.dump();
    }
    return pOut->size() - start <= limit;
}

void AppendSpaces(size_t count, std::string *pOut)
{
    pOut->append(count, ' ');
}

// Appends value, which starts at column (0-based) on a line indented by indent. comma is true
// when a comma follows the value on its line.
void AppendValue(const Json &value, size_t indent, size_t column, bool comma, std::string *pOut)
{
    if (!IsContainer(value) || value.empty())
    {
        AppendInline(value, SIZE_MAX, pOut->size(), pOut);
        return;
    }

    // The one-line form, if it fits.
    const size_t start = pOut->size();
    const size_t room = kLineWidth - std::min(kLineWidth, column + (comma ? 1 : 0));
    const size_t byteLimit = room * 4;
    if (AppendInline(value, byteLimit, start, pOut)
        && Width(std::string_view(*pOut).substr(start)) <= room)
    {
        return;
    }
    pOut->resize(start);

    const size_t inner = indent + kIndent;

    if (value.is_array()
        && std::none_of(value.begin(), value.end(), [](const Json &element) { return IsContainer(element); }))
    {
        // Packed: elements fill each line in order.
        *pOut += "[\n";
        AppendSpaces(inner, pOut);
        size_t lineColumn = inner;
        bool lineEmpty = true;
        for (size_t i = 0; i < value.size(); ++i)
        {
            const std::string text = value[i].dump();
            const bool last = i + 1 == value.size();
            const size_t width = Width(text) + (last ? 0 : 1);
            if (!lineEmpty && lineColumn + 1 + width > kLineWidth)
            {
                *pOut += '\n';
                AppendSpaces(inner, pOut);
                lineColumn = inner;
                lineEmpty = true;
            }
            if (!lineEmpty)
            {
                *pOut += ' ';
                ++lineColumn;
            }
            *pOut += text;
            if (!last)
                *pOut += ',';
            lineColumn += width;
            lineEmpty = false;
        }
        *pOut += '\n';
        AppendSpaces(indent, pOut);
        *pOut += ']';
        return;
    }

    // Expanded: one element or member per line.
    *pOut += value.is_array() ? "[\n" : "{\n";
    size_t remaining = value.size();
    if (value.is_array())
    {
        for (const Json &element : value)
        {
            const bool last = --remaining == 0;
            AppendSpaces(inner, pOut);
            AppendValue(element, inner, inner, !last, pOut);
            *pOut += last ? "\n" : ",\n";
        }
    }
    else
    {
        for (const auto &[key, member] : value.items())
        {
            const bool last = --remaining == 0;
            const std::string keyText = Json(key).dump();
            AppendSpaces(inner, pOut);
            *pOut += keyText;
            *pOut += ": ";
            AppendValue(member, inner, inner + Width(keyText) + 2, !last, pOut);
            *pOut += last ? "\n" : ",\n";
        }
    }
    AppendSpaces(indent, pOut);
    *pOut += value.is_array() ? ']' : '}';
}

// The part of an nlohmann parse_error message after its "[json.exception...] parse error at
// line L, column C: " prefix.
std::string ParseErrorMessage(const char *what)
{
    std::string_view message(what);
    const size_t at = message.find("parse error");
    if (at != std::string_view::npos)
    {
        const size_t colon = message.find(": ", at);
        if (colon != std::string_view::npos)
            return std::string(message.substr(colon + 2));
    }
    return std::string(message);
}

} // namespace

bool JsonParse(std::string_view text, nlohmann::ordered_json *pValue, JsonParseError *pError)
{
    // One set of keys per object being read. A member's value is the next value, object end, or
    // array end event at its key's depth; container start events at that depth come before it.
    std::vector<std::vector<std::string>> keyStack;
    int duplicateDepth = -1;

    auto callback = [&](int depth, nlohmann::ordered_json::parse_event_t event, Json &parsed) -> bool
    {
        using Event = nlohmann::ordered_json::parse_event_t;
        switch (event)
        {
        case Event::object_start:
            keyStack.emplace_back();
            break;

        case Event::key:
        {
            const std::string &key = parsed.get_ref<const std::string &>();
            std::vector<std::string> &keys = keyStack.back();
            if (std::find(keys.begin(), keys.end(), key) != keys.end())
                duplicateDepth = depth;
            else
                keys.push_back(key);
            break;
        }

        case Event::object_end:
            keyStack.pop_back();
            [[fallthrough]];
        case Event::array_end:
        case Event::value:
            if (depth == duplicateDepth)
                throw DuplicateKey{ parsed.start_pos() };
            break;

        default:
            break;
        }
        return true;
    };

    try
    {
        Json value = Json::parse(text.begin(), text.end(), callback);
        *pValue = std::move(value);
        return true;
    }
    catch (const DuplicateKey &duplicate)
    {
        pError->Offset = JsonKeyOffset(text, duplicate.ValueOffset);
        pError->Message = "duplicate key";
        return false;
    }
    catch (const nlohmann::ordered_json::parse_error &error)
    {
        pError->Offset = std::min(error.byte == 0 ? 0 : error.byte - 1, text.size());
        pError->Message = ParseErrorMessage(error.what());
        return false;
    }
}

CJsonLineIndex::CJsonLineIndex(std::string_view text) :
    m_Text(text)
{
    m_LineStarts.push_back(0);
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n')
            m_LineStarts.push_back(i + 1);
    }
}

JsonPosition CJsonLineIndex::Locate(size_t offset) const
{
    offset = std::min(offset, m_Text.size());
    const auto next = std::upper_bound(m_LineStarts.begin(), m_LineStarts.end(), offset);
    const size_t line = size_t(next - m_LineStarts.begin());
    size_t lineStart = *(next - 1);
    if (lineStart == 0 && IsUtf8Bom(m_Text))
        lineStart = std::min<size_t>(3, offset);

    JsonPosition position;
    position.Line = uint32_t(line);
    position.Column = uint32_t(1 + Width(m_Text.substr(lineStart, offset - lineStart)));
    return position;
}

size_t JsonKeyOffset(std::string_view text, size_t valueOffset)
{
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

    size_t i = std::min(valueOffset, text.size());
    while (i > 0 && isSpace(text[i - 1]))
        --i;
    if (i == 0 || text[i - 1] != ':')
        return valueOffset;
    --i;
    while (i > 0 && isSpace(text[i - 1]))
        --i;
    if (i == 0 || text[i - 1] != '"')
        return valueOffset;
    --i;

    // i is the closing quote. The opening quote is the nearest quote before it that is preceded
    // by an even number of backslashes; a quote inside a key is always escaped.
    while (i > 0)
    {
        --i;
        if (text[i] != '"')
            continue;
        size_t backslashes = 0;
        while (backslashes < i && text[i - 1 - backslashes] == '\\')
            ++backslashes;
        if (backslashes % 2 == 0)
            return i;
    }
    return valueOffset;
}

void JsonPointerAppend(std::string *pPointer, std::string_view key)
{
    *pPointer += '/';
    for (char c : key)
    {
        if (c == '~')
            *pPointer += "~0";
        else if (c == '/')
            *pPointer += "~1";
        else
            *pPointer += c;
    }
}

void JsonPointerAppend(std::string *pPointer, size_t index)
{
    *pPointer += '/';
    *pPointer += std::to_string(index);
}

std::string JsonFormat(const nlohmann::ordered_json &value)
{
    std::string text;
    AppendValue(value, 0, 0, false, &text);
    text += '\n';
    return text;
}

} // namespace Canvas
