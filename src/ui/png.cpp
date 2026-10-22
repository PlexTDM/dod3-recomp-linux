/* A PNG as RGBA, for the launcher's picture (src/ui/launcher.cpp): stb_image,
 * the copy the runtime's codec library carries, compiled here as a private
 * copy of its own. */
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace dod3setup {

bool png_rgba(const std::vector<uint8_t>& file, std::vector<uint8_t>* rgba, int* w, int* h)
{
    int n = 0;
    unsigned char* px = stbi_load_from_memory(file.data(), (int)file.size(), w, h, &n, 4);
    if (!px) return false;
    rgba->assign(px, px + (size_t)*w * *h * 4);
    stbi_image_free(px);
    return true;
}

}  // namespace dod3setup
