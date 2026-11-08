// ================================================================================================
// File: lighting_tests.cpp
// Brief: Check the light cook against id's rules and analytic answers: the material expression
// grammar and tables, light materials and coverage, .map entities, render lights with their
// projections, bounds and classes, a light's value at a point, lightmaps baked over a small
// scene (intensity, chroma, shadows, pages, per-vertex light for alpha-tested surfaces), and static
// func_static models merged into the areas they stand in.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/light.h"
#include "assets/lightmap.h"
#include "assets/map_entities.h"
#include "assets/material.h"
#include "assets/static_models.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace
{

using namespace assets;

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }

    return passed;
}

static bool Near(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

static float Evaluate(const char * text, const ExpressionInputs & inputs = {})
{
    MaterialExpression expression;
    std::string error;

    if (!ParseMaterialExpression(text, expression, error))
    {
        std::fprintf(stderr, "expression '%s': %s\n", text, error.c_str());
        return -12345.0f;
    }

    return EvaluateExpression(expression, inputs);
}

// ------------------------------------------------------------------------------------------------
// Expressions and materials
// ------------------------------------------------------------------------------------------------

static bool Expressions()
{
    ExpressionInputs inputs;
    inputs.parms[0] = 0.5f;
    inputs.parms[7] = 2.0f;
    inputs.time = 0.25f;

    // id's priorities (* / % over + - over comparisons over && ||), each level right-associative.
    bool passed = Evaluate("1 + 2 * 3") == 7.0f && Evaluate("( 1 + 2 ) * 3") == 9.0f && Evaluate("1 - 2 - 3") == 2.0f &&
                  Evaluate("8 / 4 / 2") == 4.0f && Evaluate("7 % 4") == 3.0f && Evaluate("-2 * 3") == -6.0f;
    passed = passed && Evaluate("parm0 * 4", inputs) == 2.0f && Evaluate("Parm7 == 2", inputs) == 1.0f && Evaluate("parm7 != 2", inputs) == 0.0f &&
             Evaluate("1 < 2 && 3 >= 3") == 1.0f && Evaluate("0 || 0") == 0.0f && Evaluate("time*4", inputs) == 1.0f &&
             Evaluate("fragmentPrograms") == 1.0f;

    // Tables, as idDeclTable looks them up: interpolated and wrapping, snapped, clamped.
    MaterialTables tables;
    std::map<std::string, MaterialInfo> materials;
    std::string error;
    passed = passed && ScanMaterials("table ramp { { 0, 1 } } table steps { snap { 0, 10, 20, 30 } } table edge { clamp { 2, 4, 6 } }", materials,
                                     error, &tables) &&
             tables.size() == 3 && tables["ramp"].values.size() == 3;
    inputs.tables = &tables;
    passed = passed && Near(Evaluate("ramp[ 0.25 ]", inputs), 0.5f, 1e-6f) && Near(Evaluate("ramp[ 1.25 ]", inputs), 0.5f, 1e-6f) &&
             Evaluate("steps[ 0.6 ]", inputs) == 20.0f && Evaluate("edge[ -1 ]", inputs) == 2.0f && Evaluate("edge[ 5 ]", inputs) == 6.0f &&
             Evaluate("missing[ 0 ]", inputs) == 1.0f;

    MaterialExpression expression;
    passed = passed && ParseMaterialExpression("sintable[ time * parm3 ] * parm0", expression, error) &&
             ExpressionReads(expression) == (kExpressionReadsTime | kExpressionReadsParms | kExpressionReadsTables);
    passed = passed && !ParseMaterialExpression("1 +", expression, error) && !ParseMaterialExpression("( 1", expression, error) &&
             !ParseMaterialExpression("bogus", expression, error) && !ParseMaterialExpression("1 2", expression, error);
    return Check(passed, "material expressions follow id's grammar, priorities and tables");
}

static bool LightMaterials()
{
    static constexpr const char * kText = R"(
lights/round
{
    lightFalloffImage makeintensity( lights/squarelight1a.tga )
    {
        forceHighQuality
        map lights/round.tga
        colored
        zeroClamp
    }
}
lights/pulse
{
    { if ( parm7 == 0 )
        map lights/a.tga
        red ( sintable[ time ] ) * Parm0
        green 0.5
        blue parm2
    }
    {
        map lights/b.tga
        rgb 0.25
        translate time , 0
    }
}
fogs/thick { fogLight noShadows { map _fog rgb 1 } }
textures/glass { { blend blend map textures/glass.tga } }
textures/wall { diffusemap textures/wall.tga }
textures/grate { { blend diffusemap map textures/grate.tga alphatest 0.5 } }
textures/shadowed { translucent forceShadows { blend add map x.tga } }
textures/sky { noShadows { map sky.tga } }
textures/filter { { blend gl_dst_color , gl_zero map f.tga } }
textures/opaque { { blend gl_one , gl_zero map o.tga } }
textures/next { { map n.tga alpha parm4
} }
)";
    std::map<std::string, MaterialInfo> materials;
    std::string error;
    bool passed = ScanMaterials(kText, materials, error) && materials.size() == 11;

    if (!passed)
    {
        std::fprintf(stderr, "scan: %s\n", error.c_str());
        return Check(false, "light materials and coverage scan as id reads them");
    }

    const MaterialInfo & round = materials["lights/round"];
    passed = round.lightFalloffImage == "makeintensity(lights/squarelight1a.tga)" && round.stages.size() == 1 && round.stages[0].zeroClamp &&
             round.stages[0].map == "lights/round.tga" && round.expressionError.empty();

    RenderLight light;
    light.parms = { 0.25f, 0.5f, 0.75f, 1.0f };
    const std::vector<LightStageColor> colors = EvaluateLightStages(round, light, nullptr, 3.0f);
    passed = passed && colors.size() == 1 && colors[0].zeroClamp && Near(colors[0].color[0], 0.75f, 1e-6f) && Near(colors[0].color[2], 2.25f, 1e-6f);

    // A stage whose condition fails is skipped; the other draws with its constant colour.
    const MaterialInfo & pulse = materials["lights/pulse"];
    light.parms[7] = 1.0f;
    const std::vector<LightStageColor> pulsed = EvaluateLightStages(pulse, light, nullptr, 2.0f);
    passed = passed && pulse.stages.size() == 2 && pulse.stages[1].transformed && pulsed.size() == 1 && pulsed[0].image == "lights/b.tga" &&
             Near(pulsed[0].color[1], 0.5f, 1e-6f) && (ExpressionReads(pulse.stages[0].color[0]) & kExpressionReadsTime) != 0;

    passed = passed && materials["fogs/thick"].fogLight && !materials["fogs/thick"].castsShadow;
    passed = passed && materials["textures/glass"].translucent && !materials["textures/glass"].castsShadow && !materials["textures/glass"].lit;
    passed = passed && materials["textures/wall"].lit && materials["textures/wall"].castsShadow && !materials["textures/wall"].translucent;
    passed = passed && materials["textures/grate"].lit && materials["textures/grate"].alphaTest && materials["textures/grate"].castsShadow;
    passed = passed && materials["textures/shadowed"].translucent && materials["textures/shadowed"].castsShadow;
    passed = passed && !materials["textures/sky"].castsShadow && !materials["textures/sky"].translucent;
    passed = passed && materials["textures/filter"].translucent && !materials["textures/opaque"].translucent;
    // An expression must not take the token after it: the stage still closes.
    passed = passed && materials["textures/next"].stages.size() == 1 && !materials["textures/next"].stages[0].color[3].Empty();
    return Check(passed, "light materials and coverage scan as id reads them");
}

