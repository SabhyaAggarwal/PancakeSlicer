#ifndef slic3r_ASCIIFolding_hpp_
#define slic3r_ASCIIFolding_hpp_

#include <string>

namespace Slic3r {

extern std::string 	fold_utf8_to_ascii(const std::string &src,bool is_convert_for_filename=false);

extern wchar_t* fold_to_ascii(wchar_t c, wchar_t *out);

}

#endif
