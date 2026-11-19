//================================================================================================
// JsonTextTest - Tests for the JSON reading and writing helpers
//================================================================================================

#include "pch.h"
#include "JsonText.h"

namespace
{
    using Json = nlohmann::ordered_json;

    Json ParseOk(std::string_view text)
    {
        Json value;
        JsonParseError error;
        EXPECT_TRUE(JsonParse(text, &value, &error)) << error.Message;
        return value;
    }

    JsonParseError ParseFails(std::string_view text)
    {
        Json value = Json::array({ 7 });
        JsonParseError error;
        EXPECT_FALSE(JsonParse(text, &value, &error));
        EXPECT_EQ(value, Json::array({ 7 })) << "a failed parse leaves the value unchanged";
        EXPECT_FALSE(error.Message.empty());
        return error;
    }
}

TEST(JsonText, ParseKeepsOrderAndOffsets)
{
    const std::string_view text = R"({"b": 1, "a": [true, null], "c": {"d": "x"}})";
    const Json value = ParseOk(text);

    std::vector<std::string> keys;
    for (const auto &[key, member] : value.items())
        keys.push_back(key);
    EXPECT_EQ(keys, (std::vector<std::string>{ "b", "a", "c" }));

    EXPECT_EQ(value.start_pos(), 0u);
    EXPECT_EQ(value["b"].start_pos(), text.find('1'));
    EXPECT_EQ(value["a"].start_pos(), text.find('['));
    EXPECT_EQ(value["a"][1].start_pos(), text.find("null"));
    EXPECT_EQ(value["c"].start_pos(), text.find("{\"d\""));
    EXPECT_EQ(value["c"]["d"].start_pos(), text.find("\"x\""));
}

TEST(JsonText, ParseIsStrict)
{
    ParseFails("[1, 2,]");
    ParseFails("{\"a\": 1,}");
    ParseFails("// comment\n{}");
    ParseFails("{} {}");
    ParseFails("");

    const JsonParseError error = ParseFails("[1, 2,\n x]");
    EXPECT_EQ(error.Offset, 8u);
}

TEST(JsonText, ParseRejectsDuplicateKeys)
{
    // Reported at the second key's opening quote, for scalar and container values alike.
    {
        const std::string_view text = R"({"a": 1, "a": 2})";
        EXPECT_EQ(ParseFails(text).Offset, text.rfind("\"a\""));
    }
    {
        const std::string_view text = R"({"a": [1], "b": {"x": 1, "y": [2]}, "a": {"z": [3]}})";
        EXPECT_EQ(ParseFails(text).Offset, text.rfind("\"a\""));
    }
    {
        const std::string_view text = R"([{"k": 1}, {"k": 2, "n": {"k": 3, "k": 4}}])";
        EXPECT_EQ(ParseFails(text).Offset, text.rfind("\"k\""));
    }

    // Keys compare after escapes are decoded.
    {
        const std::string_view text = R"({"ab": 1, "a\u0062": 2})";
        EXPECT_EQ(ParseFails(text).Offset, text.find("\"a\\u"));
    }

    // The same key in different objects is fine.
    ParseOk(R"({"a": {"a": 1}, "b": [{"a": 1}, {"a": 2}]})");
}

TEST(JsonText, ParseAcceptsByteOrderMark)
{
    const Json value = ParseOk("\xEF\xBB\xBF{\"a\": 1}");
    EXPECT_EQ(value["a"], 1);
}

TEST(JsonText, LineIndex)
{
    const std::string_view text = "{\r\n  \"na\xC3\xAFve\": 1,\n\t\"b\": 2\n}";
    const CJsonLineIndex lines(text);

    JsonPosition position = lines.Locate(0);
    EXPECT_EQ(position.Line, 1u);
    EXPECT_EQ(position.Column, 1u);

    // The accented letter is two bytes and one column, and CR LF ends a line.
    position = lines.Locate(text.find(": 1"));
    EXPECT_EQ(position.Line, 2u);
    EXPECT_EQ(position.Column, 10u);

    // A tab is one column.
    position = lines.Locate(text.find("\"b\""));
    EXPECT_EQ(position.Line, 3u);
    EXPECT_EQ(position.Column, 2u);

    position = lines.Locate(text.size() - 1);
    EXPECT_EQ(position.Line, 4u);
    EXPECT_EQ(position.Column, 1u);
}

TEST(JsonText, LineIndexByteOrderMarkTakesNoColumn)
{
    const std::string_view text = "\xEF\xBB\xBF{\"a\": 1}";
    const CJsonLineIndex lines(text);
    EXPECT_EQ(lines.Locate(3).Column, 1u);
    EXPECT_EQ(lines.Locate(text.find('1')).Column, 7u);
}

