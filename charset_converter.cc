#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <utility>
#include <memory>
#include <algorithm>

#include <boost/locale/encoding.hpp>
#include <boost/optional.hpp>

#include "charset_converter.h"
#include "cmap.h"
#include "common.h"
#include "object_storage.h"
#include "coordinates.h"
#include "fonts.h"


using namespace std;
using namespace boost::locale::conv;

namespace
{
    double get_width(const string &s, const Fonts &fonts)
    {
        double result = 0;
        for (char c : s) result += fonts.get_width(static_cast<unsigned char>(c));
        return result;
    }

    double get_width_identity(const string &s, const Fonts &fonts)
    {
        double result = 0;
        for (size_t i = 0; i < s.length(); i += 2) result += fonts.get_width(string2num(s.substr(i, 2)));
        return result;
    }

    unsigned int get_length(const string &s, const string &charset)
    {
        if (charset == "UTF-8") return utf8_length(s);
        if (charset == "UTF-16be" || charset == "UTF-16le" || charset == "UTF-16") return s.length() / 2;
        if (charset == "UTF-32be" || charset == "UTF-32le" || charset == "UTF-32") return s.length() / 4;
        return s.length();
    }
}

pair<double, string> CharsetConverter::custom_decode_symbol(const string &s, size_t &i, const Fonts &fonts) const
{
    for (unsigned char n : custom_encoding->sizes)
    {
        size_t left = s.length() - i;
        if (left < n) break;
        const string symbol = s.substr(i, n);
        auto it = custom_encoding->utf16_map.find(symbol);
        if (it == custom_encoding->utf16_map.end()) continue;
        i += n;
        return make_pair(fonts.get_width(string2num(symbol)), it->second);
    }
    ++i;
    return make_pair(0, string());
}

vector<text_chunk_t> CharsetConverter::get_strings_from_array(const string &array,
                                                              Coordinates &coordinates,
                                                              const Fonts &fonts) const
{
    vector<text_chunk_t> result;
    double Tj = 0;
    for (const array_t::value_type &p : get_array_data(array, 0))
    {
        switch (p.second)
        {
        case VALUE:
            Tj = stod(p.first);
            break;
        case STRING:
            result.push_back(get_string(decode_string(p.first), coordinates, Tj, fonts));
            Tj = 0;
            break;
        default:
            throw pdf_error(FUNC_STRING + "wrong type " + to_string(p.second) + " val=" + p.first);
        }
    }
    return result;
}
bool CharsetConverter::is_vertical() const
{
    if (custom_encoding && custom_encoding->is_vertical) return true;
    return false;
}

text_chunk_t CharsetConverter::get_string(const string &s, Coordinates &coordinates, double Tj, const Fonts &fonts) const
{
    switch (PDFencode)
    {
    case UTF8:
        return coordinates.adjust_coordinates(string(s), utf8_length(s), get_width(s, fonts), Tj, fonts);
    case IDENTITY:
        return coordinates.adjust_coordinates(to_utf<char>(s, "UTF-16be"),
                                              s.length() / 2,
                                              get_width_identity(s, fonts),
                                              Tj,
                                              fonts);
    case DEFAULT:
    case MAC_EXPERT:
    case MAC_ROMAN:
    case WIN:
    {
        const unordered_map<unsigned int, string> &standard_encoding = standard_encodings.at(PDFencode);
        string str;
        str.reserve(s.length());
        for (char c : s)
        {
            auto it = standard_encoding.find(static_cast<unsigned char>(c));
            if (it != standard_encoding.end()) str.append(it->second);
        }
        return coordinates.adjust_coordinates(std::move(str), s.length(), get_width(s, fonts), Tj, fonts);

    }
    case DIFFERENCE_MAP:
    {
        string str;
        str.reserve(s.length());
        double width = 0;
        for (char c : s)
        {
            auto it = difference_map.find(static_cast<unsigned char>(c));
            if (it != difference_map.end() && !it->second.empty())
            {
                str.append(it->second);
                width += fonts.get_width(static_cast<unsigned char>(c));
            }
        }
        return coordinates.adjust_coordinates(std::move(str), s.length(), width, Tj, fonts);
    }
    case OTHER:
        return coordinates.adjust_coordinates(to_utf<char>(s, charset),
                                              get_length(s, charset),
                                              get_width(s, fonts),
                                              Tj,
                                              fonts);
    case TO_UNICODE:
    {
        string decoded;
        double decoded_width = 0;
        size_t len = 0;
        for (size_t i = 0; i < s.length();)
        {
            pair<double, string> decoded_symbol = custom_decode_symbol(s, i, fonts);
            if (decoded_symbol.second.empty())
            {
                decoded_width += fonts.get_width(static_cast<unsigned char>(s.at(i - 1)));
                decoded += s.at(i - 1);
                ++len;
            }
            else
            {
                decoded_width += decoded_symbol.first;
                //strings from cmap are returned in big ordering
                string utf8 = to_utf<char>(decoded_symbol.second, "UTF-16be");
                len += utf8_length(utf8);
                decoded += std::move(utf8);
            }
        }
        return coordinates.adjust_coordinates(std::move(decoded), len, decoded_width, Tj, fonts);
    }
    }
}

CharsetConverter::CharsetConverter() noexcept :
                                  custom_encoding(nullptr),
                                  charset(nullptr),
                                  PDFencode(DEFAULT),
                                  difference_map(unordered_map<unsigned int, string>())
{
}

CharsetConverter::CharsetConverter(const cmap_t *cmap_arg) :
                                   custom_encoding(cmap_arg),
                                   charset(nullptr),
                                   PDFencode(TO_UNICODE),
                                   difference_map(unordered_map<unsigned int, string>())
{
}

CharsetConverter::CharsetConverter(unordered_map<unsigned int, string> &&difference_map) :
                                   custom_encoding(nullptr),
                                   charset(nullptr),
                                   PDFencode(DIFFERENCE_MAP),
                                   difference_map(std::move(difference_map))
{
}

