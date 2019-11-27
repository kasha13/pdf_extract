#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <utility>
#include <memory>

#include <boost/locale/encoding.hpp>

#include "charset_converter.h"
#include "cmap.h"
#include "common.h"
#include "object_storage.h"

using namespace std;
using namespace boost::locale::conv;

#define RETURN_SPACE_WIDTH_VALUE(DICT, KEY) do \
    {\
        auto it = DICT.find(KEY);\
        if (it != DICT.end())\
        {\
            if (it->second.second != VALUE) throw pdf_error(FUNC_STRING + KEY + " must have VALUE value"); \
            return strict_stoul(get_int(it->second.first));             \
        }\
    }\
    while(false)

namespace
{
    enum { SPACE_WIDTH_FRACTION = 2,
           NO_SPACE_WIDTH = 0,
           DEFAULT_SPACE_WIDTH = 100};
    unsigned int convert2uint(const string &s)
    {
        if (s.length() > sizeof(unsigned int) || s.empty()) throw pdf_error(FUNC_STRING + "wrong length. s= " + s);
        union convert_t
        {
            unsigned int v;
            unsigned char a[sizeof(unsigned int)];
        } v{0};

        for (int i = s.length() - 1, j = 0; i >= 0; --i, ++j) v.a[j] = s[i];
        return v.v;
    }

    unsigned int get_space_width_from_font_descriptor(const ObjectStorage &storage, const pair<string, pdf_object_t> &dict)
    {
        if (dict.second != INDIRECT_OBJECT) throw pdf_error(FUNC_STRING + "/FontDescriptor must be indirect object");
        const pair<string, pdf_object_t> desc = storage.get_object(get_id_gen(dict.first).first);
        if (desc.second != DICTIONARY) throw pdf_error(FUNC_STRING + "/FontDescriptor must be dictionary");
        const map<string, pair<string, pdf_object_t>> desc_dict = get_dictionary_data(desc.first, 0);
        RETURN_SPACE_WIDTH_VALUE(desc_dict, "/AvgWidth");
        RETURN_SPACE_WIDTH_VALUE(desc_dict, "/MissingWidth");
        return NO_SPACE_WIDTH;
    }

    unsigned int get_space_width_from_widths(const ObjectStorage &storage, const pair<string, pdf_object_t> &array_arg)
    {
        unsigned int sum = 0, n = 0;
        const string array = (array_arg.second == ARRAY)? array_arg.first : get_indirect_array(array_arg.first, storage);
        const vector<pair<string, pdf_object_t>> result = get_array_data(array, 0);
        for (const pair<string, pdf_object_t> &p : result)
        {
            ++n;
            sum += strict_stoul(get_int(p.first));
        }
        return (n == 0)? NO_SPACE_WIDTH : (sum / n);
    }

    //https://www.adobe.com/content/dam/acom/en/devnet/pdf/pdfs/pdf_reference_archives/PDFReference.pdf
    // p. 340
    unsigned int get_space_width_from_w(const ObjectStorage &storage, const pair<string, pdf_object_t> &array_arg)
    {
        unsigned int sum = 0, n = 0;
        const string array = (array_arg.second == ARRAY)? array_arg.first :
                                                          get_indirect_array(array_arg.first, storage);
        const vector<pair<string, pdf_object_t>> result = get_array_data(array, 0);
        for (size_t i = 0; i < result.size(); i += 3)
        {
            switch (result.at(i + 2).second)
            {
            case VALUE:
            {
                unsigned int n_chars = strict_stoul(result[i + 1].first) - strict_stoul(result[i].first) + 1;
                n += n_chars;
                sum += n_chars * strict_stoul(get_int(result[i + 2].first));
                break;
            }
            case ARRAY:
            {
                const vector<pair<string, pdf_object_t>> w_array = get_array_data(result[i + 2].first, 0);
                for (const pair<string, pdf_object_t> &p : w_array)
                {
                    ++n;
                    sum += strict_stoul(get_int(p.first));
                }
                break;
            }
            default:
                throw pdf_error(FUNC_STRING + "wrong type for val " + result[i + 2].first +
                                " type=" + to_string(result[i + 2].second));
            }
        }
        return (n == 0)? NO_SPACE_WIDTH : (sum / n);
    }
}