// ------------------------------------------------------------------------------------------------
// Map entities and lights
// ------------------------------------------------------------------------------------------------

static const MapEntity * Named(const std::vector<MapEntity> & entities, const char * name)
{
    for (const MapEntity & entity : entities)
    {
        if (entity.String("name") == name)
        {
            return &entity;
        }
    }

    return nullptr;
}

static bool Entities()
{
    static constexpr const char * kMap = R"(Version 2
// worldspawn with a brush
{
"classname" "worldspawn"
{ brushDef3 { ( 0 0 1 -64 ) ( ( 0.03 0 0 ) ( 0 0.03 0 ) ) "textures/a" 0 0 0 } }
}
{
"Classname" "light"
"name" "point"
"origin" "10 20 30"
"light_radius" "100 50 25"
"_color" "1 0.5 0.25"
"texture" "lights/round"
}
{
"classname" "light"
"name" "bad"
"light_target" "0 0 -100"
}
{
"classname" "light"
"name" "spot"
"origin" "0 0 100"
"light_target" "0 0 -100"
"light_up" "0 50 0"
"light_right" "50 0 0"
"rotation" "1 0 0 0 1 0 0 0 1"
}
)";
    std::vector<MapEntity> entities;
    std::string error;
    bool passed = ParseMapEntities(kMap, entities, error) && entities.size() == 4 && entities[0].primitives == 1 &&
                  entities[1].String("classname") == "light";
    passed = passed && !ParseMapEntities("{ \"a\" }", entities, error) && !ParseMapEntities("{ \"a\" \"b\"", entities, error) &&
             !ParseMapEntities("\"a\" \"b\"", entities, error);
    (void)ParseMapEntities(kMap, entities, error);

    // Point light: id's defaults and box projection.
    RenderLight point;
    LightVolume volume;
    passed = passed && ParseRenderLight(*Named(entities, "point"), point, error) && point.pointLight && point.shader == "lights/round" &&
             point.parms[1] == 0.5f && point.parms[3] == 1.0f && DeriveLightVolume(point, volume, error) &&
             Near(volume.boundsMin[0], -90.0f, 1e-3f) && Near(volume.boundsMax[2], 55.0f, 1e-3f);

    const auto Plane = [](const std::array<float, 4> & plane, const Vec3 & p)
    {
        return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3];
    };

    // At the origin the projection reads its centre; 50 units along +x reaches s = 0.75.
    passed = passed && Near(Plane(volume.project[0], { 10.0f, 20.0f, 30.0f }), 0.5f, 1e-5f) &&
             Near(Plane(volume.project[0], { 60.0f, 20.0f, 30.0f }), 0.75f, 1e-5f) && Near(Plane(volume.project[2], { 1.0f, 2.0f, 3.0f }), 1.0f, 1e-6f) &&
             Near(Plane(volume.project[3], { 10.0f, 20.0f, 55.0f }), 1.0f, 1e-5f);

    // Target without up and right: id refuses it.
    RenderLight bad;
    passed = passed && !ParseRenderLight(*Named(entities, "bad"), bad, error);

    // Spot light: the target projects to the image centre, Q grows along it.
    RenderLight spot;
    LightVolume cone;
    passed = passed && ParseRenderLight(*Named(entities, "spot"), spot, error) && !spot.pointLight && DeriveLightVolume(spot, cone, error);
    const Vec3 target = { 0.0f, 0.0f, 0.0f };
    const float q = Plane(cone.project[2], target);
    passed = passed && q > 0.0f && Near(Plane(cone.project[0], target) / q, 0.5f, 1e-4f) && Near(Plane(cone.project[1], target) / q, 0.5f, 1e-4f) &&
             cone.boundsMax[2] > 91.0f && cone.boundsMax[2] < 100.0f && cone.boundsMin[2] <= 0.5f;
    return Check(passed, "map entities and render lights derive as id's");
}