CharsetConverter::CharsetConverter(const string &encoding) :
                                   custom_encoding(nullptr),
                                   difference_map(unordered_map<unsigned int, string>())
{
    if (encoding == "/WinAnsiEncoding")
    {
        PDFencode = WIN;
        charset = nullptr;
    }
    else if (encoding == "/MacRomanEncoding")
    {
        PDFencode = MAC_ROMAN;
        charset = nullptr;
    }
    else if (encoding == "/MacExpertEncoding")
    {
        PDFencode = MAC_EXPERT;
        charset = nullptr;
    }
    else if (encoding == "/Identity-H" || encoding == "/Identity-V")
    {
        PDFencode = IDENTITY;
        charset = nullptr;
    }
    else
    {
        charset = encoding2charset.at(encoding);
        PDFencode = charset? OTHER : UTF8;
    }
}

unique_ptr<CharsetConverter> CharsetConverter::get_from_dictionary(const dict_t &dictionary, const ObjectStorage &storage)
{
    auto it = dictionary.find("/BaseEncoding");
    PDFEncode_t encoding;
    if (it == dictionary.end()) encoding = DEFAULT;
    else if (it->second.first == "/StandardEncoding") encoding = DEFAULT;
    else if (it->second.first == "/MacRomanEncoding") encoding = MAC_ROMAN;
    else if (it->second.first == "/MacExpertEncoding") encoding = MAC_EXPERT;
    else if (it->second.first == "/WinAnsiEncoding") encoding = WIN;
    else throw pdf_error(FUNC_STRING + "wrong /BaseEncoding value:" + it->second.first);
    auto it2 = dictionary.find("/Differences");
    if (it2 == dictionary.end()) return unique_ptr<CharsetConverter>((encoding == DEFAULT)?
                                                                     new CharsetConverter():
                                                                     new CharsetConverter(it->second.first));
    if (it2->second.second != ARRAY)
    {
        throw pdf_error(FUNC_STRING + "/Differences is not array. Type=" + to_string(it2->second.second));
    }
    return CharsetConverter::get_diff_map_converter(encoding, it2->second.first, storage);
}

boost::optional<string> CharsetConverter::get_symbol_string(const string &name)
{
    auto it = symbol_table.find(name);
    return (it == symbol_table.end())? boost::optional<string>() : boost::optional<string>(it->second);
}

unique_ptr<CharsetConverter> CharsetConverter::get_diff_map_converter(PDFEncode_t encoding,
                                                                      const std::string &array,
                                                                      const ObjectStorage &storage)
{
    unordered_map<unsigned int, string> code2symbol = standard_encodings.at(encoding);
    const array_t array_data = get_array_data(array, 0);

    auto start_it = find_if(array_data.begin(),
                            array_data.end(),
                            [](const pair<string, pdf_object_t> &p) { return (p.second == VALUE)? true : false;});
    if (start_it == array_data.end()) return unique_ptr<CharsetConverter>(new CharsetConverter(std::move(code2symbol)));
    unsigned int code = strict_stoul(start_it->first);

    for (auto it = start_it; it != array_data.end(); ++it)
    {
        const pair<string, pdf_object_t> symbol = (it->second == INDIRECT_OBJECT)?
                                                   get_indirect_object_data(it->first, storage) : *it;
        switch (symbol.second)
        {
        case VALUE:
            code = strict_stoul(symbol.first);
            break;
        case NAME_OBJECT:
        {
            boost::optional<string> r = get_symbol_string(symbol.first);
            if (r) code2symbol[code] = *r;
            ++code;
            break;
        }
        default:
            throw pdf_error(FUNC_STRING + "wrong symbol type=" + to_string(symbol.second) + " val=" + symbol.first);
        }

    }
    return unique_ptr<CharsetConverter>(new CharsetConverter(std::move(code2symbol)));
}

