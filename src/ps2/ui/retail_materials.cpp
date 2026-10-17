// ================================================================================================
// File: retail_materials.cpp
// Brief: Generate only cataloged constant UI stages instead of reading general retail declarations.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/log.h"
#include "ps2/ui/retail_assets.h"

#include <idlib/precompiled.h>
#include <renderer/Image.h>
#include <renderer/Material.h>

namespace ps2::ui::retail
{
PS2_COLD_FUNC bool MaterialText(const char * name, char * output, size_t capacity)
{
    if (!IsInitialized() || name == nullptr || output == nullptr || capacity == 0)
    {
        return false;
    }

    const auto & catalog = GetCatalog();

    for (std::uint32_t i = 0; i < catalog.MaterialCount(); ++i)
    {
        MaterialRecord material;
        catalog.GetMaterial(i, material);

        if (strcmp(name, material.name) != 0)
        {
            continue;
        }

        ImageRecord image;

        if (!catalog.GetImage(material.imageIndex, image))
        {
            FatalError("retail material image missing");
        }

        const int length = idStr::snPrintf(output, static_cast<int>(capacity),
                                           "material %s\n{\n{\nblend blend\nmap \"%s\"\nlinear\nclamp\nvertexColor\n}\n}\n",
                                           material.name, image.name);

        if (length <= 0 || static_cast<size_t>(length) >= capacity)
        {
            FatalError("retail material definition exceeds bounded text");
        }

        return true;
    }

    return false;
}

PS2_COLD_FUNC void RegisterMaterials()
{
    if (!IsInitialized() || rs::FrameStarted())
    {
        FatalError("invalid retail material registration ownership");
    }

    const auto & catalog = GetCatalog();

    // Font registration parses only its own atlas material. Other catalog images stay unopened
    // until requested, so a resource probe does not eagerly reserve the complete menu profile.
    for (std::uint32_t i = 0; i < catalog.FontCount(); ++i)
    {
        FontRecord font;
        catalog.GetFont(i, font);
        idStr expected(font.logicalPath);
        expected.StripFileExtension();
        bool found = false;

        for (std::uint32_t j = 0; j < catalog.MaterialCount(); ++j)
        {
            MaterialRecord material;
            catalog.GetMaterial(j, material);
            found = found || (strcmp(material.name, expected.c_str()) == 0 && material.imageIndex == font.imageIndex);
        }

        if (!found)
        {
            FatalError("retail font material binding missing: %s", font.family);
        }
    }
}
} // namespace ps2::ui::retail