static bool Classes()
{
    std::map<std::string, MaterialInfo> materials;
    std::string error;
    (void)ScanMaterials(R"(
lights/steady { { map a.tga colored zeroClamp } }
lights/flicker { { map a.tga red sintable[ time ] green 1 blue 1 } }
lights/hum { { map a.tga rgb sound } }
lights/spin { { map a.tga rotate time } }
fogs/f { fogLight { map _fog } }
)",
                        materials, error);

    const auto Classify = [&](const char * keys, const char * script = "")
    {
        std::vector<MapEntity> entities;
        std::string text = std::string("{ \"classname\" \"light\" \"name\" \"l\" ") + keys + " }\n{ \"classname\" \"trigger\" \"target\" \"switched\" }";
        (void)ParseMapEntities(text, entities, error);
        RenderLight light;
        (void)ParseRenderLight(entities[0], light, error);
        const auto found = materials.find(light.shader);
        std::string reason;
        return ClassifyLight(entities[0], light, found != materials.end() ? &found->second : nullptr, MakeLightContext(entities, script), reason);
    };

    bool passed = Classify("\"texture\" \"lights/steady\"") == LightClass::Static && Classify("\"texture\" \"lights/flicker\"") == LightClass::Animated &&
                  Classify("\"texture\" \"lights/hum\"") == LightClass::Sound && Classify("\"texture\" \"lights/spin\"") == LightClass::Moving &&
                  Classify("\"texture\" \"fogs/f\"") == LightClass::Fog && Classify("\"texture\" \"lights/steady\" \"start_off\" \"1\"") == LightClass::Switched &&
                  Classify("\"texture\" \"lights/steady\" \"bind\" \"mover\"") == LightClass::Moving &&
                  Classify("\"texture\" \"lights/missing\"") == LightClass::Moving &&
                  Classify("\"texture\" \"lights/steady\"", "$l.Off();") == LightClass::Switched &&
                  Classify("\"texture\" \"lights/steady\"", "$l.setShader( \"x\" );") == LightClass::Moving &&
                  Classify("\"texture\" \"lights/steady\"", "$l2.Off();") == LightClass::Static;

    // A light some entity targets switches.
    std::vector<MapEntity> entities;
    (void)ParseMapEntities("{ \"classname\" \"light\" \"name\" \"switched\" \"texture\" \"lights/steady\" }\n{ \"target\" \"switched\" }", entities,
                           error);
    RenderLight light;
    std::string reason;
    passed = passed && ParseRenderLight(entities[0], light, error) &&
             ClassifyLight(entities[0], light, &materials["lights/steady"], MakeLightContext(entities, ""), reason) == LightClass::Switched;
    return Check(passed, "lights fall into classes A to F by id's keys, materials and scripts");
}