const unordered_map<unsigned int, string> CharsetConverter::standard_encoding = {{32, " "},
                                                                                 {33, "!"},
                                                                                 {34, "\""},
                                                                                 {35, "#"},
                                                                                 {36, "$"},
                                                                                 {37, "%"},
                                                                                 {38, "%"},
                                                                                 {39, "’"},
                                                                                 {40, "("},
                                                                                 {41, ")"},
                                                                                 {42, "*"},
                                                                                 {43, "+"},
                                                                                 {44, ",",},
                                                                                 {45, "-"},
                                                                                 {46, "."},
                                                                                 {47, "/"},
                                                                                 {48, "0"},
                                                                                 {49, "1"},
                                                                                 {50, "2"},
                                                                                 {51, "3"},
                                                                                 {52, "4"},
                                                                                 {53, "5"},
                                                                                 {54, "6"},
                                                                                 {55, "7"},
                                                                                 {56, "8"},
                                                                                 {57, "9"},
                                                                                 {58, ":"},
                                                                                 {59, ";"},
                                                                                 {60, "<"},
                                                                                 {61, "="},
                                                                                 {62, ">"},
                                                                                 {63, "?"},
                                                                                 {64, "@"},
                                                                                 {65, "A"},
                                                                                 {66, "B"},
                                                                                 {67, "C"},
                                                                                 {68, "D"},
                                                                                 {69, "E"},
                                                                                 {70, "F"},
                                                                                 {71, "G"},
                                                                                 {72, "H"},
                                                                                 {73, "I"},
                                                                                 {74, "J"},
                                                                                 {75, "K"},
                                                                                 {76, "L"},
                                                                                 {77, "M"},
                                                                                 {78, "N"},
                                                                                 {79, "O"},
                                                                                 {80, "P"},
                                                                                 {81, "Q"},
                                                                                 {82, "R"},
                                                                                 {83, "S"},
                                                                                 {84, "T"},
                                                                                 {85, "U"},
                                                                                 {86, "V"},
                                                                                 {87, "W"},
                                                                                 {88, "X"},
                                                                                 {89, "Y"},
                                                                                 {90, "Z"},
                                                                                 {91, "["},
                                                                                 {92, "\\"},
                                                                                 {93, "]"},
                                                                                 {94, "^"},
                                                                                 {95, "_"},
                                                                                 {96, "‘"},
                                                                                 {97, "a"},
                                                                                 {98, "b"},
                                                                                 {99, "c"},
                                                                                 {100, "d"},
                                                                                 {101, "e"},
                                                                                 {102, "f"},
                                                                                 {103, "g"},
                                                                                 {104, "h"},
                                                                                 {105, "i"},
                                                                                 {106, "j"},
                                                                                 {107, "k"},
                                                                                 {108, "l"},
                                                                                 {109, "m"},
                                                                                 {110, "n"},
                                                                                 {111, "o"},
                                                                                 {112, "p"},
                                                                                 {113, "q"},
                                                                                 {114, "r"},
                                                                                 {115, "s"},
                                                                                 {116, "t"},
                                                                                 {117, "u"},
                                                                                 {118, "v"},
                                                                                 {119, "w"},
                                                                                 {120, "x"},
                                                                                 {121, "y"},
                                                                                 {122, "z"},
                                                                                 {123, "{"},
                                                                                 {124, "|"},
                                                                                 {125, "}"},
                                                                                 {126, "~"},
                                                                                 {161, "¡"},
                                                                                 {162, "¢"},
                                                                                 {163, "£"},
                                                                                 {164, "⁄"},
                                                                                 {165, "¥"},
                                                                                 {166, "ƒ"},
                                                                                 {167, "§"},
                                                                                 {168, "¤"},
                                                                                 {169, "'"},
                                                                                 {170, "“"},
                                                                                 {171, "«"},
                                                                                 {172, "‹"},
                                                                                 {173, "›"},
                                                                                 {174, "ﬁ"},
                                                                                 {175, "ﬂ"},
                                                                                 {177, "–"},
                                                                                 {178, "†"},
                                                                                 {179, "‡"},
                                                                                 {180, "·"},
                                                                                 {182, "¶"},
                                                                                 {183, "•"},
                                                                                 {184, "‚"},
                                                                                 {185, "„"},
                                                                                 {186, "”"},
                                                                                 {187, "»"},
                                                                                 {188, "…"},
                                                                                 {189, "‰"},
                                                                                 {191, "¿"},
                                                                                 {193, "`"},
                                                                                 {194, "´"},
                                                                                 {195, "ˆ"},
                                                                                 {196, "˜"},
                                                                                 {197, "¯"},
                                                                                 {198, "˘"},
                                                                                 {199, "˙"},
                                                                                 {200, "¨"},
                                                                                 {202, "˚"},
                                                                                 {203, "¸"},
                                                                                 {205, "˝"},
                                                                                 {206, "˛"},
                                                                                 {207, "ˇ"},
                                                                                 {208, "—"},
                                                                                 {225, "Æ"},
                                                                                 {227, "ª"},
                                                                                 {232, "Ł"},
                                                                                 {233, "Ø"},
                                                                                 {234, "Œ"},
                                                                                 {235, "º"},
                                                                                 {241, "æ"},
                                                                                 {245, "ı"},
                                                                                 {248, "ł"},
                                                                                 {249, "ø"},
                                                                                 {250, "œ"},
                                                                                 {251, "ß"}};

