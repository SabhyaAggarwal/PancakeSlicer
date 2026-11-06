#ifndef slic3r_Format_3mf_hpp_
#define slic3r_Format_3mf_hpp_
#include <expat.h>
#include <string>

namespace Slic3r {

class PrusaFileParser
{
public:
    PrusaFileParser() {}
    ~PrusaFileParser() {}

    bool check_3mf_from_prusa(const std::string filename);
    void _start_element_handler(const char *name, const char **attributes);
    void _characters_handler(const XML_Char *s, int len);

private:
    const char *get_attribute_value_charptr(const char **attributes, unsigned int attributes_size, const char *attribute_key);
    std::string get_attribute_value_string(const char **attributes, unsigned int attributes_size, const char *attribute_key);

    static void XMLCALL start_element_handler(void *userData, const char *name, const char **attributes);
    static void XMLCALL characters_handler(void *userData, const XML_Char *s, int len);
private:
    bool       m_from_prusa         = false;
    bool       m_is_application_key = false;
    XML_Parser m_parser;
};














    enum {
        support_points_format_version = 1
    };

    enum {
        drain_holes_format_version = 1
    };

    class Model;
    struct ConfigSubstitutionContext;
    class DynamicPrintConfig;
    struct ThumbnailData;


    extern bool load_3mf(const char* path, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, Model* model, bool check_version);



    extern bool store_3mf(const char* path, Model* model, const DynamicPrintConfig* config, bool fullpath_sources, const ThumbnailData* thumbnail_data = nullptr, bool zip64 = true);

}

#endif