TEST(JsonText, KeyOffset)
{
    const std::string_view text = "{\"plain\": 1, \"q\\\"uo\\\\\" :\n  [2], \"e\\\\\": 3}";
    const Json value = ParseOk(text);
    EXPECT_EQ(JsonKeyOffset(text, value["plain"].start_pos()), text.find("\"plain\""));
    EXPECT_EQ(JsonKeyOffset(text, value["q\"uo\\"].start_pos()), text.find("\"q"));
    EXPECT_EQ(JsonKeyOffset(text, value["e\\"].start_pos()), text.find("\"e"));
}

TEST(JsonText, PointerAppend)
{
    std::string pointer;
    JsonPointerAppend(&pointer, "packages");
    JsonPointerAppend(&pointer, size_t(3));
    JsonPointerAppend(&pointer, "a/b~c");
    EXPECT_EQ(pointer, "/packages/3/a~1b~0c");
}

TEST(JsonText, UnknownKeys)
{
    const Json value = ParseOk(R"({"a": 1, "x": 2, "b": 3, "y": 4})");
    std::vector<std::string> unknown;
    JsonForEachUnknownKey(value, { "a", "b" }, [&](const std::string &key, const Json &) { unknown.push_back(key); });
    EXPECT_EQ(unknown, (std::vector<std::string>{ "x", "y" }));
}

TEST(JsonText, FormatScalarsAndEmpty)
{
    EXPECT_EQ(JsonFormat(Json(nullptr)), "null\n");
    EXPECT_EQ(JsonFormat(Json(true)), "true\n");
    EXPECT_EQ(JsonFormat(Json(42)), "42\n");
    EXPECT_EQ(JsonFormat(Json(1.5)), "1.5\n");
    EXPECT_EQ(JsonFormat(Json(3.0)), "3.0\n");
    EXPECT_EQ(JsonFormat(Json("na\xC3\xAFve \"q\"\n\x01")), "\"na\xC3\xAFve \\\"q\\\"\\n\\u0001\"\n");
    EXPECT_EQ(JsonFormat(Json::array()), "[]\n");
    EXPECT_EQ(JsonFormat(Json::object()), "{}\n");
}

TEST(JsonText, FormatInlineWhenItFits)
{
    EXPECT_EQ(JsonFormat(ParseOk(R"([1,2,  3])")), "[1, 2, 3]\n");
    EXPECT_EQ(JsonFormat(ParseOk(R"({"a":1,"b":[true,{}]})")), "{ \"a\": 1, \"b\": [true, {}] }\n");
}

TEST(JsonText, FormatExpandsWhatDoesNotFit)
{
    // 100 columns fits; 101 does not.
    const std::string fits(96, 'x');
    EXPECT_EQ(JsonFormat(Json::array({ fits })), "[\"" + fits + "\"]\n");

    const std::string wide(97, 'x');
    EXPECT_EQ(JsonFormat(Json::array({ Json::array({ wide }) })), "[\n  [\n    \"" + wide + "\"\n  ]\n]\n");

    // A trailing comma counts: this member's one-line form fills 100 columns, and its comma is 101.
    Json object = Json::object();
    object["a"] = Json::array({ std::string(89, 'y') });
    object["b"] = 1;
    EXPECT_EQ(JsonFormat(object),
        "{\n"
        "  \"a\": [\n"
        "    \"" + std::string(89, 'y') + "\"\n"
        "  ],\n"
        "  \"b\": 1\n"
        "}\n");
}

TEST(JsonText, FormatPacksScalarArrays)
{
    Json numbers = Json::array();
    for (int i = 0; i < 40; ++i)
        numbers.push_back(1000 + i);

    Json object = Json::object();
    object["numbers"] = numbers;

    std::string expected = "{\n  \"numbers\": [\n";
    // Each element is 4 columns; a line holds 16 of them: 4 + 16 * 6 - 1 = 99 columns.
    for (int line = 0; line < 3; ++line)
    {
        expected += "    ";
        const int first = line * 16;
        const int last = std::min(first + 16, 40);
        for (int i = first; i < last; ++i)
        {
            expected += std::to_string(1000 + i);
            if (i + 1 < 40)
                expected += i + 1 < last ? ", " : ",";
        }
        expected += "\n";
    }
    expected += "  ]\n}\n";
    EXPECT_EQ(JsonFormat(object), expected);
}

TEST(JsonText, FormatColumnsCountCodePoints)
{
    // Two strings of 46 two-byte code points: 100 columns but 192 bytes of text, so they fit.
    std::string text;
    for (int i = 0; i < 46; ++i)
        text += "\xC3\xAF";
    EXPECT_EQ(JsonFormat(Json::array({ text, text })), "[\"" + text + "\", \"" + text + "\"]\n");
}

TEST(JsonText, FormatRoundTrips)
{
    const Json value = ParseOk(R"({
        "formatVersion": 1,
        "nested": [[1, 2], {"x": [], "y": {}}, "s"],
        "long": ["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", {"c": 0.25}]
    })");
    const std::string text = JsonFormat(value);
    EXPECT_EQ(ParseOk(text), value);
    EXPECT_EQ(JsonFormat(ParseOk(text)), text);
}