const unordered_map<unsigned int, string> CharsetConverter::mac_expert_encoding = {{32, " "},
                                                                                   {33, ""},
                                                                                   {34, ""},
                                                                                   {35, ""},
                                                                                   {36, ""},
                                                                                   {37, ""},
                                                                                   {38, ""},
                                                                                   {39, ""},
                                                                                   {40, "⁽"},
                                                                                   {41, "⁾"},
                                                                                   {42, ".."},
                                                                                   {43, "."},
                                                                                   {44, "",},
                                                                                   {45, "-"},
                                                                                   {46, "."},
                                                                                   {47, "⁄"},
                                                                                   {48, "0"},
                                                                                   {49, "1"},
                                                                                   {50, "2"},
                                                                                   {51, "3"},
                                                                                   {52, "4"},
                                                                                   {53, "5"},
                                                                                   {54, "6"},
                                                                                   {55, "7"},
                                                                                   {56, "8"},
                                                                                   {57, "9"},
                                                                                   {58, ":"},
                                                                                   {59, ";"},
                                                                                   {61, ""},
                                                                                   {63, ""},
                                                                                   {68, ""},
                                                                                   {71, "¼"},
                                                                                   {72, "½"},
                                                                                   {73, "¾"},
                                                                                   {74, "⅛"},
                                                                                   {75, "⅜"},
                                                                                   {76, "⅝"},
                                                                                   {77, "⅞"},
                                                                                   {78, "⅓"},
                                                                                   {79, "⅔"},
                                                                                   {86, "ff"},
                                                                                   {87, "fi"},
                                                                                   {88, "fl"},
                                                                                   {89, "ffi"},
                                                                                   {90, "ffl"},
                                                                                   {91, "₍"},
                                                                                   {93, "₎"},
                                                                                   {94, ""},
                                                                                   {95, ""},
                                                                                   {96, ""},
                                                                                   {97, "A"},
                                                                                   {98, "B"},
                                                                                   {99, "C"},
                                                                                   {100, "D"},
                                                                                   {101, "E"},
                                                                                   {102, "F"},
                                                                                   {103, "G"},
                                                                                   {104, "H"},
                                                                                   {105, "I"},
                                                                                   {106, "J"},
                                                                                   {107, "K"},
                                                                                   {108, "L"},
                                                                                   {109, "M"},
                                                                                   {110, "N"},
                                                                                   {111, "O"},
                                                                                   {112, "P"},
                                                                                   {113, "Q"},
                                                                                   {114, "R"},
                                                                                   {115, "S"},
                                                                                   {116, "T"},
                                                                                   {117, "U"},
                                                                                   {118, "V"},
                                                                                   {119, "W"},
                                                                                   {120, "X"},
                                                                                   {121, "Y"},
                                                                                   {122, "Z"},
                                                                                   {123, "₡"},
                                                                                   {124, ""},
                                                                                   {125, ""},
                                                                                   {126, ""},
                                                                                   {129, ""},
                                                                                   {130, ""},
                                                                                   {135, ""},
                                                                                   {136, ""},
                                                                                   {137, ""},
                                                                                   {138, ""},
                                                                                   {139, ""},
                                                                                   {140, ""},
                                                                                   {141, ""},
                                                                                   {142, ""},
                                                                                   {143, ""},
                                                                                   {144, ""},
                                                                                   {145, ""},
                                                                                   {146, ""},
                                                                                   {147, ""},
                                                                                   {148, ""},
                                                                                   {149, ""},
                                                                                   {150, ""},
                                                                                   {151, ""},
                                                                                   {152, ""},
                                                                                   {153, ""},
                                                                                   {154, ""},
                                                                                   {155, ""},
                                                                                   {156, ""},
                                                                                   {157, ""},
                                                                                   {158, ""},
                                                                                   {159, ""},
                                                                                   {161, "⁸"},
                                                                                   {162, "₄"},
                                                                                   {163, "₃"},
                                                                                   {164, "₆"},
                                                                                   {165, "₈"},
                                                                                   {166, "⁷"},
                                                                                   {167, ""},
                                                                                   {169, ""},
                                                                                   {170, "₃"},
                                                                                   {172, ""},
                                                                                   {174, ""},
                                                                                   {175, ""},
                                                                                   {176, "₅"},
                                                                                   {178, ""},
                                                                                   {179, ""},
                                                                                   {180, ""},
                                                                                   {182, ""},
                                                                                   {185, ""},
                                                                                   {187, "₉"},
                                                                                   {188, "₀"},
                                                                                   {189, ""},
                                                                                   {190, ""},
                                                                                   {191, ""},
                                                                                   {192, ""},
                                                                                   {193, "₁"},
                                                                                   {194, ""},
                                                                                   {201, ""},
                                                                                   {207, ""},
                                                                                   {208, "‒"},
                                                                                   {209, ""},
                                                                                   {214, ""},
                                                                                   {216, ""},
                                                                                   {218, "1"},
                                                                                   {219, "2"},
                                                                                   {220, "3"},
                                                                                   {221, "4"},
                                                                                   {222, "5"},
                                                                                   {223, "6"},
                                                                                   {224, "7"},
                                                                                   {225, "8"},
                                                                                   {226, "0"},
                                                                                   {228, ""},
                                                                                   {229, ""},
                                                                                   {230, ""},
                                                                                   {233, ""},
                                                                                   {234, ""},
                                                                                   {235, ""},
                                                                                   {241, ""},
                                                                                   {242, ""},
                                                                                   {243, ""},
                                                                                   {244, ""},
                                                                                   {245, ""},
                                                                                   {246, "ⁿ"},
                                                                                   {247, ""},
                                                                                   {248, ""},
                                                                                   {249, ""},
                                                                                   {250, ""},
                                                                                   {251, ""}};

