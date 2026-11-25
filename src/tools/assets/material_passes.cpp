// ================================================================================================
// File: material_passes.cpp
// Brief: Compile world materials into the base pass and additive passes the PS2 draws.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/material_passes.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace assets
{
namespace
{

// A folded operand: a constant, or the node that computes it.
struct Folded
{
    std::optional<float> constant;
    std::uint16_t node = 0;
};

// Evaluates one operator on constants exactly as the tool's evaluator (and id's) does.
static float Apply(ExpressionOp op, float a, float b)
{
    MaterialExpression expression;
    ExpressionNode left;
    left.value = a;
    ExpressionNode right;
    right.value = b;
    ExpressionNode node;
    node.op = op;
    node.a = 0;
    node.b = 1;
    expression.nodes = { left, right, node };
    expression.root = 2;
    return EvaluateExpression(expression, ExpressionInputs{});
}

static float Lookup(const std::string & table, float index, const MaterialTables & tables)
{
    MaterialExpression expression;
    ExpressionNode operand;
    operand.value = index;
    ExpressionNode node;
    node.op = ExpressionOp::Table;
    node.a = 0;
    node.table = table;
    expression.nodes = { operand, node };
    expression.root = 1;
    ExpressionInputs inputs;
    inputs.tables = &tables;
    return EvaluateExpression(expression, inputs);
}

class Folder
{
public:
    Folder(const MaterialExpression & expression, const ShaderParms & parms, const MaterialTables & tables, PlannedPass & pass)
        : m_expression(expression)
        , m_parms(parms)
        , m_tables(tables)
        , m_pass(pass)
    {
    }

    // False when the pass would take more nodes than the target evaluates.
    bool Fold(int index, Folded & output)
    {
        const ExpressionNode & entry = m_expression.nodes[static_cast<std::size_t>(index)];

        switch (entry.op)
        {
            case ExpressionOp::Constant:
                output.constant = entry.value;
                return true;
            case ExpressionOp::Parm:
                output.constant = m_parms[static_cast<std::size_t>(entry.index)];
                return true;
            case ExpressionOp::Global:
            case ExpressionOp::Sound:
                // The world's views carry no global parms, and no sound drives world surfaces.
                output.constant = 0.0f;
                return true;
            case ExpressionOp::Time:
            {
                D3txNode node;
                node.op = static_cast<std::uint8_t>(ExpressionOp::Time);
                return Emit(node, "", output);
            }
            case ExpressionOp::Table:
            {
                Folded operand;

                if (!Fold(entry.a, operand))
                {
                    return false;
                }

                const auto found = m_tables.find(entry.table);

                // An undeclared table, or one of under two values, reads 1 (idDeclTable).
                if (found == m_tables.end() || found->second.values.size() < 3)
                {
                    output.constant = 1.0f;
                    return true;
                }

                if (operand.constant)
                {
                    output.constant = Lookup(entry.table, *operand.constant, m_tables);
                    return true;
                }

                D3txNode node;
                node.op = static_cast<std::uint8_t>(ExpressionOp::Table);
                node.flags = static_cast<std::uint8_t>((found->second.snap ? kD3txTableSnap : 0U) | (found->second.clamp ? kD3txTableClamp : 0U));
                node.a = operand.node;
                node.count = static_cast<std::uint16_t>(found->second.values.size());
                return Emit(node, entry.table, output);
            }
            default:
                break;
        }

        Folded a;
        Folded b;

        if (!Fold(entry.a, a) || !Fold(entry.b, b))
        {
            return false;
        }

        if (a.constant && b.constant)
        {
            output.constant = Apply(entry.op, *a.constant, *b.constant);
            return true;
        }

        const std::uint16_t left = Materialize(a);
        const std::uint16_t right = Materialize(b);

        if (left == kD3txNoNode || right == kD3txNoNode)
        {
            return false;
        }

        D3txNode node;
        node.op = static_cast<std::uint8_t>(entry.op);
        node.a = left;
        node.b = right;
        return Emit(node, "", output);
    }

    // A folded operand as a node index, a constant becoming a node.
    std::uint16_t Materialize(const Folded & folded)
    {
        if (!folded.constant)
        {
            return folded.node;
        }

        D3txNode node;
        node.value = *folded.constant;
        Folded output;
        return Emit(node, "", output) ? output.node : kD3txNoNode;
    }

private:
    bool Emit(const D3txNode & node, const std::string & table, Folded & output)
    {
        if (m_pass.nodes.size() >= kD3txMaxNodesPerPass)
        {
            return false;
        }

        output.constant.reset();
        output.node = static_cast<std::uint16_t>(m_pass.nodes.size());
        m_pass.nodes.push_back(node);
        m_pass.nodeTables.push_back(table);
        return true;
    }

    const MaterialExpression & m_expression;
    const ShaderParms & m_parms;
    const MaterialTables & m_tables;
    PlannedPass & m_pass;
};

static bool SameExpression(const MaterialExpression & a, const MaterialExpression & b)
{
    if (a.root != b.root || a.nodes.size() != b.nodes.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < a.nodes.size(); ++index)
    {
        const ExpressionNode & x = a.nodes[index];
        const ExpressionNode & y = b.nodes[index];

        if (x.op != y.op || x.value != y.value || x.index != y.index || x.a != y.a || x.b != y.b || x.table != y.table)
        {
            return false;
        }
    }

    return true;
}

static float EvaluateConstant(const MaterialExpression & expression, const ShaderParms & parms, const MaterialTables & tables, float fallback)
{
    ExpressionInputs inputs;
    std::copy(parms.begin(), parms.end(), inputs.parms.begin());
    inputs.tables = &tables;
    return EvaluateExpression(expression, inputs, fallback);
}

// A translucent material's stage as a pass over the frame: its blend and what its texture holds,
// or why it draws none. id lights a translucent material's diffuse stage over the surface below
// (ONE, ONE): an add of the diffuse with the vertex colours, which carry the cook's light. The GS
// multiplies the frame only by an alpha, so a multiply stage takes its image's luminance there.
static const char * TranslucentBlend(const MaterialStage & stage, PlannedPass & planned)
{
    const std::string & blend = stage.blend;

    if (blend == "diffusemap")
    {
        planned.pass.flags = kD3txPassVertexColor;
        planned.source = PassImage::Diffuse;
    }
    else if (blend == "filter" || blend == "gl_dst_color,gl_zero" || blend == "gl_zero,gl_src_color")
    {
        planned.pass.blend = kD3txBlendMultiply;
        planned.source = PassImage::Luminance;
    }
    else if (blend == "gl_zero,gl_one_minus_src_color")
    {
        planned.pass.blend = kD3txBlendMultiply;
        planned.source = PassImage::InverseLuminance;
    }
    else if (blend == "blend" || blend == "gl_src_alpha,gl_one_minus_src_alpha")
    {
        planned.pass.blend = kD3txBlendAlpha;
        planned.source = PassImage::Alpha;
    }
    else if (blend != "add" && blend != "gl_one,gl_one")
    {
        return blend.empty() ? "no blend: it replaces the frame" : "a blend the GS cannot draw";
    }

    return nullptr;
}

// Why a stage draws no pass, or null when it does: 'translucent' for a translucent material's stages
// (TranslucentBlend), else an opaque material's additive stage.
static const char * PassRefusal(const MaterialStage & stage, bool translucent, PlannedPass & planned)
{
    if (translucent)
    {
        const char * refusal = stage.special ? "a cube map, texgen, video, render map or program" : TranslucentBlend(stage, planned);

        if (refusal != nullptr)
        {
            return refusal;
        }
    }
    else if (stage.blend != "add" && stage.blend != "gl_one,gl_one")
    {
        return "a blend other than add";
    }

    if (stage.special)
    {
        return "a cube map, texgen, video, render map or program";
    }

    if (stage.map.empty() || stage.map[0] == '_')
    {
        return "no image, or an intrinsic one";
    }

    if (stage.transformed)
    {
        return "a texture matrix";
    }

    if (stage.alphaTest)
    {
        return "an alpha test";
    }

    return nullptr;
}

} // namespace

std::uint8_t PassColorByte(float value)
{
    return static_cast<std::uint8_t>(std::clamp(std::round(value * 128.0f), 0.0f, 255.0f));
}

MaterialPassPlan PlanMaterialPasses(const MaterialInfo & material, const ShaderParms & parms, const MaterialTables & tables)
{
    MaterialPassPlan plan;

    if (material.diffuseStage >= 0)
    {
        const MaterialStage & diffuse = material.stages[static_cast<std::size_t>(material.diffuseStage)];

        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            plan.diffuseColor[channel] = std::clamp(EvaluateConstant(diffuse.color[channel], parms, tables, 1.0f), 0.0f, 2.0f);
        }

        if (diffuse.alphaTest)
        {
            const float reference = std::clamp(EvaluateConstant(diffuse.alphaTestValue, parms, tables, 0.5f), 0.0f, 1.0f);
            plan.alphaRef = static_cast<std::uint8_t>(std::round(reference * 128.0f));
        }
    }

    // A translucent material has no base pass: its stages draw over the frame. An alpha-tested one
    // draws opaque where it passes.
    const bool translucent = material.translucent && !material.alphaTest;

    if (translucent && (material.mirror || !material.deform.empty()))
    {
        plan.skipped.push_back(material.mirror ? "a mirror" : "a deform (" + material.deform + ")");
        return plan;
    }

    if (!translucent && (material.diffuseStage < 0 || material.translucent))
    {
        return plan;
    }

    for (std::size_t index = 0; index < material.stages.size(); ++index)
    {
        const MaterialStage & stage = material.stages[index];

        if ((stage.blend == "diffusemap" && !translucent) || stage.blend == "bumpmap" || stage.blend == "specularmap")
        {
            continue;
        }

        PlannedPass planned;
        planned.stage = static_cast<int>(index);
        planned.image = stage.map;
        const char * refusal = PassRefusal(stage, translucent, planned);

        if (refusal != nullptr)
        {
            plan.skipped.push_back("stage " + std::to_string(index) + " (" + stage.blend + " " + stage.map + "): " + refusal);
            continue;
        }

        bool fits = true;

        if (!stage.condition.Empty())
        {
            Folded condition;
            fits = Folder(stage.condition, parms, tables, planned).Fold(stage.condition.root, condition);

            if (fits && condition.constant && *condition.constant == 0.0f)
            {
                // Never drawn under these parms.
                continue;
            }

            planned.pass.condition = fits && !condition.constant ? condition.node : kD3txNoNode;
        }

        // A lit diffuse's colour is in the vertex colours with the light (the base pass's diffuse
        // colour, folded by the cook). A multiply stage's colour scales its alpha by its luminance,
        // a constant: the GS multiplies the frame by one value per pixel.
        if (fits && (planned.source == PassImage::Diffuse || planned.source == PassImage::Luminance || planned.source == PassImage::InverseLuminance))
        {
            std::array<float, 4> color = { 1.0f, 1.0f, 1.0f, 1.0f };
            bool constant = true;

            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                Folded value;
                PlannedPass scratch;
                const MaterialExpression & expression = stage.color[channel];
                constant = constant && (expression.Empty() || (Folder(expression, parms, tables, scratch).Fold(expression.root, value) && value.constant));
                color[channel] = expression.Empty() || !value.constant ? 1.0f : *value.constant;
            }

            const float luminance = 0.299f * color[0] + 0.587f * color[1] + 0.114f * color[2];

            if (planned.source != PassImage::Diffuse && (!constant || (planned.source == PassImage::InverseLuminance && luminance != 1.0f)))
            {
                plan.skipped.push_back("stage " + std::to_string(index) + " (" + stage.map + "): a multiply colour the GS cannot apply");
                continue;
            }

            if (planned.source != PassImage::Diffuse)
            {
                planned.pass.color[3] = PassColorByte(std::clamp(luminance, 0.0f, 1.0f));
            }

            planned.pass.nodeCount = static_cast<std::uint16_t>(planned.nodes.size());
            plan.passes.push_back(std::move(planned));
            continue;
        }

        for (std::size_t channel = 0; fits && channel < 4; ++channel)
        {
            const MaterialExpression & expression = stage.color[channel];

            if (expression.Empty())
            {
                planned.pass.color[channel] = 0x80;
                continue;
            }

            planned.pass.color[channel] = PassColorByte(EvaluateConstant(expression, parms, tables, 1.0f));

            // 'rgb' gives three channels one expression: they share its nodes.
            if (channel > 0 && SameExpression(expression, stage.color[channel - 1]))
            {
                planned.pass.colorNodes[channel] = planned.pass.colorNodes[channel - 1];
                continue;
            }

            Folded value;
            fits = Folder(expression, parms, tables, planned).Fold(expression.root, value);

            if (fits && !value.constant)
            {
                planned.pass.colorNodes[channel] = value.node;
            }
        }

        if (!fits)
        {
            plan.skipped.push_back("stage " + std::to_string(index) + " (" + stage.map + "): its expressions need too many nodes");
            continue;
        }

        const bool black = planned.pass.blend == kD3txBlendAdd && planned.pass.color[0] == 0 && planned.pass.color[1] == 0 &&
                           planned.pass.color[2] == 0 && planned.pass.colorNodes[0] == kD3txNoNode && planned.pass.colorNodes[1] == kD3txNoNode &&
                           planned.pass.colorNodes[2] == kD3txNoNode;

        if (black)
        {
            // Adds nothing.
            continue;
        }

        planned.pass.nodeCount = static_cast<std::uint16_t>(planned.nodes.size());
        plan.passes.push_back(std::move(planned));
    }

    return plan;
}

