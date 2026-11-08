// ================================================================================================
// File: material.cpp
// Brief: Scan retail .mtr declarations for what the texture cook needs from each material, its
// diffuse image and whether that stage alpha-tests, and name the BIMAGE files id generated for an
// image, so the cook reads exactly the pixels the engine would load.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/material.h"

#include <cctype>
#include <cstdio>
#include <vector>

namespace assets
{
namespace
{

static constexpr int kMaxDepth = 16;

struct Token
{
    std::string text;
    bool quoted = false;
    int line = 0;
};

static std::string Lower(std::string text)
{
    for (char & character : text)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return text;
}

// id's lexer with path names allowed: punctuation splits words, '/' '.' '-' ':' stay inside them,
// comments and quotes as usual.
static bool Tokenize(std::string_view text, std::vector<Token> & tokens, std::string & error)
{
    static constexpr std::string_view kPunctuation = "{}()[],;";
    std::size_t at = 0;
    int line = 1;

    while (at < text.size())
    {
        const char character = text[at];

        if (character == '\n')
        {
            ++line;
            ++at;
        }
        else if (std::isspace(static_cast<unsigned char>(character)) != 0)
        {
            ++at;
        }
        else if (text.compare(at, 2, "//") == 0)
        {
            while (at < text.size() && text[at] != '\n')
            {
                ++at;
            }
        }
        else if (text.compare(at, 2, "/*") == 0)
        {
            const std::size_t end = text.find("*/", at + 2);

            if (end == std::string_view::npos)
            {
                error = "unterminated comment at line " + std::to_string(line);
                return false;
            }

            for (std::size_t index = at; index < end; ++index)
            {
                line += text[index] == '\n' ? 1 : 0;
            }

            at = end + 2;
        }
        else if (character == '"')
        {
            const std::size_t end = text.find('"', at + 1);

            if (end == std::string_view::npos)
            {
                error = "unterminated string at line " + std::to_string(line);
                return false;
            }

            tokens.push_back({ std::string(text.substr(at + 1, end - at - 1)), true, line });
            at = end + 1;
        }
        else if (kPunctuation.find(character) != std::string_view::npos)
        {
            tokens.push_back({ std::string(1, character), false, line });
            ++at;
        }
        else
        {
            const std::size_t start = at;

            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) == 0 && kPunctuation.find(text[at]) == std::string_view::npos &&
                   text[at] != '"' && text.compare(at, 2, "//") != 0 && text.compare(at, 2, "/*") != 0)
            {
                ++at;
            }

            tokens.push_back({ std::string(text.substr(start, at - start)), false, line });
        }
    }

    return true;
}

static bool Is(const Token & token, std::string_view text)
{
    return !token.quoted && token.text == text;
}

// An image argument: a name, or a program 'op( ... )' rebuilt as its source text without spaces.
static std::string ImageArgument(const std::vector<Token> & tokens, std::size_t & at)
{
    if (at >= tokens.size())
    {
        return {};
    }

    std::string image = tokens[at].text;
    ++at;

    if (at < tokens.size() && Is(tokens[at], "("))
    {
        int depth = 0;

        while (at < tokens.size())
        {
            const Token & token = tokens[at++];
            image += token.text;
            depth += Is(token, "(") ? 1 : Is(token, ")") ? -1 : 0;

            if (depth == 0)
            {
                break;
            }
        }
    }

    return image;
}

struct Stage
{
    std::string blend;
    std::string map;
    bool alphaTest = false;
};

static void CloseStage(const Stage & stage, MaterialInfo & info)
{
    const std::string blend = Lower(stage.blend);

    if (blend == "diffusemap")
    {
        if (info.diffuse.empty())
        {
            info.diffuse = stage.map;
            info.alphaTest = stage.alphaTest;
        }
    }
    else if (blend != "bumpmap" && blend != "specularmap")
    {
        ++info.otherStages;
    }
}

} // namespace

bool ScanMaterials(std::string_view text, std::map<std::string, MaterialInfo> & materials, std::string & error)
{
    std::vector<Token> tokens;

    if (!Tokenize(text, tokens, error))
    {
        return false;
    }

    std::size_t at = 0;

    while (at < tokens.size())
    {
        // [type] name { body }
        bool material = true;
        std::string type = Lower(tokens[at].text);

        if (type == "material" || type == "table" || type == "skin" || type == "sound" || type == "particle" || type == "fx" ||
            type == "entitydef" || type == "model" || type == "mapdef" || type == "pda" || type == "email" || type == "video" ||
            type == "audio")
        {
            material = type == "material";
            ++at;
        }

        if (at + 1 >= tokens.size() || !Is(tokens[at + 1], "{"))
        {
            error = "declaration without a name and body at line " + std::to_string(at < tokens.size() ? tokens[at].line : 0);
            return false;
        }

        const std::string name = Lower(tokens[at].text);
        at += 2;
        MaterialInfo info;
        Stage stage;
        int depth = 1;

        while (at < tokens.size() && depth > 0)
        {
            const Token & token = tokens[at];
            const std::string word = token.quoted ? std::string() : Lower(token.text);

            if (Is(token, "{"))
            {
                if (++depth > kMaxDepth)
                {
                    error = "material " + name + " nests too deeply";
                    return false;
                }

                if (depth == 2)
                {
                    stage = {};
                }

                ++at;
            }
            else if (Is(token, "}"))
            {
                if (depth == 2)
                {
                    CloseStage(stage, info);
                }

                --depth;
                ++at;
            }
            else if (depth == 1 && (word == "diffusemap" || word == "bumpmap" || word == "specularmap"))
            {
                ++at;
                Stage shorthand;
                shorthand.blend = word;
                shorthand.map = ImageArgument(tokens, at);
                CloseStage(shorthand, info);
            }
            else if (depth == 2 && word == "blend" && at + 1 < tokens.size())
            {
                stage.blend = tokens[at + 1].text;
                at += 2;
            }
            else if (depth == 2 && word == "map")
            {
                ++at;
                stage.map = ImageArgument(tokens, at);
            }
            else if (depth == 2 && word == "alphatest")
            {
                stage.alphaTest = true;
                ++at;
            }
            else
            {
                ++at;
            }
        }

        if (depth != 0)
        {
            error = "declaration " + name + " lacks its closing brace";
            return false;
        }

        if (material)
        {
            materials.emplace(name, std::move(info));
        }
    }

    return true;
}

std::string GeneratedImageName(const std::string & image, int usage)
{
    std::string name = Lower(image);

    for (char & character : name)
    {
        character = character == '\\' ? '/' : character;
    }

    // idImageManager::ImageFromFile strips the extension: the text after the last '.' that follows
    // the last '/'.
    const std::size_t dot = name.rfind('.');
    const std::size_t slash = name.rfind('/');

    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
    {
        name.resize(dot);
    }

    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "#__%02d00", usage);
    name += suffix;

    std::string path = "generated/images/";

    for (const char character : name)
    {
        if (character == '(' || character == ',')
        {
            path += '/';
        }
        else if (character != ')' && character != ' ')
        {
            path += character;
        }
    }

    return path + ".bimage";
}

} // namespace assets
