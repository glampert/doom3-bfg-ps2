// ================================================================================================
// File: light.cpp
// Brief: A map's lights as id's renderer derives them (idGameEdit::ParseSpawnArgsToRenderLight,
// R_DeriveLightData's projection and bounds), their classes A to F for the PS2's lighting split
// (RENDERING_ARCHITECTURE.md 8.1), and a light's colour at a point from its material's stages and
// images, which the lightmap baker sums.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/light.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace assets
{

namespace
{

using Mat4 = std::array<std::array<double, 4>, 4>;

static float Dot(const Vec3 & a, const Vec3 & b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float LengthSquared(const Vec3 & a)
{
    return Dot(a, a);
}

static std::string Lower(std::string text)
{
    for (char & character : text)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return text;
}

static Mat4 Multiply(const Mat4 & a, const Mat4 & b)
{
    Mat4 result = {};

    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t k = 0; k < 4; ++k)
            {
                result[row][column] += a[row][k] * b[k][column];
            }
        }
    }

    return result;
}

// Gauss-Jordan with partial pivoting.
static bool Invert(const Mat4 & input, Mat4 & output)
{
    Mat4 a = input;
    Mat4 inverse = {};

    for (std::size_t index = 0; index < 4; ++index)
    {
        inverse[index][index] = 1.0;
    }

    for (std::size_t column = 0; column < 4; ++column)
    {
        std::size_t pivot = column;

        for (std::size_t row = column + 1; row < 4; ++row)
        {
            if (std::fabs(a[row][column]) > std::fabs(a[pivot][column]))
            {
                pivot = row;
            }
        }

        if (std::fabs(a[pivot][column]) < 1e-12)
        {
            return false;
        }

        std::swap(a[pivot], a[column]);
        std::swap(inverse[pivot], inverse[column]);
        const double scale = 1.0 / a[column][column];

        for (std::size_t k = 0; k < 4; ++k)
        {
            a[column][k] *= scale;
            inverse[column][k] *= scale;
        }

        for (std::size_t row = 0; row < 4; ++row)
        {
            if (row == column)
            {
                continue;
            }

            const double factor = a[row][column];

            for (std::size_t k = 0; k < 4; ++k)
            {
                a[row][k] -= factor * a[column][k];
                inverse[row][k] -= factor * inverse[column][k];
            }
        }
    }

    output = inverse;
    return true;
}

// R_ComputePointLightProjectionMatrix and R_ComputeParallelLightProjectionMatrix: a box.
static Mat4 BoxProjection(const RenderLight & light)
{
    Mat4 project = {};

    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        project[axis][axis] = 0.5 / static_cast<double>(light.lightRadius[axis]);
        project[axis][3] = 0.5;
    }

    project[3][3] = 1.0;
    return project;
}

// R_ComputeSpotLightProjectionMatrix, in double; 'zScale' as id returns it.
static Mat4 SpotProjection(const RenderLight & light, double & zScale)
{
    static constexpr double kMinZNear = 8.0;
    static constexpr double kMinZFar = 16.0;

    const auto D = [](const Vec3 & v)
    {
        return std::array<double, 3>{ static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2]) };
    };
    const auto DotD = [](const std::array<double, 3> & a, const std::array<double, 3> & b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };

    const std::array<double, 3> target = D(light.target);
    const std::array<double, 3> right = D(light.right);
    const std::array<double, 3> up = D(light.up);
    const double targetDistSqr = DotD(target, target);
    const double targetDist = std::sqrt(targetDistSqr);
    const double rightScale = 0.5 * targetDist / DotD(right, right);
    const double upScale = -0.5 * targetDist / DotD(up, up);

    Mat4 project = {};
    std::array<double, 3> normalizedTarget = {};

    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        normalizedTarget[axis] = target[axis] / targetDist;
        project[0][axis] = right[axis] * rightScale;
        project[1][axis] = up[axis] * upScale;
        project[3][axis] = normalizedTarget[axis];
    }

    const double zNear = std::max(DotD(D(light.start), normalizedTarget), kMinZNear);
    const double zFar = std::max(DotD(D(light.end), normalizedTarget), kMinZFar);
    const double scale = (zNear + zFar) / zFar;

    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        project[2][axis] = normalizedTarget[axis] * scale;
    }

    project[2][3] = -zNear * scale;

    // Offset to the 0..1 texture range.
    std::array<double, 4> projected = {};

    for (std::size_t row = 0; row < 4; ++row)
    {
        projected[row] = project[row][0] * target[0] + project[row][1] * target[1] + project[row][2] * target[2] + project[row][3];
    }

    const double offset0 = 0.5 - projected[0] / projected[3];
    const double offset1 = 0.5 - projected[1] / projected[3];

    for (std::size_t column = 0; column < 4; ++column)
    {
        project[0][column] += offset0 * project[3][column];
        project[1][column] += offset1 * project[3][column];
    }

    zScale = 1.0 / (zNear + zFar);
    return project;
}

