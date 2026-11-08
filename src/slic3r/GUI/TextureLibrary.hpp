#ifndef slic3r_TextureLibrary_hpp_
#define slic3r_TextureLibrary_hpp_

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI {

struct TextureLibraryEntry
{
    std::string name;
    std::string path;
    bool        is_user;
};

const std::vector<TextureLibraryEntry> &texture_library(bool force_rescan = false);

void touch_user_texture(const std::string &path);

bool remove_user_texture(const std::string &path, std::string &error);

std::string user_texture_dir();

std::optional<TextureLibraryEntry> import_texture_to_library(const std::string &source_path, std::string &error);

std::shared_ptr<std::vector<unsigned char>> load_texture_image_data(const std::string &path, std::string &error);

}

#endif
