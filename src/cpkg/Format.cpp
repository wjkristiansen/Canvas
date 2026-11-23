//================================================================================================
// Format - rewrites a package source in the standard form
//
// The standard form of a file, as "Editing Sources" in src/cpkg/README.md describes it, is the
// file's JSON with each member Load found equal to its default removed, each number written with
// a fraction or an exponent respelled, and the result laid out by JsonFormat.
//================================================================================================

#include "CpkgTool.h"
#include "Stages.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace CpkgTool
{

namespace fs = std::filesystem;

namespace
{

// Numbers from -2^53 to 2^53 are the integers a double holds exactly.
constexpr double kExactIntegerLimit = 9007199254740992.0;

// Respells each number of value written with a fraction or an exponent, except within format
// keys, whose values are kept as written.
void RespellNumbers(Json &value)
{
    if (value.is_object())
    {
        for (auto &[key, member] : value.items())
        {
            if (!key.starts_with('$'))
                RespellNumbers(member);
        }
    }
    else if (value.is_array())
    {
        for (Json &element : value)
            RespellNumbers(element);
    }
    else if (value.is_number_float())
    {
        const double number = value.get<double>();
        const bool negativeZero = number == 0.0 && std::signbit(number);
        if (number == std::floor(number) && std::abs(number) <= kExactIntegerLimit && !negativeZero)
            value = number < 0.0 ? Json(int64_t(number)) : Json(uint64_t(number));
        else
            value = FloatJson(float(number));
    }
}

} // namespace

//------------------------------------------------------------------------------------------------
Json FloatJson(float value)
{
    char text[32];
    const char *pEnd = std::to_chars(text, text + sizeof(text), value).ptr;
    const std::string_view written(text, size_t(pEnd - text));
    if (written.find_first_of(".e") == std::string_view::npos && written != "-0")
    {
        int64_t integer = 0;
        if (std::from_chars(text, pEnd, integer).ec == std::errc())
            return integer;
    }
    double number = 0.0;
    std::from_chars(text, pEnd, number);
    if (float(number) != value)
        number = double(value);
    return number;
}

std::string StandardText(const SourceFile &file)
{
    Json document = file.Document;
    for (const std::string &pointer : file.Defaults)
    {
        const Json::json_pointer member(pointer);
        document[member.parent_pointer()].erase(member.back());
    }
    RespellNumbers(document);
    return Canvas::JsonFormat(document);
}

//------------------------------------------------------------------------------------------------
void Format(const FormatOptions &options, CDiagnostics &diagnostics)
{
    SourcePackage package;
    if (!LoadPackageSource(options.Package, &package, diagnostics))
        return;

    for (const std::unique_ptr<SourceFile> &pFile : package.Files)
    {
        const std::string text = StandardText(*pFile);
        if (text == pFile->Text)
            continue;
        if (options.Check)
            diagnostics.Report(Severity::Error, pFile->Path, "the file is not in the standard form; cpkg format rewrites it");
        else if (SaveFile(PathOf(pFile->Path), text) != FileResult::Success)
            diagnostics.Report(Severity::Error, pFile->Path, "cannot write the file");
    }
}

} // namespace CpkgTool