void AddPlannedPass(TexturePack & pack, const PlannedPass & planned, std::uint32_t texture, const MaterialTables & tables,
                    std::map<std::string, std::uint16_t> & tableOffsets)
{
    D3txPass pass = planned.pass;
    const auto base = static_cast<std::uint16_t>(pack.nodes.size());
    pass.texture = texture;
    pass.firstNode = base;
    pass.nodeCount = static_cast<std::uint16_t>(planned.nodes.size());
    pass.condition = pass.condition == kD3txNoNode ? kD3txNoNode : static_cast<std::uint16_t>(pass.condition + base);

    for (std::uint16_t & node : pass.colorNodes)
    {
        node = node == kD3txNoNode ? kD3txNoNode : static_cast<std::uint16_t>(node + base);
    }

    for (std::size_t at = 0; at < planned.nodes.size(); ++at)
    {
        D3txNode entry = planned.nodes[at];
        const auto op = static_cast<ExpressionOp>(entry.op);

        if (op == ExpressionOp::Table)
        {
            const std::string & name = planned.nodeTables[at];
            auto offset = tableOffsets.find(name);

            if (offset == tableOffsets.end())
            {
                offset = tableOffsets.emplace(name, static_cast<std::uint16_t>(pack.tableValues.size())).first;
                const std::vector<float> & values = tables.at(name).values;
                pack.tableValues.insert(pack.tableValues.end(), values.begin(), values.end());
            }

            entry.a = static_cast<std::uint16_t>(entry.a + base);
            entry.b = offset->second;
        }
        else if (op != ExpressionOp::Constant && op != ExpressionOp::Time)
        {
            entry.a = static_cast<std::uint16_t>(entry.a + base);
            entry.b = static_cast<std::uint16_t>(entry.b + base);
        }

        pack.nodes.push_back(entry);
    }

    pack.passes.push_back(pass);
}

std::vector<float> EvaluatePlannedNodes(const PlannedPass & pass, const MaterialTables & tables, float time)
{
    std::vector<float> values(pass.nodes.size(), 0.0f);

    for (std::size_t index = 0; index < pass.nodes.size(); ++index)
    {
        const D3txNode & node = pass.nodes[index];
        const auto op = static_cast<ExpressionOp>(node.op);

        if (op == ExpressionOp::Constant)
        {
            values[index] = node.value;
        }
        else if (op == ExpressionOp::Time)
        {
            values[index] = time;
        }
        else if (op == ExpressionOp::Table)
        {
            values[index] = Lookup(pass.nodeTables[index], values[node.a], tables);
        }
        else
        {
            values[index] = Apply(op, values[node.a], values[node.b]);
        }
    }

    return values;
}

} // namespace assets
