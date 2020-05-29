#ifndef PAGES_EXTRACTOR_H
#define PAGES_EXTRACTOR_H

#include <string>
#include <utility>
#include <map>
#include <memory>
#include <unordered_set>

#include <boost/optional.hpp>

#include "common.h"
#include "object_storage.h"
#include "cmap.h"
#include "fonts.h"
#include "coordinates.h"
#include "diff_converter.h"
#include "to_unicode_converter.h"
#include "converter_engine.h"

enum {RECTANGLE_ELEMENTS_NUM = 4};
using mediabox_t = std::array<double, RECTANGLE_ELEMENTS_NUM>;

class PagesExtractor
{
public:
    PagesExtractor(unsigned int catalog_pages_id,
                   const ObjectStorage &storage_arg,
                   const dict_t &decrypt_data_arg,
                   const std::string &doc_arg);
    std::string get_text();
private:
    DiffConverter get_diff_converter(const boost::optional<std::pair<std::string, pdf_object_t>> &encoding) const;
    ToUnicodeConverter get_to_unicode_converter(const dict_t &font_dict);
    boost::optional<mediabox_t> get_box(const dict_t &dictionary,
                                        const boost::optional<mediabox_t> &parent_media_box) const;
    mediabox_t parse_rectangle(const std::pair<std::string, pdf_object_t> &rectangle) const;
    std::vector<std::vector<text_chunk_t>> extract_text(const std::string &page_content,
                                                        const std::string &resource_id,
                                                        const boost::optional<matrix_t> CTM);
    void get_pages_resources_int(std::unordered_set<unsigned int> &checked_nodes,
                                 const dict_t &parent_dict,
                                 const dict_t &parent_fonts,
                                 const boost::optional<mediabox_t> &parent_media_box,
                                 unsigned int parent_rotate);
    dict_t get_fonts(const dict_t &dictionary, const dict_t &parent_fonts) const;
    std::unique_ptr<ConverterEngine> get_font_encoding(const std::string &font, const std::string &resource_id);
    boost::optional<std::pair<std::string, pdf_object_t>> get_encoding(const dict_t &font_dict) const;
    void get_XObjects_data(const std::string &page_id,
                           const dict_t &page,
                           const dict_t &parent_fonts,
                           std::unordered_set<unsigned int> &visited_XObjects);
    void get_XObject_data(const std::string &page_id,
                          const dict_t::value_type &XObject,
                          const dict_t &parent_fonts,
                          std::unordered_set<unsigned int> &visited_XObjects);
private:
    const std::string &doc;
    const ObjectStorage &storage;
    const dict_t &decrypt_data;
    std::map<std::string, Fonts> fonts;
    std::vector<unsigned int> pages;
    std::map<std::string, mediabox_t> media_boxes;
    std::map<std::string, unsigned int> rotates;
    std::map<unsigned int, cmap_t> cmap_storage;
    std::map<std::string, std::string> XObject_streams;
    std::map<std::string, matrix_t> XObject_matrices;
};

#endif //PAGES_EXTRACTOR_H
