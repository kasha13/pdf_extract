#include <utility>
#include <string>
#include <vector>

#include <boost/optional.hpp>

#include "converter_engine.h"
#include "coordinates.h"
#include "fonts.h"
#include "common.h"

using namespace std;

ConverterEngine::ConverterEngine(CharsetConverter &&charset_converter_arg,
                                 DiffConverter &&diff_converter_arg,
                                 ToUnicodeConverter &&to_unicode_converter_arg) :
    charset_converter(std::move(charset_converter_arg)),
    diff_converter(std::move(diff_converter_arg)),
    to_unicode_converter(std::move(to_unicode_converter_arg))
{
}

bool ConverterEngine::is_vertical() const
{
    if (charset_converter.is_vertical()) return true;
    if (to_unicode_converter.is_empty()) return false;
    return to_unicode_converter.is_vertical();
}

text_chunk_t ConverterEngine::get_string(const string &s, Coordinates &coordinates, float Tj, const Fonts &fonts) const
{
    if (to_unicode_converter.is_empty())
    {
        pair<string, float> p = diff_converter.is_empty()? charset_converter.get_string(s, fonts) :
                                                           diff_converter.get_string(s, fonts);
        return coordinates.adjust_coordinates(std::move(p.first), s.length(), p.second, Tj, fonts);

    }
    string decoded;
    float decoded_width = 0;
    size_t len = 0;
    for (size_t i = 0; i < s.length();)
    {
        pair<string, float> decoded_symbol = to_unicode_converter.custom_decode_symbol(s, i, fonts);
        if (decoded_symbol.first.empty())
        {
            boost::optional<string> c = (diff_converter.is_empty())? charset_converter.get_char(s[i]) :
                                                                     diff_converter.get_char(s[i]);
            if (c)
            {
                decoded += *c;
                decoded_width += fonts.get_width(static_cast<unsigned char>(s[i]));
                ++len;
            }
            ++i;
        }
        else
        {
            decoded_width += decoded_symbol.second;
            len += utf8_length(decoded_symbol.first);
            decoded += std::move(decoded_symbol.first);
        }
    }
    return coordinates.adjust_coordinates(std::move(decoded), len, decoded_width, Tj, fonts);
}

vector<text_chunk_t> ConverterEngine::get_strings_from_array(const string &array,
                                                             Coordinates &coordinates,
                                                             const Fonts &fonts) const
{
    vector<text_chunk_t> result;
    float Tj = 0;
    array_t array_data = get_array_data(array, 0);
    result.reserve(array_data.size());
    for (const array_t::value_type &p : array_data)
    {
        switch (p.second)
        {
        case VALUE:
            Tj = stof(p.first);
            break;
        case STRING:
        {
            text_chunk_t chunk = get_string(decode_string(p.first), coordinates, Tj, fonts);
            if (!chunk.is_empty) result.push_back(std::move(chunk));
            Tj = 0;
            break;
        }
        default:
            throw pdf_error(FUNC_STRING + "wrong type " + to_string(p.second) + " val=" + p.first);
        }
    }
    return result;
}