// A light image of one value, 'size' square.
static LightImage Flat(int size, float value)
{
    LightImage image;
    image.width = size;
    image.height = size;
    image.rgb.assign(static_cast<std::size_t>(size * size * 3), value);
    return image;
}

static LightSource PointSource(const Vec3 & origin, float radius, const Vec3 & color, const LightImage * projection, const LightImage * falloff)
{
    RenderLight light;
    light.pointLight = true;
    light.origin = origin;
    light.lightRadius = { radius, radius, radius };
    LightSource source;
    std::string error;
    (void)DeriveLightVolume(light, source.volume, error);
    source.falloff = falloff;
    LightStageColor stage;
    stage.image = "flat";
    stage.zeroClamp = true;
    stage.color = color;
    source.stages.push_back(stage);
    source.images.push_back(projection);
    return source;
}

static bool Evaluation()
{
    // Bilinear sampling with GL's texel centres; zero clamp fades to black past the edge.
    LightImage ramp;
    ramp.width = 2;
    ramp.height = 1;
    ramp.rgb = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
    bool passed = Near(ramp.Sample(0.5f, 0.5f, false)[0], 0.5f, 1e-6f) && Near(ramp.Sample(0.25f, 0.5f, false)[0], 0.0f, 1e-6f) &&
                  Near(ramp.Sample(1.0f, 0.5f, false)[0], 1.0f, 1e-6f) && Near(ramp.Sample(1.0f, 0.5f, true)[0], 0.5f, 1e-6f);

    const LightImage white = Flat(4, 1.0f);
    const LightSource light = PointSource({ 0.0f, 0.0f, 100.0f }, 200.0f, { 1.0f, 0.5f, 0.25f }, &white, &white);

    // Straight below, facing it: colour x 1; at 60 degrees, half.
    const Vec3 below = EvaluateLight(light, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
    const Vec3 slanted = EvaluateLight(light, { 0.0f, 0.0f, 0.0f }, { 0.0f, std::sqrt(0.75f), 0.5f });
    const Vec3 behind = EvaluateLight(light, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f });
    passed = passed && Near(below[0], 1.0f, 1e-5f) && Near(below[2], 0.25f, 1e-5f) && Near(slanted[0], 0.5f, 1e-4f) && behind[0] == 0.0f;
    return Check(passed, "a light's value is colour x projection x falloff x N.L");
}

// ------------------------------------------------------------------------------------------------
// Baking
// ------------------------------------------------------------------------------------------------

static void AddQuad(ProcSurface & surface, const Vec3 & a, const Vec3 & b, const Vec3 & c, const Vec3 & d, const Vec3 & normal)
{
    const std::uint32_t first = static_cast<std::uint32_t>(surface.vertices.size());

    for (const Vec3 & corner : { a, b, c, d })
    {
        ProcVertex vertex;
        vertex.position = corner;
        vertex.normal = normal;
        vertex.uv = { corner[0] / 64.0f, corner[1] / 64.0f };
        surface.vertices.push_back(vertex);
    }

    for (const std::uint32_t index : { 0U, 1U, 2U, 0U, 2U, 3U })
    {
        surface.indices.push_back(first + index);
    }
}