const unordered_map<unsigned int, string> CharsetConverter::mac_roman_encoding = {{32, " "},
                                                                                  {33, "!"},
                                                                                  {34, "\""},
                                                                                  {35, "#"},
                                                                                  {36, "$"},
                                                                                  {37, "%"},
                                                                                  {38, "&"},
                                                                                  {39, "'"},
                                                                                  {40, "("},
                                                                                  {41, ")"},
                                                                                  {42, "*"},
                                                                                  {43, "+"},
                                                                                  {44, "",},
                                                                                  {45, "-"},
                                                                                  {46, "."},
                                                                                  {47, "/"},
                                                                                  {48, "0"},
                                                                                  {49, "1"},
                                                                                  {50, "2"},
                                                                                  {51, "3"},
                                                                                  {52, "4"},
                                                                                  {53, "5"},
                                                                                  {54, "6"},
                                                                                  {55, "7"},
                                                                                  {56, "8"},
                                                                                  {57, "9"},
                                                                                  {58, ":"},
                                                                                  {59, ";"},
                                                                                  {60, "<"},
                                                                                  {61, "="},
                                                                                  {62, ">"},
                                                                                  {63, "?"},
                                                                                  {64, "@"},
                                                                                  {65, "A"},
                                                                                  {66, "B"},
                                                                                  {67, "C"},
                                                                                  {68, "D"},
                                                                                  {69, "E"},
                                                                                  {70, "F"},
                                                                                  {71, "G"},
                                                                                  {72, "H"},
                                                                                  {73, "I"},
                                                                                  {74, "J"},
                                                                                  {75, "K"},
                                                                                  {76, "L"},
                                                                                  {77, "M"},
                                                                                  {78, "N"},
                                                                                  {79, "O"},
                                                                                  {80, "P"},
                                                                                  {81, "Q"},
                                                                                  {82, "R"},
                                                                                  {83, "S"},
                                                                                  {84, "T"},
                                                                                  {85, "U"},
                                                                                  {86, "V"},
                                                                                  {87, "W"},
                                                                                  {88, "X"},
                                                                                  {89, "Y"},
                                                                                  {90, "Z"},
                                                                                  {91, "["},
                                                                                  {92, "\\"},
                                                                                  {93, "]"},
                                                                                  {94, "^"},
                                                                                  {95, "_"},
                                                                                  {96, "`"},
                                                                                  {97, "a"},
                                                                                  {98, "b"},
                                                                                  {99, "c"},
                                                                                  {100, "d"},
                                                                                  {101, "e"},
                                                                                  {102, "f"},
                                                                                  {103, "g"},
                                                                                  {104, "h"},
                                                                                  {105, "i"},
                                                                                  {106, "j"},
                                                                                  {107, "k"},
                                                                                  {108, "l"},
                                                                                  {109, "m"},
                                                                                  {110, "n"},
                                                                                  {111, "o"},
                                                                                  {112, "p"},
                                                                                  {113, "q"},
                                                                                  {114, "r"},
                                                                                  {115, "s"},
                                                                                  {116, "t"},
                                                                                  {117, "u"},
                                                                                  {118, "v"},
                                                                                  {119, "w"},
                                                                                  {120, "x"},
                                                                                  {121, "y"},
                                                                                  {122, "z"},
                                                                                  {123, "{"},
                                                                                  {124, "|"},
                                                                                  {125, "}"},
                                                                                  {126, "~"},
                                                                                  {128, "Ä"},
                                                                                  {129, "Å"},
                                                                                  {130, "Ç"},
                                                                                  {131, "É"},
                                                                                  {132, "Ñ"},
                                                                                  {133, "Ö"},
                                                                                  {134, "Ü"},
                                                                                  {135, "á"},
                                                                                  {136, "à"},
                                                                                  {137, "â"},
                                                                                  {138, "ä"},
                                                                                  {139, "ã"},
                                                                                  {140, "å"},
                                                                                  {141, "ç"},
                                                                                  {142, "é"},
                                                                                  {143, "è"},
                                                                                  {144, "ê"},
                                                                                  {145, "ë"},
                                                                                  {146, "í"},
                                                                                  {147, "ì"},
                                                                                  {148, "î"},
                                                                                  {149, "ï"},
                                                                                  {150, "ñ"},
                                                                                  {151, "ó"},
                                                                                  {152, "ò"},
                                                                                  {153, "ô"},
                                                                                  {154, "ö"},
                                                                                  {155, "õ"},
                                                                                  {156, "ú"},
                                                                                  {157, "ù"},
                                                                                  {158, "û"},
                                                                                  {159, "ü"},
                                                                                  {160, "†"},
                                                                                  {161, "°"},
                                                                                  {162, "¢"},
                                                                                  {163, "£"},
                                                                                  {164, "§"},
                                                                                  {165, "•"},
                                                                                  {166, "¶"},
                                                                                  {167, "ß"},
                                                                                  {168, "®"},
                                                                                  {169, "©"},
                                                                                  {170, "™"},
                                                                                  {171, "´"},
                                                                                  {172, "¨"},
                                                                                  {173, "≠"},
                                                                                  {174, "Æ"},
                                                                                  {175, "Ø"},
                                                                                  {176, "∞"},
                                                                                  {177, "±"},
                                                                                  {178, "≤"},
                                                                                  {179, "≥"},
                                                                                  {180, "¥"},
                                                                                  {181, "µ"},
                                                                                  {182, "∂"},
                                                                                  {183, "∑"},
                                                                                  {184, "∏"},
                                                                                  {185, "π"},
                                                                                  {186, "∫"},
                                                                                  {187, "ª"},
                                                                                  {188, "º"},
                                                                                  {189, "Ω"},
                                                                                  {190, "æ"},
                                                                                  {191, "ø"},
                                                                                  {192, "¿"},
                                                                                  {193, "¡"},
                                                                                  {194, "¬"},
                                                                                  {195, "√"},
                                                                                  {196, "ƒ"},
                                                                                  {197, "≈"},
                                                                                  {198, "∆"},
                                                                                  {199, "«"},
                                                                                  {200, "»"},
                                                                                  {201, "…"},
                                                                                  {202, " "},
                                                                                  {203, "À"},
                                                                                  {204, "Ã"},
                                                                                  {205, "Õ"},
                                                                                  {206, "Œ"},
                                                                                  {207, "œ"},
                                                                                  {208, "–"},
                                                                                  {209, "—"},
                                                                                  {210, "“"},
                                                                                  {211, "”"},
                                                                                  {212, "‘"},
                                                                                  {213, "’"},
                                                                                  {214, "÷"},
                                                                                  {215, "◊"},
                                                                                  {216, "ÿ"},
                                                                                  {217, "Ÿ"},
                                                                                  {218, "⁄"},
                                                                                  {219, "€¹"},
                                                                                  {220, "‹"},
                                                                                  {221, "›"},
                                                                                  {222, "ﬁ"},
                                                                                  {223, "ﬂ"},
                                                                                  {224, "‡"},
                                                                                  {225, "·"},
                                                                                  {226, "‚"},
                                                                                  {227, "„"},
                                                                                  {228, "‰"},
                                                                                  {229, "Â"},
                                                                                  {230, "Ê"},
                                                                                  {231, "Á"},
                                                                                  {232, "Ë"},
                                                                                  {233, "È"},
                                                                                  {234, "Í"},
                                                                                  {235, "Î"},
                                                                                  {236, "Ï"},
                                                                                  {237, "Ì"},
                                                                                  {238, "Ó"},
                                                                                  {239, "Ô"},
                                                                                  {240, ""},
                                                                                  {241, "Ò"},
                                                                                  {242, "Ú"},
                                                                                  {243, "Û"},
                                                                                  {244, "Ù"},
                                                                                  {245, "ı"},
                                                                                  {246, "ˆ"},
                                                                                  {247, "˜"},
                                                                                  {248, "¯"},
                                                                                  {249, "˘"},
                                                                                  {250, "˙"},
                                                                                  {251, "˚"},
                                                                                  {252, "¸"},
                                                                                  {253, "˝"},
                                                                                  {254, "˛"},
                                                                                  {255, "ˇ"}};

