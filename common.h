#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
#include <stack>
#include <array>

#include <boost/optional.hpp>
#define LEN(S) (sizeof(S) - 1)

enum pdf_object_t { DICTIONARY = 1, ARRAY = 2, STRING = 3, VALUE = 4, INDIRECT_OBJECT = 5, NAME_OBJECT = 6 };
enum { MATRIX_ELEMENTS = 6 };

#define FUNC_STRING (std::string(__func__) + ": ")
extern const std::map<pdf_object_t, std::string (&)(const std::string&, size_t&)> TYPE2FUNC;
class ObjectStorage;

class pdf_error : public std::runtime_error
{
public:
    pdf_error(const char* what) : runtime_error(what)
    {
    }

    pdf_error(const std::string &what) : runtime_error(what)
    {
    }
};

using dict_t = std::map<std::string, std::pair<std::string, pdf_object_t>>;


size_t efind_first(const std::string &src, const std::string& str, size_t pos);
size_t efind_first(const std::string &src, const char* s, size_t pos);
size_t efind_first(const std::string &src, const char* s, size_t pos, size_t n);
size_t efind_first_not(const std::string &src, const std::string& str, size_t pos);
size_t efind_first_not(const std::string &src, const char* s, size_t pos);
size_t efind_first_not(const std::string &src, const char* s, size_t pos, size_t n);
size_t efind(const std::string &src, const std::string& str, size_t pos);
size_t efind(const std::string &src, const char* s, size_t pos);
size_t efind(const std::string &src, char c, size_t pos);
size_t skip_spaces(const std::string &buffer, size_t offset, bool validate = true);
size_t skip_comments(const std::string &buffer, size_t offset);
pdf_object_t get_object_type(const std::string &buffer, size_t &offset);
std::string get_value(const std::string &buffer, size_t &offset);
std::string get_array(const std::string &buffer, size_t &offset);
std::string get_name_object(const std::string &buffer, size_t &offset);
std::string get_indirect_object(const std::string &buffer, size_t &offset);
std::string get_string(const std::string &buffer, size_t &offset);
std::string get_dictionary(const std::string &buffer, size_t &offset);
std::string decode_string(const std::string &str);
size_t strict_stoul(const std::string &str, int base = 10);
long int strict_stol(const std::string &str, int base = 10);
std::string predictor_decode(const std::string &data, const dict_t &opts);
dict_t get_dictionary_data(const std::string &buffer, size_t offset);
std::vector<std::pair<unsigned int, unsigned int>> get_set(const std::string &array);
std::pair<std::string, pdf_object_t> get_object(const std::string &buffer,
                                                size_t id,
                                                const std::map<size_t, size_t> &id2offsets);
std::string get_stream(const std::string &doc,
                       const std::pair<unsigned int, unsigned int> &id_gen,
                       const ObjectStorage &storage,
                       const dict_t &encrypt_data);
std::string get_content(const std::string &buffer, size_t len, size_t offset);
std::string decode(const std::string &content, const dict_t &props);
size_t find_number(const std::string &buffer, size_t offset);
size_t efind_number(const std::string &buffer, size_t offset);
size_t get_length(const std::string &buffer,
                  const std::map<size_t, size_t> &id2offsets,
                  const dict_t &props);
std::pair<unsigned int, unsigned int> get_id_gen(const std::string &data);
std::pair<std::string, pdf_object_t> get_indirect_object_data(const std::string &indirect_object,
                                                              const ObjectStorage &storage,
                                                              boost::optional<pdf_object_t> type = boost::none);
std::vector<std::pair<std::string, pdf_object_t>> get_array_data(const std::string &buffer, size_t offset);
template <class T> T pop(std::stack<T> &st)
{
    if (st.empty()) throw pdf_error(FUNC_STRING + "stack is empty");
    T result = st.top();
    st.pop();
    return result;
}
std::pair<double, double> apply_matrix_norm(const std::array<double, MATRIX_ELEMENTS> &matrix,
                                            const std::pair<double, double> &point);
std::string get_dict_val(const dict_t &dict, const std::string &key, const std::string &def);
#endif //COMMON
