#ifndef FONT_ENCODINGS_H
#define FONT_ENCODINGS_H

#include <string>

struct get_char_t
{
    std::string (*get_string)(const std::string &s, const std::string *encoding_table, const char *charset);
    const std::string *encoding_table;
    const char *charset;
};

extern const get_char_t standard_encoding;
extern const get_char_t identity_h_encoding;
extern const get_char_t identity_v_encoding;
extern const get_char_t mac_expert_encoding;
extern const get_char_t mac_roman_encoding;
extern const get_char_t win_ansi_encoding;
extern const get_char_t unicns_ucs2_h_encoding;
extern const get_char_t unicns_ucs2_v_encoding;
extern const get_char_t gbk_euc_h_encoding;
extern const get_char_t gbk_euc_v_encoding;
extern const get_char_t gb_h_encoding;
extern const get_char_t gb_v_encoding;
extern const get_char_t gb_euc_h_encoding;
extern const get_char_t gb_euc_v_encoding;
extern const get_char_t gbpc_euc_h_encoding;
extern const get_char_t gbpc_euc_v_encoding;
extern const get_char_t gbt_h_encoding;
extern const get_char_t gbt_v_encoding;
extern const get_char_t gbt_euc_h_encoding;
extern const get_char_t gbt_euc_v_encoding;
extern const get_char_t gbtpc_euc_h_encoding;
extern const get_char_t gbtpc_euc_v_encoding;
extern const get_char_t gbkp_euc_h_encoding;
extern const get_char_t gbkp_euc_v_encoding;
extern const get_char_t gbk2k_h_encoding;
extern const get_char_t gbk2k_v_encoding;
extern const get_char_t unigb_ucs2_h_encoding;
extern const get_char_t unigb_ucs2_v_encoding;
extern const get_char_t unigb_utf8_h_encoding;
extern const get_char_t unigb_utf8_v_encoding;
extern const get_char_t unigb_utf16_h_encoding;
extern const get_char_t unigb_utf16_v_encoding;
extern const get_char_t unigb_utf32_h_encoding;
extern const get_char_t unigb_utf32_v_encoding;
extern const get_char_t b5_h_encoding;
extern const get_char_t b5_v_encoding;
extern const get_char_t b5pc_h_encoding;
extern const get_char_t b5pc_v_encoding;
extern const get_char_t eten_b5_h_encoding;
extern const get_char_t eten_b5_v_encoding;
extern const get_char_t etenms_b5_h_encoding;
extern const get_char_t etenms_b5_v_encoding;
extern const get_char_t cns1_h_encoding;
extern const get_char_t cns1_v_encoding;
extern const get_char_t cns2_h_encoding;
extern const get_char_t cns2_v_encoding;
extern const get_char_t cns_euc_h_encoding;
extern const get_char_t cns_euc_v_encoding;
extern const get_char_t unicns_utf8_h_encoding;
extern const get_char_t unicns_utf8_v_encoding;
extern const get_char_t unicns_utf16_h_encoding;
extern const get_char_t unicns_utf16_v_encoding;
extern const get_char_t unicns_utf32_h_encoding;
extern const get_char_t unicns_utf32_v_encoding;
extern const get_char_t ethk_b5_h_encoding;
extern const get_char_t ethk_b5_v_encoding;
extern const get_char_t hkdla_b5_h_encoding;
extern const get_char_t hkdla_b5_v_encoding;
extern const get_char_t hkdlb_b5_h_encoding;
extern const get_char_t hkdlb_b5_v_encoding;
extern const get_char_t hkgccs_b5_h_encoding;
extern const get_char_t hkgccs_b5_v_encoding;
extern const get_char_t hkm314_b5_h_encoding;
extern const get_char_t hkm314_b5_v_encoding;
extern const get_char_t hkm471_b5_h_encoding;
extern const get_char_t hkm471_b5_v_encoding;
extern const get_char_t hkscs_b5_h_encoding;
extern const get_char_t hkscs_b5_v_encoding;
extern const get_char_t h_encoding;
extern const get_char_t v_encoding;
extern const get_char_t rksj_h_encoding;
extern const get_char_t rksj_v_encoding;
extern const get_char_t euc_h_encoding;
extern const get_char_t euc_v_encoding;
extern const get_char_t pv83_rksj_h_encoding;
extern const get_char_t pv83_rksj_v_encoding;
extern const get_char_t add_h_encoding;
extern const get_char_t add_v_encoding;
extern const get_char_t add_rksj_h_encoding;
extern const get_char_t add_rksj_v_encoding;
extern const get_char_t ext_h_encoding;
extern const get_char_t ext_v_encoding;
extern const get_char_t ext_rksj_h_encoding;
extern const get_char_t ext_rksj_v_encoding;
extern const get_char_t nwp_h_encoding;
extern const get_char_t nwp_v_encoding;
extern const get_char_t pv90_rksj_h_encoding;
extern const get_char_t pv90_rksj_v_encoding;
extern const get_char_t ms90_rksj_h_encoding;
extern const get_char_t ms90_rksj_v_encoding;
extern const get_char_t msp90_rksj_h_encoding;
extern const get_char_t msp90_rksj_v_encoding;
extern const get_char_t h78_encoding;
extern const get_char_t v78_encoding;
extern const get_char_t rksj78_h_encoding;
extern const get_char_t rksj78_v_encoding;
extern const get_char_t ms78_rksj_h_encoding;
extern const get_char_t ms78_rksj_v_encoding;
extern const get_char_t euc78_h_encoding;
extern const get_char_t euc78_v_encoding;
extern const get_char_t unijis_ucs2_h_encoding;
extern const get_char_t unijis_ucs2_v_encoding;
extern const get_char_t unijis_ucs2_hw_h_encoding;
extern const get_char_t unijis_ucs2_hw_v_encoding;
extern const get_char_t unijis_utf8_h_encoding;
extern const get_char_t unijis_utf8_v_encoding;
extern const get_char_t unijis_utf16_h_encoding;
extern const get_char_t unijis_utf16_v_encoding;

#endif //FONT_ENCODINGS_H