// World to light local: the inverse of the axis-and-origin matrix (axis rows are the local axes).
static Mat4 WorldToLocal(const RenderLight & light)
{
    Mat4 matrix = {};

    for (std::size_t row = 0; row < 3; ++row)
    {
        double translation = 0.0;

        for (std::size_t column = 0; column < 3; ++column)
        {
            matrix[row][column] = static_cast<double>(light.axis[row][column]);
            translation += static_cast<double>(light.axis[row][column]) * static_cast<double>(light.origin[column]);
        }

        matrix[row][3] = -translation;
    }

    matrix[3][3] = 1.0;
    return matrix;
}

static bool HasMethodCall(const std::string & script, const std::string & name, const char * const * methods, std::size_t count)
{
    const std::string prefix = "$" + name + ".";
    std::size_t at = 0;

    while ((at = script.find(prefix, at)) != std::string::npos)
    {
        at += prefix.size();

        for (std::size_t index = 0; index < count; ++index)
        {
            const std::string method = methods[index];

            if (script.compare(at, method.size(), method) == 0 &&
                (at + method.size() >= script.size() || std::isalnum(static_cast<unsigned char>(script[at + method.size()])) == 0))
            {
                return true;
            }
        }
    }

    return false;
}

static bool Mentions(const std::string & script, const std::string & name)
{
    const std::string needle = "$" + name;
    std::size_t at = 0;

    while ((at = script.find(needle, at)) != std::string::npos)
    {
        const std::size_t end = at + needle.size();

        if (end >= script.size() || (std::isalnum(static_cast<unsigned char>(script[end])) == 0 && script[end] != '_'))
        {
            return true;
        }

        at = end;
    }

    return false;
}

} // namespace

const char * LightClassName(LightClass value)
{
    static constexpr const char * kNames[] = { "A", "B", "C", "D", "E", "F" };
    return value < LightClass::Count ? kNames[static_cast<std::size_t>(value)] : "?";
}

bool ParseRenderLight(const MapEntity & entity, RenderLight & output, std::string & error)
{
    RenderLight light;
    light.name = Lower(entity.String("name"));

    if (!entity.Vector("light_origin", light.origin))
    {
        (void)entity.Vector("origin", light.origin);
    }

    const bool gotTarget = entity.Vector("light_target", light.target);
    const bool gotUp = entity.Vector("light_up", light.up);
    const bool gotRight = entity.Vector("light_right", light.right);
    (void)entity.Vector("light_start", light.start);

    if (!entity.Vector("light_end", light.end))
    {
        light.end = light.target;
    }

    if ((gotTarget || gotUp || gotRight) != (gotTarget && gotUp && gotRight))
    {
        error = "light " + light.name + " has bad target info";
        return false;
    }

    if (!gotTarget)
    {
        light.pointLight = true;
        (void)entity.Vector("light_center", light.lightCenter);

        if (!entity.Vector("light_radius", light.lightRadius))
        {
            const float radius = entity.Float("light", 300.0f);
            light.lightRadius = { radius, radius, radius };
        }
    }

    if (!entity.Matrix("light_rotation", light.axis) && !entity.Matrix("rotation", light.axis))
    {
        // idAngles( 0, yaw, 0 ).ToMat3().
        const float yaw = entity.Float("angle", 0.0f) * 3.14159265358979323846f / 180.0f;
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        light.axis = { Vec3{ c, s, 0.0f }, Vec3{ -s, c, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f } };
    }

    Vec3 color = { 1.0f, 1.0f, 1.0f };
    (void)entity.Vector("_color", color);
    light.parms[0] = color[0];
    light.parms[1] = color[1];
    light.parms[2] = color[2];
    light.parms[3] = entity.Float("shaderparm3", 1.0f);
    light.parms[4] = entity.Float("shaderparm4", 0.0f);
    light.parms[5] = entity.Float("shaderparm5", 0.0f);
    light.parms[6] = entity.Float("shaderparm6", 0.0f);
    light.parms[7] = entity.Float("shaderparm7", 0.0f);
    light.noShadows = entity.Bool("noshadows", false);
    light.noSpecular = entity.Bool("nospecular", false);
    light.parallel = entity.Bool("parallel", false);
    light.shader = Lower(entity.String("texture", "lights/squarelight1"));

    if (light.pointLight && (light.lightRadius[0] <= 0.0f || light.lightRadius[1] <= 0.0f || light.lightRadius[2] <= 0.0f))
    {
        error = "light " + light.name + " has a radius that is not positive";
        return false;
    }

    output = light;
    return true;
}