const unordered_map<unsigned int, string> CharsetConverter::win_ansi_encoding = {{32, " "},
                                                                                 {33, "!"},
                                                                                 {34, "@"},
                                                                                 {35, "#"},
                                                                                 {36, "$"},
                                                                                 {37, "%"},
                                                                                 {38, "&"},
                                                                                 {39, "'"},
                                                                                 {40, "("},
                                                                                 {41, ")"},
                                                                                 {42, "*"},
                                                                                 {43, "+"},
                                                                                 {44, "",},
                                                                                 {45, "-"},
                                                                                 {46, "."},
                                                                                 {47, "/"},
                                                                                 {48, "0"},
                                                                                 {49, "1"},
                                                                                 {50, "2"},
                                                                                 {51, "3"},
                                                                                 {52, "4"},
                                                                                 {53, "5"},
                                                                                 {54, "6"},
                                                                                 {55, "7"},
                                                                                 {56, "8"},
                                                                                 {57, "9"},
                                                                                 {58, ":"},
                                                                                 {59, ";"},
                                                                                 {60, "<"},
                                                                                 {61, "="},
                                                                                 {62, ">"},
                                                                                 {63, "?"},
                                                                                 {64, "@"},
                                                                                 {65, "A"},
                                                                                 {66, "B"},
                                                                                 {67, "C"},
                                                                                 {68, "D"},
                                                                                 {69, "E"},
                                                                                 {70, "F"},
                                                                                 {71, "G"},
                                                                                 {72, "H"},
                                                                                 {73, "I"},
                                                                                 {74, "J"},
                                                                                 {75, "K"},
                                                                                 {76, "L"},
                                                                                 {77, "M"},
                                                                                 {78, "N"},
                                                                                 {79, "O"},
                                                                                 {80, "P"},
                                                                                 {81, "Q"},
                                                                                 {82, "R"},
                                                                                 {83, "S"},
                                                                                 {84, "T"},
                                                                                 {85, "U"},
                                                                                 {86, "V"},
                                                                                 {87, "W"},
                                                                                 {88, "X"},
                                                                                 {89, "Y"},
                                                                                 {90, "Z"},
                                                                                 {91, "["},
                                                                                 {92, "\\"},
                                                                                 {93, "]"},
                                                                                 {94, "^"},
                                                                                 {95, "_"},
                                                                                 {96, "`"},
                                                                                 {97, "a"},
                                                                                 {98, "b"},
                                                                                 {99, "c"},
                                                                                 {100, "d"},
                                                                                 {101, "e"},
                                                                                 {102, "f"},
                                                                                 {103, "g"},
                                                                                 {104, "h"},
                                                                                 {105, "i"},
                                                                                 {106, "j"},
                                                                                 {107, "k"},
                                                                                 {108, "l"},
                                                                                 {109, "m"},
                                                                                 {110, "n"},
                                                                                 {111, "o"},
                                                                                 {112, "p"},
                                                                                 {113, "q"},
                                                                                 {114, "r"},
                                                                                 {115, "s"},
                                                                                 {116, "t"},
                                                                                 {117, "u"},
                                                                                 {118, "v"},
                                                                                 {119, "w"},
                                                                                 {120, "x"},
                                                                                 {121, "y"},
                                                                                 {122, "z"},
                                                                                 {123, "{"},
                                                                                 {124, "|"},
                                                                                 {125, "}"},
                                                                                 {126, "~"},
                                                                                 {127, "•"},
                                                                                 {128, "€"},
                                                                                 {129, "•"},
                                                                                 {130, "‚"},
                                                                                 {131, "ƒ"},
                                                                                 {132, "„"},
                                                                                 {133, "…"},
                                                                                 {134, "†"},
                                                                                 {135, "‡"},
                                                                                 {136, "ˆ"},
                                                                                 {137, "‰"},
                                                                                 {138, "Š"},
                                                                                 {139, "‹"},
                                                                                 {140, "Œ"},
                                                                                 {141, "•"},
                                                                                 {142, "Ž"},
                                                                                 {143, "•"},
                                                                                 {144, "•"},
                                                                                 {145, "‘"},
                                                                                 {146, "’"},
                                                                                 {147, "“"},
                                                                                 {148, "”"},
                                                                                 {149, "•"},
                                                                                 {150, "–"},
                                                                                 {151, "—"},
                                                                                 {152, "˜"},
                                                                                 {153, "™"},
                                                                                 {154, "š"},
                                                                                 {155, "›"},
                                                                                 {156, "œ"},
                                                                                 {157, "•"},
                                                                                 {158, "ž"},
                                                                                 {159, "Ÿ"},
                                                                                 {160, " "},
                                                                                 {161, "¡"},
                                                                                 {162, "¢"},
                                                                                 {163, "£"},
                                                                                 {164, "¤"},
                                                                                 {165, "¥"},
                                                                                 {166, "¦"},
                                                                                 {167, "§"},
                                                                                 {168, "¨"},
                                                                                 {169, "©"},
                                                                                 {170, "ª"},
                                                                                 {171, "«"},
                                                                                 {172, "¬"},
                                                                                 {173, "-"},
                                                                                 {174, "®"},
                                                                                 {175, "¯"},
                                                                                 {176, "°"},
                                                                                 {177, "±"},
                                                                                 {178, "²"},
                                                                                 {179, "³"},
                                                                                 {180, "´"},
                                                                                 {181, "µ"},
                                                                                 {182, "¶"},
                                                                                 {183, "·"},
                                                                                 {184, "¸"},
                                                                                 {185, "¹"},
                                                                                 {186, "º"},
                                                                                 {187, "»"},
                                                                                 {188, "¼"},
                                                                                 {189, "½"},
                                                                                 {190, "¾"},
                                                                                 {191, "¿"},
                                                                                 {192, "À"},
                                                                                 {193, "Á"},
                                                                                 {194, "Â"},
                                                                                 {195, "Ã"},
                                                                                 {196, "Ä"},
                                                                                 {197, "Å"},
                                                                                 {198, "Æ"},
                                                                                 {199, "Ç"},
                                                                                 {200, "È"},
                                                                                 {201, "É"},
                                                                                 {202, "Ê"},
                                                                                 {203, "Ë"},
                                                                                 {204, "Ì"},
                                                                                 {205, "Í"},
                                                                                 {206, "Î"},
                                                                                 {207, "Ï"},
                                                                                 {208, "Ð"},
                                                                                 {209, "Ñ"},
                                                                                 {210, "Ò"},
                                                                                 {211, "Ó"},
                                                                                 {212, "Ô"},
                                                                                 {213, "Õ"},
                                                                                 {214, "Ö"},
                                                                                 {215, "×"},
                                                                                 {216, "Ø"},
                                                                                 {217, "Ù"},
                                                                                 {218, "Ú"},
                                                                                 {219, "Û"},
                                                                                 {220, "Ü"},
                                                                                 {221, "Ý"},
                                                                                 {222, "Þ"},
                                                                                 {223, "ß"},
                                                                                 {224, "à"},
                                                                                 {225, "á"},
                                                                                 {226, "â"},
                                                                                 {227, "ã"},
                                                                                 {228, "ä"},
                                                                                 {229, "å"},
                                                                                 {230, "æ"},
                                                                                 {231, "ç"},
                                                                                 {232, "è"},
                                                                                 {233, "é"},
                                                                                 {234, "ê"},
                                                                                 {235, "ë"},
                                                                                 {236, "ì"},
                                                                                 {237, "í"},
                                                                                 {238, "î"},
                                                                                 {239, "ï"},
                                                                                 {240, "ð"},
                                                                                 {241, "ñ"},
                                                                                 {242, "ò"},
                                                                                 {243, "ó"},
                                                                                 {244, "ô"},
                                                                                 {245, "õ"},
                                                                                 {246, "ö"},
                                                                                 {247, "÷"},
                                                                                 {248, "ø"},
                                                                                 {249, "ù"},
                                                                                 {250, "ú"},
                                                                                 {251, "û"},
                                                                                 {252, "ü"},
                                                                                 {253, "ý"},
                                                                                 {254, "þ"},
                                                                                 {255, "ÿ"}};

