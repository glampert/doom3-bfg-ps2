// ================================================================================================
// File: material.h
// Brief: Scan retail .mtr declarations for what the cooks need from each material: its diffuse image
// and whether that stage alpha-tests, whether it receives and casts light as id decides it, and a
// light material's stages with their register expressions (id's grammar, evaluated in the tool),
// and name the BIMAGE files id generated for an image, so the cooks read exactly what the engine
// would load.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{

// textureUsage_t (Image.h): the two digits after "#__" in a generated image name.
static constexpr int kTextureUsageDiffuse = 1;
static constexpr int kTextureUsageDefault = 2;
static constexpr int kTextureUsageLight = 5;
static constexpr int kTextureUsageCoverage = 10;

// ------------------------------------------------------------------------------------------------
// Register expressions
// ------------------------------------------------------------------------------------------------

// idMaterial::ParseExpression's operators. Each priority level is right-associative, as id parses
// it: 'a - b - c' is a - (b - c).
enum class ExpressionOp : std::uint8_t
{
    Constant,
    Time,
    Parm,
    Global,
    Sound,
    Table,
    Add,
    Subtract,
    Multiply,
    Divide,
    Mod,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Equal,
    NotEqual,
    And,
    Or
};

struct ExpressionNode
{
    ExpressionOp op = ExpressionOp::Constant;
    float value = 0.0f;
    // The parm or global register, or nothing.
    int index = 0;
    // Operand nodes; a table lookup's index is 'a'.
    int a = -1;
    int b = -1;
    std::string table;
};

// An empty expression stands for the register's default.
struct MaterialExpression
{
    std::vector<ExpressionNode> nodes;
    int root = -1;

    bool Empty() const
    {
        return root < 0;
    }
};

// A table declaration as idDeclTable stores it: the values with the first repeated at the end.
struct MaterialTable
{
    bool snap = false;
    bool clamp = false;
    std::vector<float> values;
};

using MaterialTables = std::map<std::string, MaterialTable>;

// What an expression reads, as bits.
static constexpr unsigned kExpressionReadsTime = 1U;
static constexpr unsigned kExpressionReadsSound = 2U;
static constexpr unsigned kExpressionReadsParms = 4U;
static constexpr unsigned kExpressionReadsGlobals = 8U;
static constexpr unsigned kExpressionReadsTables = 16U;

struct ExpressionInputs
{
    std::array<float, 12> parms = {};
    std::array<float, 8> globals = {};
    float time = 0.0f;
    float sound = 0.0f;
    const MaterialTables * tables = nullptr;
};

// Parses one expression, the whole text (for tests and job files).
bool ParseMaterialExpression(std::string_view text, MaterialExpression & output, std::string & error);

unsigned ExpressionReads(const MaterialExpression & expression);

// idMaterial::EvaluateRegisters for one expression; an empty one gives 'fallback'. A table that
// is not declared reads 1, as an idDeclTable of under two values does.
float EvaluateExpression(const MaterialExpression & expression, const ExpressionInputs & inputs, float fallback = 1.0f);

// ------------------------------------------------------------------------------------------------
// Materials
// ------------------------------------------------------------------------------------------------

struct MaterialStage
{
    // Lowercase: "diffusemap" and the like, a blend shorthand, or "gl_src,gl_dst"; empty for none.
    std::string blend;
    std::string map;
    bool alphaTest = false;
    // The alphaTest register: a fragment draws when its alpha is at least this.
    MaterialExpression alphaTestValue;
    // The image's clamp mode as id reads it: 'clamp' or 'zeroClamp' (black outside [0, 1]).
    bool clamp = false;
    bool zeroClamp = false;
    // A texture matrix (translate, scroll, scale, centerScale, shear, rotate).
    bool transformed = false;
    // An image no cooked texture stands for: a cube map, texgen, a video, sound or render map, or a
    // vertex or fragment program.
    bool special = false;
    // 'if': the stage draws while it is non-zero.
    MaterialExpression condition;
    // RGBA registers ('colored' is parm0..3); empty ones are 1.
    std::array<MaterialExpression, 4> color;
};

struct MaterialInfo
{
    // The first diffuse stage's image as the material names it ('diffusemap X' or a stage with
    // 'blend diffusemap' and 'map X'), image programs included; empty without one.
    std::string diffuse;
    // That stage alpha-tests: id loads its coverage image too (Material.cpp's coverage stage).
    bool alphaTest = false;
    // That stage's index in 'stages', or -1.
    int diffuseStage = -1;
    // The first bump stage's image ('bumpmap X' or 'blend bumpmap'), empty without one.
    std::string bump;
    // Stages that are neither diffuse, bump nor specular (blend, add, filter and the like).
    int otherStages = 0;
    // An interaction stage (diffuse, bump or specular): lights reach the surface.
    bool lit = false;
    // idMaterial::SurfaceCastsShadow: 'forceShadows', or neither 'noShadows', a sprite, tube or
    // flare deform nor translucent coverage (the keyword, no stages, or no interaction with a first
    // stage that blends with the destination). Alpha-tested surfaces cast whole shadows, as id's
    // shadow volumes do.
    bool castsShadow = true;
    bool translucent = false;
    // Light material keywords.
    std::string lightFalloffImage;
    bool fogLight = false;
    bool blendLight = false;
    bool ambientLight = false;
    // Every stage in order, the shorthand stages ('diffusemap X') included.
    std::vector<MaterialStage> stages;
    // The first expression that failed to parse; its register keeps the default.
    std::string expressionError;
};

// Adds the materials of one .mtr file to 'materials', keyed by lowercase name; a name already
// present keeps its first definition. With 'tables', table declarations are added there the same
// way; skins and the other declaration types are skipped. This scans what the cooks need; it is not
// id's material parser.
bool ScanMaterials(std::string_view text, std::map<std::string, MaterialInfo> & materials, std::string & error,
                   MaterialTables * tables = nullptr);

// The resource id reads for an image loaded with 'usage' (idImage::GetGeneratedName, then
// idBinaryImage::GetGeneratedFileName): "generated/images/<name>#__UU00.bimage", the name lowercased
// without its extension, '(' and ',' as '/', ')' and spaces dropped.
std::string GeneratedImageName(const std::string & image, int usage);

} // namespace assets