bool DeriveLightVolume(const RenderLight & light, LightVolume & output, std::string & error)
{
    double zScale = 1.0;
    const Mat4 localProject = light.pointLight || light.parallel ? BoxProjection(light) : SpotProjection(light, zScale);

    if (!light.pointLight && !light.parallel && (LengthSquared(light.target) <= 0.0f || LengthSquared(light.up) <= 0.0f || LengthSquared(light.right) <= 0.0f))
    {
        error = "light " + light.name + " has a degenerate projection";
        return false;
    }

    const Mat4 worldToLocal = WorldToLocal(light);
    const Mat4 base = Multiply(localProject, worldToLocal);

    // Old style rows: S, T, Q (localProject's w), then the falloff (its z) times zScale.
    static constexpr std::size_t kRows[4] = { 0, 1, 3, 2 };
    LightVolume volume;

    for (std::size_t row = 0; row < 4; ++row)
    {
        const double scale = row == 3 ? zScale : 1.0;

        for (std::size_t column = 0; column < 4; ++column)
        {
            volume.project[row][column] = static_cast<float>(base[kRows[row]][column] * scale);
        }
    }

    if (light.parallel)
    {
        Vec3 direction = light.lightCenter;
        const float length = std::sqrt(LengthSquared(direction));
        direction = length > 0.0f ? Vec3{ direction[0] / length, direction[1] / length, direction[2] / length } : Vec3{ 0.0f, 0.0f, 1.0f };

        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            volume.origin[axis] = light.origin[axis] + direction[axis] * 100000.0f;
        }
    }
    else
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            volume.origin[axis] = light.origin[axis] + light.axis[0][axis] * light.lightCenter[0] + light.axis[1][axis] * light.lightCenter[1] +
                                  light.axis[2][axis] * light.lightCenter[2];
        }
    }

    // The bounds: the zero-to-one cube through the inverse projection (idRenderMatrix::ProjectedBounds).
    Mat4 inverse = {};

    if (!Invert(base, inverse))
    {
        error = "light " + light.name + " has a singular projection";
        return false;
    }

    for (int corner = 0; corner < 8; ++corner)
    {
        const double cube[4] = { static_cast<double>(corner & 1), static_cast<double>((corner >> 1) & 1), static_cast<double>((corner >> 2) & 1), 1.0 };
        double point[4] = {};

        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                point[row] += inverse[row][column] * cube[column];
            }
        }

        if (std::fabs(point[3]) < 1e-12)
        {
            error = "light " + light.name + " has an unbounded volume";
            return false;
        }

        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const float value = static_cast<float>(point[axis] / point[3]);
            volume.boundsMin[axis] = corner == 0 ? value : std::min(volume.boundsMin[axis], value);
            volume.boundsMax[axis] = corner == 0 ? value : std::max(volume.boundsMax[axis], value);
        }
    }

    output = volume;
    return true;
}

LightContext MakeLightContext(const std::vector<MapEntity> & entities, const std::string & script)
{
    LightContext context;
    context.script = Lower(script);

    for (const MapEntity & entity : entities)
    {
        for (const auto & [key, value] : entity.keys)
        {
            if (key.rfind("target", 0) == 0)
            {
                context.targeted.insert(Lower(value));
            }
        }
    }

    return context;
}

LightClass ClassifyLight(const MapEntity & entity, const RenderLight & light, const MaterialInfo * material, const LightContext & context,
                         std::string & reason)
{
    static constexpr const char * kRuntimeMethods[] = { "setshader", "setlightparm", "setlightparms", "setradius", "setradiusxyz" };

    if (material == nullptr)
    {
        reason = "undeclared material";
        return LightClass::Moving;
    }

    if (material->fogLight || material->blendLight)
    {
        reason = material->fogLight ? "fog light" : "blend light";
        return LightClass::Fog;
    }

    if (entity.Find("bind") != nullptr || entity.Find("bindtojoint") != nullptr)
    {
        reason = "bound";
        return LightClass::Moving;
    }

    unsigned reads = 0;
    bool transformed = false;

    for (const MaterialStage & stage : material->stages)
    {
        transformed = transformed || stage.transformed;
        reads |= ExpressionReads(stage.condition);

        for (const MaterialExpression & channel : stage.color)
        {
            reads |= ExpressionReads(channel);
        }
    }

    if (transformed || entity.Find("cinematic") != nullptr)
    {
        reason = transformed ? "texture matrix" : "cinematic";
        return LightClass::Moving;
    }

    if (HasMethodCall(context.script, light.name, kRuntimeMethods, std::size(kRuntimeMethods)))
    {
        reason = "script changes its shader, parms or radius";
        return LightClass::Moving;
    }

    if (!material->expressionError.empty())
    {
        reason = "unparsed expression: " + material->expressionError;
        return LightClass::Moving;
    }

    if ((reads & kExpressionReadsSound) != 0)
    {
        reason = "sound";
        return LightClass::Sound;
    }

    if (entity.Bool("start_off", false) || entity.Int("health", 0) > 0 || !entity.String("broken").empty() || entity.Bool("break", false) ||
        entity.Int("levels", 1) > 1)
    {
        reason = "switchable keys";
        return LightClass::Switched;
    }

    if (context.targeted.count(light.name) != 0 || Mentions(context.script, light.name))
    {
        reason = context.targeted.count(light.name) != 0 ? "targeted" : "scripted";
        return LightClass::Switched;
    }

    if ((reads & (kExpressionReadsTime | kExpressionReadsGlobals)) != 0)
    {
        reason = "time";
        return LightClass::Animated;
    }

    reason = "static";
    return LightClass::Static;
}