const std::unordered_map<string, const char*> CharsetConverter::encoding2charset = {
    {"/UniCNS-UCS2-H", "UTF-16be"},
    {"/UniCNS-UCS2-V", "UTF-16be"},
    {"/GBK-EUC-H", "GBK"},
    {"/GBK-EUC_V", "GBK"},
    {"/GB-H", "ISO-2022-CN"},
    {"/GB-V", "ISO-2022-CN"},
    {"/GB-EUC-H", "EUC-CN"},
    {"/GB-EUC-V", "EUC-CN"},
    {"/GBpc-EUC-H", "EUC-CN"},
    {"/GBpc-EUC-V", "EUC-CN"},
    {"/GBT-H", "ISO-2022-CN"},
    {"/GBT-V", "ISO-2022-CN"},
    {"/GBT-EUC-H", "EUC-CN"},
    {"/GBT-EUC-V", "EUC-CN"},
    {"/GBTpc-EUC-H", "EUC-CN"},
    {"/GBTpc-EUC-V", "EUC-CN"},
    {"/GBKp-EUC-H", "GBK"},
    {"/GBKp-EUC-V", "GBK"},
    {"/GBK2K-H", "GB18030"},
    {"/GBK2K-V", "GB18030"},
    {"/UniGB-UCS2-H", "UTF-16be"},
    {"/UniGB-UCS2-V", "UTF-16be"},
    {"/UniGB-UTF8-H", nullptr},
    {"/UniGB-UTF8-V", nullptr},
    {"/UniGB-UTF16-H", "UTF-16be"},
    {"/UniGB-UTF16-V", "UTF-16be"},
    {"/UniGB-UTF32-H", "UTF-32be"},
    {"/UniGB-UTF32-V", "UTF-32be"},
    {"/B5-H", "Big5"},
    {"/B5-V", "Big5"},
    {"/B5pc-H", "Big5"},
    {"/B5pc-V", "Big5"},
    {"/ETen-B5-H", "Big5"},
    {"/ETen-B5-V", "Big5"},
    {"/ETenms-B5-H", "Big5"},
    {"/ETenms-B5-V", "Big5"},
    {"/CNS1-H", "ISO-2022-CN"},
    {"/CNS1-V", "ISO-2022-CN"},
    {"/CNS2-H", "ISO-2022-CN"},
    {"/CNS2-V", "ISO-2022-CN"},
    {"/CNS-EUC-H", "EUC-TW"},
    {"/CNS-EUC-V", "EUC-TW"},
    {"/UniCNS-UTF8-H", nullptr},
    {"/UniCNS-UTF8-V", nullptr},
    {"/UniCNS-UTF16-H", "UTF-16be"},
    {"/UniCNS-UTF16-V", "UTF-16be"},
    {"/UniCNS-UTF32-H", "UTF-32be"},
    {"/UniCNS-UTF32-V", "UTF-32be"},
    {"/ETHK-B5-H", "Big-5"},
    {"/ETHK-B5-V", "Big-5"},
    {"/HKdla-B5-H", "Big-5"},
    {"/HKdla-B5-V", "Big-5"},
    {"/HKdlb-B5-H", "Big-5"},
    {"/HKdlb-B5-V", "Big-5"},
    {"/HKgccs-B5-H", "Big-5"},
    {"/HKgccs-B5-V", "Big-5"},
    {"/HKm314-B5-H", "Big-5"},
    {"/HKm314-B5-V", "Big-5"},
    {"/HKm471-B5-H", "Big-5"},
    {"/HKm471-B5-V", "Big-5"},
    {"/HKscs-B5-H", "Big-5"},
    {"/HKscs-B5-V", "Big-5"},
    {"/H", "ISO-2022-JP"},
    {"/V", "ISO-2022-JP"},
    {"/RKSJ-H", "Shift-JIS"},
    {"/RKSJ-V", "Shift-JIS"},
    {"/EUC-H", "EUC-JP"},
    {"/EUC-V", "EUC-JP"},
    {"/83pv-RKSJ-H", "Shift-JIS"},
    {"/83pv-RKSJ-V", "Shift-JIS"},
    {"/Add-H", "ISO-2022-JP"},
    {"/Add-V", "ISO-2022-JP"},
    {"/Add-RKSJ-H", "Shift-JIS"},
    {"/Add-RKSJ-V", "Shift-JIS"},
    {"/Ext-H", "ISO-2022-JP"},
    {"/Ext-V", "ISO-2022-JP"},
    {"/Ext-RKSJ-H", "Shift-JIS"},
    {"/Ext-RKSJ-V", "Shift-JIS"},
    {"/NWP-H", "ISO-2022-JP"},
    {"/NWP-V", "ISO-2022-JP"},
    {"/90pv-RKSJ-H", "Shift-JIS"},
    {"/90pv-RKSJ-V", "Shift-JIS"},
    {"/90ms-RKSJ-H", "Shift-JIS"},
    {"/90ms-RKSJ-V", "Shift-JIS"},
    {"/90msp-RKSJ-H", "Shift-JIS"},
    {"/90msp-RKSJ-V", "Shift-JIS"},
    {"/78-H", "ISO-2022-JP"},
    {"/78-V", "ISO-2022-JP"},
    {"/78-RKSJ-H", "Shift-JIS"},
    {"/78-RKSJ-V", "Shift-JIS"},
    {"/78ms-RKSJ-H", "Shift-JIS"},
    {"/78ms-RKSJ-V", "Shift-JIS"},
    {"/78-EUC-H", "EUC-JP"},
    {"/78-EUC-V", "EUC-JP"},
    {"/UniJIS-UCS2-H", "UTF-16be"},
    {"/UniJIS-UCS2-V", "UTF-16be"},
    {"/UniJIS-UCS2-HW-H", "UTF-16be"},
    {"/UniJIS-UCS2-HW-V", "UTF-16be"},
    {"/UniJIS-UTF8-H", nullptr},
    {"/UniJIS-UTF8-V", nullptr},
    {"/UniJIS-UTF16-H", "UTF-16be"},
    {"/UniJIS-UTF16-V", "UTF-16be"},
    {"/UniJIS-UTF32-H", "UTF-32be"},
    {"/UniJIS-UTF32-V", "UTF-32be"},
    {"/UniJIS2004-UTF8-H", nullptr},
    {"/UniJIS2004-UTF8-V", nullptr},
    {"/UniJIS2004-UTF16-H", "UTF-16be"},
    {"/UniJIS2004-UTF16-V", "UTF-16be"},
    {"/UniJIS2004-UTF32-H", "UTF-32be"},
    {"/UniJIS2004-UTF32-V", "UTF-32be"},
    {"/UniJISX0213-UTF32-H", "UTF-32be"},
    {"/UniJISX0213-UTF32-V", "UTF-32be"},
    {"/UniJISX02132004-UTF32-H", "UTF-32be"},
    {"/UniJISX02132004-UTF32-V", "UTF-32be"},
    {"/UniAKR-UTF8-H", nullptr},
    {"/UniAKR-UTF8-V", nullptr},
    {"/UniAKR-UTF16-H", "UTF-16be"},
    {"/UniAKR-UTF16-V", "UTF-16be"},
    {"/UniAKR-UTF32-H", "UTF-32be"},
    {"/UniAKR-UTF32-V", "UTF-32be"},
    {"/KSC-H", "ISO-2022-KR"},
    {"/KSC-V", "ISO-2022-KR"},
    {"/KSC-EUC-H", "EUC-KR"},
    {"/KSC-EUC-V", "EUC-KR"},
    {"/KSCpv-EUC-H", "EUC-KR"},
    {"/KSCpv-EUC-V", "EUC-KR"},
    {"/KSCms-EUC-H", "UHC"},
    {"/KSCms-EUC-V", "UHC"},
    {"/KSCms-EUC-HW-H", "UHC"},
    {"/KSCms-EUC-HW-V", "UHC"},
    {"/KSC-Johab-H", "UHC"},
    {"/KSC-Johab-V", "UHC"},
    {"/UniKS-UCS2-H", "UTF-16be"},
    {"/UniKS-UCS2-V", "UTF-16be"},
    {"/UniKS-UTF8-H", nullptr},
    {"/UniKS-UTF8-V", nullptr},
    {"/UniKS-UTF16-H", "UTF-16be"},
    {"/UniKS-UTF16-V", "UTF-16be"},
    {"/UniKS-UTF32-H", "UTF-32be"},
    {"/UniKS-UTF32-V", "UTF-32be"},
    {"/Hojo-H", "ISO-2022-JP-1"},
    {"/Hojo-V", "ISO-2022-JP-1"},
    {"/Hojo-EUC-H", "EUC-JP"},
    {"/Hojo-EUC-V", "EUC-JP"},
    {"/UniHojo-UCS2-H", "UTF-16be"},
    {"/UniHojo-UCS2-V", "UTF-16be"},
    {"/UniHojo-UTF8-H", nullptr},
    {"/UniHojo-UTF8-V", nullptr},
    {"/UniHojo-UTF16-H", "UTF-16be"},
    {"/UniHojo-UTF16-V", "UTF-16be"},
    {"/UniHojo-UTF32-H", "UTF-32be"},
    {"/UniHojo-UTF32-V", "UTF-32be"}};

const std::map<CharsetConverter::PDFEncode_t,
               const std::unordered_map<unsigned int, string>&> CharsetConverter::standard_encodings = {
    {DEFAULT, standard_encoding},
    {MAC_EXPERT, mac_expert_encoding},
    {MAC_ROMAN, mac_roman_encoding},
    {WIN, win_ansi_encoding}};

const std::unordered_map<std::string, std::string> CharsetConverter::symbol_table = {
    #include "symbol_table.h"
};
