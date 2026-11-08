#ifndef slic3r_Format_AMF_hpp_
#define slic3r_Format_AMF_hpp_

namespace Slic3r {

class Model;
class DynamicPrintConfig;
struct ConfigSubstitutionContext;

extern bool load_amf(const char* path, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, Model* model, bool* use_inches);

}

#endif