//https://stackoverflow.com/questions/55147999/pdf-tj-operator/55180478
unsigned int CharsetConverter::get_space_width(const ObjectStorage &storage,
                                               const map<string, pair<string, pdf_object_t>> &font_dict)
{
    auto it = font_dict.find("/Widths");
    if (it != font_dict.end()) return get_space_width_from_widths(storage, it->second);
    it = font_dict.find("/W");
    if (it != font_dict.end()) return get_space_width_from_w(storage, it->second);
    RETURN_SPACE_WIDTH_VALUE(font_dict, "/DW");
    it = font_dict.find("/FontDescriptor");
    if (it != font_dict.end()) return get_space_width_from_font_descriptor(storage, it->second);

    return NO_SPACE_WIDTH;
}

string CharsetConverter::custom_decode_symbol(const string &s, size_t &i) const
{
    for (unsigned char n : custom_encoding->sizes)
    {
        size_t left = s.length() - i;
        if (left < n) break;
        auto it = custom_encoding->utf16_map.find(convert2uint(s.substr(i, n)));
        if (it == custom_encoding->utf16_map.end()) continue;
        i += n;
        return it->second;
    }
    ++i;
    return string();
}

string CharsetConverter::get_strings_from_array(const string &array) const
{
    string result;
    for (const pair<string, pdf_object_t> &p : get_array_data(array, 0))
    {
        switch (p.second)
        {
        case VALUE:
        {
            long int n = strict_stol(get_int(p.first));
            if (n > 0) continue;
            n = -n;
            if (n > get_space_width()) result += ' ';
            break;
        }
        case STRING:
            result += get_string(decode_string(p.first));
            break;
        default:
            throw pdf_error(FUNC_STRING + "wrong type " + to_string(p.second) + " val=" + p.first);
        }
    }
    return result;
}

string CharsetConverter::get_string(const string &s) const
{
    string result;
    switch (PDFencode)
    {
    case UTF8:
        return s;
    case IDENTITY:
        return to_utf<char>(s, "UTF-16be");
    case DEFAULT:
    case MAC_EXPERT:
    case MAC_ROMAN:
    case WIN:
    {
        const unordered_map<unsigned int, string> &standard_encoding = standard_encodings.at(PDFencode);
        result.reserve(s.length());
        for (char c : s)
        {
            auto it = standard_encoding.find(static_cast<unsigned char>(c));
            if (it != standard_encoding.end()) result.append(it->second);
        }
        return result;
    }
    case DIFFERENCE_MAP:
        result.reserve(s.length());
        for (char c : s)
        {
            auto it = difference_map.find(static_cast<unsigned char>(c));
            if (it != difference_map.end()) result.append(it->second);
        }
        return result;
    case OTHER:
        return to_utf<char>(s, charset);
    case TO_UNICODE:
    {
        string decoded;
        for (size_t i = 0; i < s.length(); decoded += custom_decode_symbol(s, i));
        //strings from cmap returned in little ordering
        return to_utf<char>(decoded, "UTF-16le");
    }
    }
}

CharsetConverter::CharsetConverter() noexcept :
                                  custom_encoding(nullptr),
                                  charset(nullptr),
                                  PDFencode(DEFAULT),
                                  difference_map(unordered_map<unsigned int, string>()),
                                  space_width(NO_SPACE_WIDTH)
{
}

CharsetConverter::CharsetConverter(const cmap_t *cmap_arg, unsigned int space_width_arg) :
                                   custom_encoding(cmap_arg),
                                   charset(nullptr),
                                   PDFencode(TO_UNICODE),
                                   difference_map(unordered_map<unsigned int, string>()),
                                   space_width(space_width_arg / SPACE_WIDTH_FRACTION)
{
}

