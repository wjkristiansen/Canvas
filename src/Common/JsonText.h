//================================================================================================
// JsonText - JSON reading and writing helpers built on nlohmann::ordered_json
//
// Reading: JsonParse reads strict RFC 8259 JSON, rejects duplicate keys, and records each value's
// byte offset in the text (nlohmann's start_pos, enabled by JSON_DIAGNOSTIC_POSITIONS).
// CJsonLineIndex turns a byte offset into a line and column, and JsonKeyOffset finds the key of
// an object member from its value's offset.
//
// Writing: JsonFormat lays out a value by these rules, so a value always produces the same text:
// - Two spaces of indent per level, lines of at most 100 columns, a line feed after every line,
//   the last one included. Columns count code points.
// - Scalars are written as nlohmann's dump writes them: strings in UTF-8 with only '"', '\',
//   and control characters escaped, and numbers in their shortest round-trip form.
// - An empty array or object is written "[]" or "{}".
// - An array or object whose one-line form fits on its line, including a comma that follows it,
//   is written on one line: "[1, 2]", "{ "a": 1 }".
// - Otherwise an array of scalars is packed: its elements fill lines in order, a line holding as
//   many as fit, and one that fits on no line alone.
// - Otherwise each element or member is on its own line.
//
// These helpers throw what nlohmann and the standard library throw, such as std::bad_alloc.
//================================================================================================

#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace Canvas
{

struct JsonPosition
{
    uint32_t Line = 0;      // from 1
    uint32_t Column = 0;    // from 1, in code points
};

struct JsonParseError
{
    size_t Offset = 0;      // byte offset in the text where reading stopped
    std::string Message;
};

// Parses text into *pValue. Returns false on a syntax error or a duplicate key, the error at the
// duplicate key's opening quote, and leaves *pValue unchanged.
bool JsonParse(std::string_view text, nlohmann::ordered_json *pValue, JsonParseError *pError);

// Maps byte offsets in one text to lines and columns. A line ends at a line feed, and a
// carriage return before a line feed is part of the line ending. A byte order mark at the start
// of the text takes no column.
class CJsonLineIndex
{
public:
    explicit CJsonLineIndex(std::string_view text);

    JsonPosition Locate(size_t offset) const;

private:
    std::string_view m_Text;
    std::vector<size_t> m_LineStarts;
};

// Returns the offset of the opening quote of the key whose member value starts at valueOffset.
size_t JsonKeyOffset(std::string_view text, size_t valueOffset);

// Appends a reference token to a JSON Pointer, escaping '~' and '/'.
void JsonPointerAppend(std::string *pPointer, std::string_view key);
void JsonPointerAppend(std::string *pPointer, size_t index);

// Calls report(key, value) for each member of object whose key is not one of knownKeys, in
// member order.
template<typename TReport>
void JsonForEachUnknownKey(const nlohmann::ordered_json &object, std::initializer_list<std::string_view> knownKeys, TReport &&report)
{
    for (const auto &[key, value] : object.items())
    {
        bool known = false;
        for (std::string_view knownKey : knownKeys)
        {
            if (key == knownKey)
            {
                known = true;
                break;
            }
        }
        if (!known)
            report(key, value);
    }
}

// Returns value laid out by the rules above.
std::string JsonFormat(const nlohmann::ordered_json &value);

} // namespace Canvas