static bool Bake()
{
    // A 512 x 512 floor at z 0, a 32-unit roof tile at z 64 over (64..96, 64..96), whose shadow
    // from the light falls on (128..192, 128..192), a grate and an unlit panel; one white point
    // light at (0, 0, 128) reaching 512 units.
    ProcFile proc;
    proc.areaCount = 1;
    ProcModel model;
    model.name = "_area0";
    ProcSurface floor;
    floor.material = "floor";
    AddQuad(floor, { -256.0f, -256.0f, 0.0f }, { 256.0f, -256.0f, 0.0f }, { 256.0f, 256.0f, 0.0f }, { -256.0f, 256.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
    ProcSurface roof;
    roof.material = "roof";
    AddQuad(roof, { 64.0f, 64.0f, 64.0f }, { 96.0f, 64.0f, 64.0f }, { 96.0f, 96.0f, 64.0f }, { 64.0f, 96.0f, 64.0f }, { 0.0f, 0.0f, -1.0f });
    ProcSurface grate;
    grate.material = "grate";
    AddQuad(grate, { -64.0f, -64.0f, 8.0f }, { -32.0f, -64.0f, 8.0f }, { -32.0f, -32.0f, 8.0f }, { -64.0f, -32.0f, 8.0f }, { 0.0f, 0.0f, 1.0f });
    ProcSurface panel;
    panel.material = "panel";
    AddQuad(panel, { 200.0f, -200.0f, 16.0f }, { 216.0f, -200.0f, 16.0f }, { 216.0f, -184.0f, 16.0f }, { 200.0f, -184.0f, 16.0f },
            { 0.0f, 0.0f, 1.0f });
    model.surfaces = { floor, roof, grate, panel };
    proc.models.push_back(model);

    const LightImage white = Flat(4, 1.0f);
    LightmapScene scene;
    scene.proc = &proc;
    scene.materials["floor"] = { true, true, false };
    scene.materials["roof"] = { true, true, false };
    scene.materials["grate"] = { true, false, true };
    scene.materials["panel"] = { false, false, false };
    scene.lights.push_back(PointSource({ 0.0f, 0.0f, 128.0f }, 512.0f, { 0.5f, 0.75f, 1.0f }, &white, &white));

    LightmapPolicy policy;
    WorldLighting lighting;
    std::vector<LightmapPage> pages;
    LightmapStats stats;
    std::string error;
    bool passed = BakeLightmaps(scene, policy, lighting, pages, stats, error) && lighting.areas.size() == 1 && lighting.areas[0].size() == 4 &&
                  pages.size() == 1 && lighting.pages == 1 && stats.litSurfaces == 3 && stats.vertexLitSurfaces == 1 && stats.rays > 0;

    if (!passed)
    {
        std::fprintf(stderr, "bake: %s\n", error.c_str());
        return Check(false, "lightmaps bake intensity, chroma, shadows and pages");
    }

    // The floor: every triangle on page 0, UVs inside it, chroma the light's (blue strongest).
    const WorldLitSurface & lit = lighting.areas[0][0];
    passed = lit.pages.size() == 2 && lit.pages[0] == 0 && lit.pages[1] == 0;

    for (const WorldLitVertex & vertex : lit.vertices)
    {
        passed = passed && vertex.lightmapUv[0] > 0.0f && vertex.lightmapUv[0] < 1.0f && vertex.lightmapUv[1] > 0.0f && vertex.lightmapUv[1] < 1.0f &&
                 Near(vertex.color[1] / vertex.color[2], 0.75f, 0.02f) && Near(vertex.color[0] / vertex.color[2], 0.5f, 0.02f);
    }

    // The light at a floor point through the page: vertex colour (chroma x exposure) at the
    // nearest corner times the texel, against the analytic value. The quad's UVs map linearly,
    // so a point's UV interpolates them.
    const auto Texel = [&](float x, float y)
    {
        const float u = (x + 256.0f) / 512.0f;
        const float v = (y + 256.0f) / 512.0f;
        const auto & a = lit.vertices[0].lightmapUv;
        const auto & c = lit.vertices[2].lightmapUv;
        const float pageX = (a[0] + (c[0] - a[0]) * u) * static_cast<float>(pages[0].size);
        const float pageY = (a[1] + (c[1] - a[1]) * v) * static_cast<float>(pages[0].size);
        const int tx = static_cast<int>(pageX);
        const int ty = static_cast<int>(pageY);
        return static_cast<float>(pages[0].texels[static_cast<std::size_t>(ty * pages[0].size + tx)]) / 128.0f;
    };

    // Directly below the light: N.L 1, white images: intensity 1 x blue's 1.0 (chroma 1).
    const float exposure = lit.vertices[0].color[2];
    passed = passed && Near(Texel(0.0f, 0.0f) * exposure, 1.0f, 0.03f);

    // At (128, 0): N.L = 128 / |(128, 0, 128)|.
    passed = passed && Near(Texel(128.0f, 0.0f) * exposure, 128.0f / std::sqrt(2.0f * 128.0f * 128.0f), 0.04f);

    // In the roof tile's shadow the floor is dark; beside it, lit.
    passed = passed && Texel(160.0f, 160.0f) * exposure < 0.05f && Texel(160.0f, 100.0f) * exposure > 0.3f;

    // The grate: lit per vertex, no page; the unlit panel: full colour, no page.
    const WorldLitSurface & grated = lighting.areas[0][2];
    const WorldLitSurface & unlit = lighting.areas[0][3];
    passed = passed && grated.pages[0] == kBatchNoLightmap && grated.vertices[0].color[2] > 0.5f && grated.vertices[0].color[2] <= 255.0f / 128.0f &&
             unlit.pages[0] == kBatchNoLightmap && unlit.vertices[0].color[0] == 1.0f;

    // The roof faces away from the light above it: black.
    passed = passed && Texel(0.0f, 0.0f) > 0.0f && lighting.areas[0][1].pages[0] == 0;
    return Check(passed, "lightmaps bake intensity, chroma, shadows and pages");
}

// ------------------------------------------------------------------------------------------------
// Static models
// ------------------------------------------------------------------------------------------------

static void AddTriangle(ProcSurface & surface, const Vec3 & a, const Vec3 & b, const Vec3 & c)
{
    for (const Vec3 & corner : { a, b, c })
    {
        ProcVertex vertex;
        vertex.position = corner;
        vertex.normal = { 1.0f, 0.0f, 0.0f };
        surface.indices.push_back(static_cast<std::uint32_t>(surface.vertices.size()));
        surface.vertices.push_back(vertex);
    }
}

static MapEntity StaticEntity(const char * name, std::vector<std::pair<std::string, std::string> > keys)
{
    MapEntity entity;
    entity.keys = { { "classname", "func_static" }, { "name", name }, { "model", name } };
    entity.keys.insert(entity.keys.end(), keys.begin(), keys.end());
    return entity;
}

static bool StaticModels()
{
    std::map<std::string, MaterialInfo> materials;
    std::string error;
    // A wall, a flare coloured by the entity, and an opaque panel whose glow stage is coloured too.
    const bool scanned = ScanMaterials("textures/wall { diffusemap textures/wall_d }\n"
                                       "textures/flare { translucent { blend add map textures/flare_d colored } }\n"
                                       "textures/panel { diffusemap textures/panel_d { blend add map textures/panel_add colored } }\n",
                                       materials, error);

    if (!scanned)
    {
        std::fprintf(stderr, "materials: %s\n", error.c_str());
        return Check(false, "static func_static models merge into their areas");
    }

    // Two areas split at x = 0: area 0 on the positive side.
    ProcFile proc;
    proc.areaCount = 2;
    proc.nodes.push_back(ProcNode{ { 1.0f, 0.0f, 0.0f, 0.0f }, { -1, -2 } });
    proc.models.push_back(ProcModel{ "_area0", { ProcSurface{ "textures/wall", {}, {} } } });
    AddTriangle(proc.models.back().surfaces[0], { 10.0f, 0.0f, 0.0f }, { 10.0f, 8.0f, 0.0f }, { 10.0f, 0.0f, 8.0f });
    proc.models.push_back(ProcModel{ "_area1", {} });

    // func_static_1, turned 180 degrees at x = 100: its +150 triangle lands at x = -50 (area 1),
    // its -50 one at x = 150 (area 0).
    ProcModel moved{ "func_static_1", { ProcSurface{ "textures/wall", {}, {} } } };
    AddTriangle(moved.surfaces[0], { 150.0f, 0.0f, 0.0f }, { 150.0f, 8.0f, 0.0f }, { 150.0f, 0.0f, 8.0f });
    AddTriangle(moved.surfaces[0], { -50.0f, 0.0f, 0.0f }, { -50.0f, 8.0f, 0.0f }, { -50.0f, 0.0f, 8.0f });
    proc.models.push_back(moved);

    // A bound copy, a flare, a panel with a flare, and one the script names.
    for (const char * name : { "func_static_2", "func_static_3", "func_static_5" })
    {
        proc.models.push_back(ProcModel{ name, { ProcSurface{ name == std::string("func_static_3") ? "textures/flare" : "textures/wall", {}, {} } } });
        AddTriangle(proc.models.back().surfaces[0], { 0.0f, 0.0f, 0.0f }, { 0.0f, 8.0f, 0.0f }, { 0.0f, 0.0f, 8.0f });
    }

    proc.models.push_back(ProcModel{ "func_static_4", { ProcSurface{ "textures/panel", {}, {} }, ProcSurface{ "textures/flare", {}, {} } } });

    for (ProcSurface & surface : proc.models.back().surfaces)
    {
        AddTriangle(surface, { 0.0f, 0.0f, 0.0f }, { 0.0f, 8.0f, 0.0f }, { 0.0f, 0.0f, 8.0f });
    }

    const std::vector<MapEntity> entities = {
        StaticEntity("func_static_1", { { "origin", "100 0 0" }, { "angle", "180" } }),
        StaticEntity("func_static_2", { { "origin", "50 0 0" }, { "bind", "mover" } }),
        StaticEntity("func_static_3", { { "origin", "50 0 0" }, { "_color", "1 0 0" } }),
        StaticEntity("func_static_4", { { "origin", "50 0 0" }, { "_color", "0 1 0" } }),
        StaticEntity("func_static_5", { { "origin", "50 0 0" } }),
    };
    const LightContext context = MakeLightContext(entities, "void main() { $func_static_5.hide(); }");
    StaticModelStats stats;
    bool passed = MergeStaticModels(proc, entities, context, materials, stats, error);

    std::vector<std::string> names;

    for (const ProcModel & model : proc.models)
    {
        names.push_back(model.name);
    }

    // func_static_1 left whole; the panel's flare stays with func_static_4.
    passed = passed && names == std::vector<std::string>{ "_area0", "_area1", "func_static_2", "func_static_3", "func_static_5", "func_static_4" } &&
             proc.models[5].surfaces.size() == 1 && proc.models[5].surfaces[0].material == "textures/flare";
    passed = passed && stats.inlineModels == 5 && stats.merged == 1 && stats.partial == 1 && stats.keptSurfaces == 2 && stats.parmSurfaces == 1 &&
             stats.triangles == 3 && stats.solidTriangles == 0 && stats.surfacesAdded == 2 && stats.kept.size() == 3 && stats.kept["bound"] == 1 &&
             stats.kept["parms"] == 1 && stats.kept["scripted"] == 1;

    // Area 0: its wall gained the turned -50 triangle at x = 150 (y and the normal turned too),
    // and a panel surface; area 1 a new wall surface with the +150 triangle at x = -50.
    const ProcModel & area0 = proc.models[0];
    const ProcModel & area1 = proc.models[1];
    passed = passed && area0.surfaces.size() == 2 && area0.surfaces[0].vertices.size() == 6 && area0.surfaces[0].indices.size() == 6 &&
             area0.surfaces[1].material == "textures/panel" && area1.surfaces.size() == 1 && area1.surfaces[0].indices.size() == 3;

    if (passed)
    {
        const ProcVertex & turned = area0.surfaces[0].vertices[4];
        const ProcVertex & far = area1.surfaces[0].vertices[0];
        passed = Near(turned.position[0], 150.0f, 1e-3f) && Near(turned.position[1], -8.0f, 1e-3f) && Near(turned.position[2], 0.0f, 1e-3f) &&
                 Near(turned.normal[0], -1.0f, 1e-5f) && Near(far.position[0], -50.0f, 1e-3f) && area0.surfaces[0].indices[3] == 3;
    }

    return Check(passed, "static func_static models merge into their areas");
}

} // namespace

int main()
{
    bool passed = Expressions();
    passed = LightMaterials() && passed;
    passed = Entities() && passed;
    passed = Classes() && passed;
    passed = Evaluation() && passed;
    passed = Bake() && passed;
    passed = StaticModels() && passed;

    if (!passed)
    {
        return 1;
    }

    std::puts("Lighting tests passed.");
    return 0;
}