std::vector<LightStageColor> EvaluateLightStages(const MaterialInfo & material, const RenderLight & light, const MaterialTables * tables,
                                                 float lightScale)
{
    ExpressionInputs inputs;
    inputs.parms = light.parms;
    inputs.tables = tables;
    std::vector<LightStageColor> stages;

    for (const MaterialStage & stage : material.stages)
    {
        if (stage.map.empty() || EvaluateExpression(stage.condition, inputs) == 0.0f)
        {
            continue;
        }

        LightStageColor color;
        color.image = stage.map;
        color.zeroClamp = stage.zeroClamp;

        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            color.color[channel] = lightScale * EvaluateExpression(stage.color[channel], inputs);
        }

        if (color.color[0] > 0.0f || color.color[1] > 0.0f || color.color[2] > 0.0f)
        {
            stages.push_back(color);
        }
    }

    return stages;
}

LightImage LightImage::FromRgba(const RgbaImage & image)
{
    LightImage output;
    output.width = image.width;
    output.height = image.height;
    output.rgb.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 3);

    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            const std::uint8_t * texel = image.At(x, y);
            float * out = &output.rgb[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 3];

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                out[channel] = static_cast<float>(texel[channel]) / 255.0f;
            }
        }
    }

    return output;
}

Vec3 LightImage::Sample(float s, float t, bool zero) const
{
    if (width <= 0 || height <= 0)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float x = s * static_cast<float>(width) - 0.5f;
    const float y = t * static_cast<float>(height) - 0.5f;
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const float ax = x - fx;
    const float ay = y - fy;
    Vec3 result = {};

    for (int corner = 0; corner < 4; ++corner)
    {
        int tx = x0 + (corner & 1);
        int ty = y0 + (corner >> 1);
        const float weight = ((corner & 1) != 0 ? ax : 1.0f - ax) * ((corner >> 1) != 0 ? ay : 1.0f - ay);

        if (tx < 0 || ty < 0 || tx >= width || ty >= height)
        {
            if (zero)
            {
                continue;
            }

            tx = std::clamp(tx, 0, width - 1);
            ty = std::clamp(ty, 0, height - 1);
        }

        const float * texel = &rgb[(static_cast<std::size_t>(ty) * static_cast<std::size_t>(width) + static_cast<std::size_t>(tx)) * 3];

        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            result[channel] += weight * texel[channel];
        }
    }

    return result;
}

Vec3 EvaluateLight(const LightSource & light, const Vec3 & point, const Vec3 & normal)
{
    const auto Plane = [&point](const std::array<float, 4> & plane)
    {
        return plane[0] * point[0] + plane[1] * point[1] + plane[2] * point[2] + plane[3];
    };

    const float q = Plane(light.volume.project[2]);

    if (q <= 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float s = Plane(light.volume.project[0]) / q;
    const float t = Plane(light.volume.project[1]) / q;
    const float f = Plane(light.volume.project[3]);
    float lambert = 1.0f;

    if (!light.ambient)
    {
        Vec3 toLight = { light.volume.origin[0] - point[0], light.volume.origin[1] - point[1], light.volume.origin[2] - point[2] };
        const float length = std::sqrt(LengthSquared(toLight));
        lambert = length > 0.0f ? std::max(0.0f, Dot(toLight, normal) / length) : 0.0f;
    }

    if (lambert <= 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const Vec3 falloff = light.falloff != nullptr ? light.falloff->Sample(f, 0.5f, false) : Vec3{ 1.0f, 1.0f, 1.0f };
    Vec3 result = {};

    for (std::size_t index = 0; index < light.stages.size(); ++index)
    {
        const LightImage * image = light.images[index];
        const Vec3 projection = image != nullptr ? image->Sample(s, t, light.stages[index].zeroClamp) : Vec3{ 1.0f, 1.0f, 1.0f };

        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            result[channel] += light.stages[index].color[channel] * projection[channel] * falloff[channel] * lambert;
        }
    }

    return result;
}

} // namespace assets