CharsetConverter::CharsetConverter(unordered_map<unsigned int, string> &&difference_map, unsigned int space_width_arg) :
                                   custom_encoding(nullptr),
                                   charset(nullptr),
                                   PDFencode(DIFFERENCE_MAP),
                                   difference_map(std::move(difference_map)),
                                   space_width(space_width_arg / SPACE_WIDTH_FRACTION)
{
}

CharsetConverter::CharsetConverter(const string &encoding, unsigned int space_width_arg) :
                                   custom_encoding(nullptr),
                                   difference_map(unordered_map<unsigned int, string>()),
                                   space_width(space_width_arg / SPACE_WIDTH_FRACTION)
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

unique_ptr<CharsetConverter> CharsetConverter::get_from_dictionary(const map<string,
                                                                   pair<string, pdf_object_t>> &dictionary,
                                                                   const ObjectStorage &storage,
                                                                   unsigned int space_width)
{
    auto it = dictionary.find("/BaseEncoding");
    PDFEncode_t encoding;
    if (it == dictionary.end()) encoding = DEFAULT;
    else if (it->second.first == "/StandardEncoding") encoding = DEFAULT;
    else if (it->second.first == "/MacRomanEncoding") encoding = MAC_ROMAN;
    else if (it->second.first == "/MacExpertEncoding") encoding = MAC_EXPERT;
    else if (it->second.first == "/WinAnsiEncoding") encoding = WIN;
    else throw pdf_error(FUNC_STRING + "wrong /BaseEncoding value:" + it->second.first);
    it = dictionary.find("/Differences");
    if (it->second.second != ARRAY)
    {
        throw pdf_error(FUNC_STRING + "/Differences is not array. Type=" + to_string(it->second.second));
    }
    return CharsetConverter::get_diff_map_converter(encoding, it->second.first, storage, space_width);
}

string CharsetConverter::get_symbol_string(const string &name)
{
    auto it = symbol_table.find(name);
    return (it == symbol_table.end())? " " : it->second;
}

unique_ptr<CharsetConverter> CharsetConverter::get_diff_map_converter(PDFEncode_t encoding,
                                                                      const std::string &array,
                                                                      const ObjectStorage &storage,
                                                                      unsigned int space_width)
{
    unordered_map<unsigned int, string> code2symbol = standard_encodings.at(encoding);
    vector<pair<string, pdf_object_t>> array_data = get_array_data(array, 0);
    if (array_data.empty()) return unique_ptr<CharsetConverter>(new CharsetConverter(std::move(code2symbol), space_width));
    if (array_data[0].second != VALUE) throw pdf_error(FUNC_STRING + "wrong type in diff map array. type=" +
                                                       to_string(array_data[0].second) + " val=" + array_data[0].first);
    unsigned int code = strict_stoul(array_data[0].first);
    for (const pair<string, pdf_object_t> &p : array_data)
    {
        const pair<string, pdf_object_t> symbol = (p.second == INDIRECT_OBJECT)?
                                                  storage.get_object(get_id_gen(p.first).first) : p;
        switch (symbol.second)
        {
        case VALUE:
            code = strict_stoul(symbol.first);
            break;
        case NAME_OBJECT:
            code2symbol[code] = get_symbol_string(symbol.first);
            ++code;
            break;
        default:
            throw pdf_error(FUNC_STRING + "wrong symbol type=" + to_string(symbol.second) + " val=" + symbol.first);
        }
    }
    return unique_ptr<CharsetConverter>(new CharsetConverter(std::move(code2symbol), space_width));
}

unsigned int CharsetConverter::get_space_width() const
{
    return (space_width == NO_SPACE_WIDTH)? DEFAULT_SPACE_WIDTH : space_width;
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