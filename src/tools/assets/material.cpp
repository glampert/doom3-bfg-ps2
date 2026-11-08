// ================================================================================================
// File: material.cpp
// Brief: Scan retail .mtr declarations for what the cooks need from each material: its diffuse image
// and whether that stage alpha-tests, whether it receives and casts light as id decides it, and a
// light material's stages with their register expressions (id's grammar, evaluated in the tool),
// and name the BIMAGE files id generated for an image, so the cooks read exactly what the engine
// would load.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/material.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// ------------------------------------------------------------------------------------------------
// Expressions
// ------------------------------------------------------------------------------------------------

static constexpr int kMaxExpressionNodes = 256;
static constexpr int kMaxExpressionDepth = 32;

// Reads expression tokens out of the declaration's: a word token mixing operators and names
// ("time*2") is split further, since the declaration lexer keeps '/', '.' and '-' inside words.
class ExpressionLexer final
{
public:
    ExpressionLexer(const std::vector<Token> & tokens, std::size_t & at)
        : m_tokens(tokens)
        , m_at(at)
    {
    }

    bool Peek(std::string & output)
    {
        if (!Fill())
        {
            return false;
        }

        output = m_pieces[m_piece];
        return true;
    }

    // Takes the piece Peek returns; a token is used up once its last piece is.
    bool Next(std::string & output)
    {
        if (!Peek(output))
        {
            return false;
        }

        if (++m_piece == m_pieces.size())
        {
            m_pieces.clear();
            m_piece = 0;
            ++m_at;
        }

        return true;
    }

    // An expression must end on a token boundary.
    bool Whole() const
    {
        return m_piece == 0;
    }

private:
    static bool IsOperator(char character)
    {
        return std::string_view("+-*/%<>=!&|").find(character) != std::string_view::npos;
    }

    // Splits the token at m_at into pieces, without consuming it.
    bool Fill()
    {
        if (!m_pieces.empty())
        {
            return true;
        }

        if (m_at >= m_tokens.size() || m_tokens[m_at].quoted)
        {
            return false;
        }

        const std::string & text = m_tokens[m_at].text;
        std::size_t index = 0;

        while (index < text.size())
        {
            const char character = text[index];

            if (IsOperator(character))
            {
                const std::string pair = text.substr(index, 2);
                const bool twoCharacters = pair == ">=" || pair == "<=" || pair == "==" || pair == "!=" || pair == "&&" || pair == "||";
                m_pieces.push_back(text.substr(index, twoCharacters ? 2 : 1));
                index += twoCharacters ? 2 : 1;
            }
            else
            {
                const std::size_t start = index;

                while (index < text.size() && !IsOperator(text[index]))
                {
                    ++index;
                }

                m_pieces.push_back(text.substr(start, index - start));
            }
        }

        return !m_pieces.empty();
    }

    const std::vector<Token> & m_tokens;
    std::size_t & m_at;
    std::vector<std::string> m_pieces;
    std::size_t m_piece = 0;
};

static bool IsNumber(const std::string & text)
{
    if (text.empty() || text == ".")
    {
        return false;
    }

    for (const char character : text)
    {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0 && character != '.')
        {
            return false;
        }
    }

    return true;
}

static bool IsName(const std::string & text)
{
    return !text.empty() && (std::isalpha(static_cast<unsigned char>(text[0])) != 0 || text[0] == '_');
}

class ExpressionParser final
{
public:
    ExpressionParser(ExpressionLexer & lexer, MaterialExpression & output)
        : m_lexer(lexer)
        , m_output(output)
    {
    }

    bool Parse(std::string & error)
    {
        m_output = {};
        m_output.root = Priority(4);

        if (m_output.root < 0 || !m_error.empty() || !m_lexer.Whole())
        {
            error = m_error.empty() ? "an expression ends inside a token" : m_error;
            m_output = {};
            return false;
        }

        return true;
    }

private:
    int Fail(const std::string & message)
    {
        if (m_error.empty())
        {
            m_error = message;
        }

        return -1;
    }

    int Emit(const ExpressionNode & node)
    {
        if (m_output.nodes.size() >= static_cast<std::size_t>(kMaxExpressionNodes))
        {
            return Fail("an expression is too long");
        }

        m_output.nodes.push_back(node);
        return static_cast<int>(m_output.nodes.size()) - 1;
    }

    int Leaf(ExpressionOp op, float value = 0.0f, int index = 0)
    {
        ExpressionNode node;
        node.op = op;
        node.value = value;
        node.index = index;
        return Emit(node);
    }

    // ParseExpressionPriority: 1 * / %, 2 + -, 3 comparisons, 4 && ||; the right side parses at
    // the same priority, so each level is right-associative.
    int Priority(int priority)
    {
        if (++m_depth > kMaxExpressionDepth)
        {
            return Fail("an expression nests too deeply");
        }

        const int a = priority == 0 ? Term() : Priority(priority - 1);
        --m_depth;

        if (a < 0)
        {
            return -1;
        }

        static constexpr struct
        {
            int priority;
            const char * token;
            ExpressionOp op;
        } kOperators[] = {
            { 1, "*", ExpressionOp::Multiply }, { 1, "/", ExpressionOp::Divide }, { 1, "%", ExpressionOp::Mod },
            { 2, "+", ExpressionOp::Add }, { 2, "-", ExpressionOp::Subtract },
            { 3, ">", ExpressionOp::Greater }, { 3, ">=", ExpressionOp::GreaterEqual }, { 3, "<", ExpressionOp::Less },
            { 3, "<=", ExpressionOp::LessEqual }, { 3, "==", ExpressionOp::Equal }, { 3, "!=", ExpressionOp::NotEqual },
            { 4, "&&", ExpressionOp::And }, { 4, "||", ExpressionOp::Or },
        };

        std::string token;

        if (priority == 0 || !m_lexer.Peek(token))
        {
            return a;
        }

        for (const auto & entry : kOperators)
        {
            if (entry.priority == priority && token == entry.token)
            {
                (void)m_lexer.Next(token);
                ++m_depth;
                const int b = Priority(priority);
                --m_depth;

                if (b < 0)
                {
                    return -1;
                }

                ExpressionNode node;
                node.op = entry.op;
                node.a = a;
                node.b = b;
                return Emit(node);
            }
        }

        return a;
    }

    int Term()
    {
        std::string token;

        if (!m_lexer.Next(token))
        {
            return Fail("an expression ends early");
        }

        if (token == "(")
        {
            const int a = Priority(4);

            if (a < 0 || !m_lexer.Next(token) || token != ")")
            {
                return Fail("an expression lacks its ')'");
            }

            return a;
        }

        const std::string word = Lower(token);

        if (word == "time")
        {
            return Leaf(ExpressionOp::Time);
        }

        if (word == "sound")
        {
            return Leaf(ExpressionOp::Sound);
        }

        if (word == "fragmentprograms")
        {
            return Leaf(ExpressionOp::Constant, 1.0f);
        }

        for (int index = 0; index < 12; ++index)
        {
            if (word == "parm" + std::to_string(index))
            {
                return Leaf(ExpressionOp::Parm, 0.0f, index);
            }
        }

        for (int index = 0; index < 8; ++index)
        {
            if (word == "global" + std::to_string(index))
            {
                return Leaf(ExpressionOp::Global, 0.0f, index);
            }
        }

        if (token == "-")
        {
            if (!m_lexer.Next(token) || !IsNumber(token))
            {
                return Fail("a bad negative number");
            }

            return Leaf(ExpressionOp::Constant, -std::strtof(token.c_str(), nullptr));
        }

        if (IsNumber(token))
        {
            return Leaf(ExpressionOp::Constant, std::strtof(token.c_str(), nullptr));
        }

        if (IsName(token))
        {
            std::string bracket;

            if (!m_lexer.Next(bracket) || bracket != "[")
            {
                return Fail("a bad term '" + token + "'");
            }

            const int index = Priority(4);

            if (index < 0 || !m_lexer.Next(bracket) || bracket != "]")
            {
                return Fail("a table lookup lacks its ']'");
            }

            ExpressionNode node;
            node.op = ExpressionOp::Table;
            node.a = index;
            node.table = word;
            return Emit(node);
        }

        return Fail("a bad term '" + token + "'");
    }

    ExpressionLexer & m_lexer;
    MaterialExpression & m_output;
    std::string m_error;
    int m_depth = 0;
};

static float TableLookup(const MaterialTable & table, float index)
{
    // idDeclTable::TableLookup; 'values' repeats its first entry at the end.
    const int domain = static_cast<int>(table.values.size()) - 1;

    if (domain <= 1)
    {
        return 1.0f;
    }

    int whole = 0;
    float fraction = 0.0f;

    if (table.clamp)
    {
        index *= static_cast<float>(domain - 1);

        if (index >= static_cast<float>(domain - 1))
        {
            return table.values[static_cast<std::size_t>(domain - 1)];
        }

        if (index <= 0.0f)
        {
            return table.values[0];
        }

        whole = static_cast<int>(index);
        fraction = index - static_cast<float>(whole);
    }
    else
    {
        index *= static_cast<float>(domain);

        if (index < 0.0f)
        {
            index += static_cast<float>(domain) * std::ceil(-index / static_cast<float>(domain));
        }

        whole = static_cast<int>(std::floor(index));
        fraction = index - static_cast<float>(whole);
        whole %= domain;
    }

    if (!table.snap)
    {
        return table.values[static_cast<std::size_t>(whole)] * (1.0f - fraction) + table.values[static_cast<std::size_t>(whole) + 1] * fraction;
    }

    return table.values[static_cast<std::size_t>(whole)];
}

static float Evaluate(const MaterialExpression & expression, int node, const ExpressionInputs & inputs)
{
    const ExpressionNode & entry = expression.nodes[static_cast<std::size_t>(node)];

    switch (entry.op)
    {
        case ExpressionOp::Constant:
            return entry.value;
        case ExpressionOp::Time:
            return inputs.time;
        case ExpressionOp::Parm:
            return inputs.parms[static_cast<std::size_t>(entry.index)];
        case ExpressionOp::Global:
            return inputs.globals[static_cast<std::size_t>(entry.index)];
        case ExpressionOp::Sound:
            return inputs.sound;
        case ExpressionOp::Table:
        {
            const float index = Evaluate(expression, entry.a, inputs);

            if (inputs.tables == nullptr)
            {
                return 1.0f;
            }

            const auto found = inputs.tables->find(entry.table);
            return found == inputs.tables->end() ? 1.0f : TableLookup(found->second, index);
        }
        default:
            break;
    }

    const float a = Evaluate(expression, entry.a, inputs);
    const float b = Evaluate(expression, entry.b, inputs);

    switch (entry.op)
    {
        case ExpressionOp::Add:
            return a + b;
        case ExpressionOp::Subtract:
            return a - b;
        case ExpressionOp::Multiply:
            return a * b;
        case ExpressionOp::Divide:
            return b != 0.0f ? a / b : 0.0f;
        case ExpressionOp::Mod:
        {
            // id truncates both to integers.
            const int divisor = static_cast<int>(b);
            return divisor != 0 ? static_cast<float>(static_cast<int>(a) % divisor) : 0.0f;
        }
        case ExpressionOp::Greater:
            return a > b ? 1.0f : 0.0f;
        case ExpressionOp::GreaterEqual:
            return a >= b ? 1.0f : 0.0f;
        case ExpressionOp::Less:
            return a < b ? 1.0f : 0.0f;
        case ExpressionOp::LessEqual:
            return a <= b ? 1.0f : 0.0f;
        case ExpressionOp::Equal:
            return a == b ? 1.0f : 0.0f;
        case ExpressionOp::NotEqual:
            return a != b ? 1.0f : 0.0f;
        case ExpressionOp::And:
            return a != 0.0f && b != 0.0f ? 1.0f : 0.0f;
        case ExpressionOp::Or:
            return a != 0.0f || b != 0.0f ? 1.0f : 0.0f;
        default:
            return 0.0f;
    }
}

// ------------------------------------------------------------------------------------------------
// Declarations
// ------------------------------------------------------------------------------------------------

struct Scan
{
    MaterialInfo info;
    MaterialStage stage;
    bool translucentKeyword = false;
    bool noShadows = false;
    bool forceShadows = false;
};

static bool InteractionBlend(const std::string & blend)
{
    return blend == "diffusemap" || blend == "bumpmap" || blend == "specularmap";
}

static void CloseStage(Scan & scan)
{
    MaterialInfo & info = scan.info;
    const MaterialStage & stage = scan.stage;

    if (stage.blend == "diffusemap")
    {
        if (info.diffuse.empty())
        {
            info.diffuse = stage.map;
            info.alphaTest = stage.alphaTest;
        }
    }
    else if (stage.blend != "bumpmap" && stage.blend != "specularmap")
    {
        ++info.otherStages;
    }

    info.lit = info.lit || InteractionBlend(stage.blend);
    info.stages.push_back(stage);
}

// Material.cpp's coverage: a first ambient stage that blends with the destination (its shorthands,
// or a destination factor other than zero, or a source factor reading the destination).
static bool BlendsWithDestination(const std::string & blend)
{
    if (blend == "blend" || blend == "add" || blend == "filter" || blend == "modulate" || blend == "none")
    {
        return true;
    }

    const std::size_t comma = blend.find(',');

    if (comma == std::string::npos)
    {
        return false;
    }

    const std::string source = blend.substr(0, comma);
    const std::string destination = blend.substr(comma + 1);
    return destination != "gl_zero" || source == "gl_dst_color" || source == "gl_one_minus_dst_color" || source == "gl_dst_alpha" ||
           source == "gl_one_minus_dst_alpha";
}

static void CloseMaterial(Scan & scan)
{
    MaterialInfo & info = scan.info;
    info.translucent = scan.translucentKeyword || info.stages.empty() || (!info.lit && BlendsWithDestination(info.stages[0].blend));
    info.castsShadow = scan.forceShadows || !(scan.noShadows || info.translucent);
}

static void ParseStageExpression(const std::vector<Token> & tokens, std::size_t & at, MaterialExpression & output, MaterialInfo & info)
{
    ExpressionLexer lexer(tokens, at);
    ExpressionParser parser(lexer, output);
    std::string error;

    if (!parser.Parse(error) && info.expressionError.empty())
    {
        info.expressionError = error;
    }
}

// 'table name { [snap] [clamp] { v, v, ... } }'.
static bool ParseTable(const std::vector<Token> & tokens, std::size_t & at, const std::string & name, MaterialTables & tables, std::string & error)
{
    MaterialTable table;

    while (at < tokens.size() && !Is(tokens[at], "{"))
    {
        const std::string word = Lower(tokens[at].text);
        table.snap = table.snap || word == "snap";
        table.clamp = table.clamp || word == "clamp";
        ++at;
    }

    if (at >= tokens.size())
    {
        error = "table " + name + " lacks its values";
        return false;
    }

    ++at;

    while (at < tokens.size() && !Is(tokens[at], "}"))
    {
        if (!Is(tokens[at], ","))
        {
            // Retail tables hold typos ("..5" in helld3xp.mtr); idLexer::ParseFloat warns and reads
            // what it can, as atof does.
            table.values.push_back(std::strtof(tokens[at].text.c_str(), nullptr));
        }

        ++at;
    }

    if (at + 1 >= tokens.size() || !Is(tokens[at + 1], "}"))
    {
        error = "table " + name + " lacks its closing brace";
        return false;
    }

    at += 2;

    if (!table.values.empty())
    {
        table.values.push_back(table.values[0]);
    }

    tables.emplace(name, std::move(table));
    return true;
}

static void SkipBody(const std::vector<Token> & tokens, std::size_t & at)
{
    int depth = 1;

    while (at < tokens.size() && depth > 0)
    {
        depth += Is(tokens[at], "{") ? 1 : Is(tokens[at], "}") ? -1 : 0;
        ++at;
    }
}

} // namespace

bool ParseMaterialExpression(std::string_view text, MaterialExpression & output, std::string & error)
{
    std::vector<Token> tokens;

    if (!Tokenize(text, tokens, error))
    {
        return false;
    }

    std::size_t at = 0;
    ExpressionLexer lexer(tokens, at);
    ExpressionParser parser(lexer, output);

    if (!parser.Parse(error))
    {
        return false;
    }

    if (at != tokens.size())
    {
        error = "text follows the expression";
        output = {};
        return false;
    }

    return true;
}

unsigned ExpressionReads(const MaterialExpression & expression)
{
    unsigned reads = 0;

    for (const ExpressionNode & node : expression.nodes)
    {
        reads |= node.op == ExpressionOp::Time ? kExpressionReadsTime : 0U;
        reads |= node.op == ExpressionOp::Sound ? kExpressionReadsSound : 0U;
        reads |= node.op == ExpressionOp::Parm ? kExpressionReadsParms : 0U;
        reads |= node.op == ExpressionOp::Global ? kExpressionReadsGlobals : 0U;
        reads |= node.op == ExpressionOp::Table ? kExpressionReadsTables : 0U;
    }

    return reads;
}

float EvaluateExpression(const MaterialExpression & expression, const ExpressionInputs & inputs, float fallback)
{
    return expression.Empty() ? fallback : Evaluate(expression, expression.root, inputs);
}

bool ScanMaterials(std::string_view text, std::map<std::string, MaterialInfo> & materials, std::string & error, MaterialTables * tables)
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
        bool table = false;
        std::string type = Lower(tokens[at].text);

        if (type == "material" || type == "table" || type == "skin" || type == "sound" || type == "particle" || type == "fx" ||
            type == "entitydef" || type == "model" || type == "mapdef" || type == "pda" || type == "email" || type == "video" ||
            type == "audio")
        {
            material = type == "material";
            table = type == "table";
            ++at;
        }

        if (at + 1 >= tokens.size() || !Is(tokens[at + 1], "{"))
        {
            error = "declaration without a name and body at line " + std::to_string(at < tokens.size() ? tokens[at].line : 0);
            return false;
        }

        const std::string name = Lower(tokens[at].text);
        at += 2;

        if (table)
        {
            if (tables != nullptr)
            {
                if (!ParseTable(tokens, at, name, *tables, error))
                {
                    return false;
                }
            }
            else
            {
                SkipBody(tokens, at);
            }

            continue;
        }

        Scan scan;
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
                    scan.stage = {};
                }

                ++at;
            }
            else if (Is(token, "}"))
            {
                if (depth == 2)
                {
                    CloseStage(scan);
                }

                --depth;
                ++at;
            }
            else if (depth == 1 && InteractionBlend(word))
            {
                ++at;
                scan.stage = {};
                scan.stage.blend = word;
                scan.stage.map = ImageArgument(tokens, at);
                CloseStage(scan);
            }
            else if (depth == 1 && word == "lightfalloffimage")
            {
                ++at;
                scan.info.lightFalloffImage = ImageArgument(tokens, at);
            }
            else if (depth == 1 && (word == "foglight" || word == "blendlight" || word == "ambientlight" || word == "noshadows" ||
                                    word == "forceshadows" || word == "translucent"))
            {
                scan.info.fogLight = scan.info.fogLight || word == "foglight";
                scan.info.blendLight = scan.info.blendLight || word == "blendlight";
                scan.info.ambientLight = scan.info.ambientLight || word == "ambientlight";
                scan.noShadows = scan.noShadows || word == "noshadows";
                scan.forceShadows = scan.forceShadows || word == "forceshadows";
                scan.translucentKeyword = scan.translucentKeyword || word == "translucent";
                ++at;
            }
            else if (depth == 1 && word == "deform" && at + 1 < tokens.size())
            {
                const std::string deform = Lower(tokens[at + 1].text);
                scan.noShadows = scan.noShadows || deform == "sprite" || deform == "tube" || deform == "flare";
                at += 2;
            }
            else if (depth == 2 && word == "blend" && at + 1 < tokens.size())
            {
                scan.stage.blend = Lower(tokens[at + 1].text);
                at += 2;

                if (at + 1 < tokens.size() && Is(tokens[at], ","))
                {
                    scan.stage.blend += "," + Lower(tokens[at + 1].text);
                    at += 2;
                }
            }
            else if (depth == 2 && word == "map")
            {
                ++at;
                scan.stage.map = ImageArgument(tokens, at);
            }
            else if (depth == 2 && word == "alphatest")
            {
                scan.stage.alphaTest = true;
                ++at;
            }
            else if (depth == 2 && (word == "clamp" || word == "zeroclamp" || word == "alphazeroclamp"))
            {
                scan.stage.clamp = scan.stage.clamp || word == "clamp";
                scan.stage.zeroClamp = scan.stage.zeroClamp || word != "clamp";
                ++at;
            }
            else if (depth == 2 && (word == "translate" || word == "scroll" || word == "scale" || word == "centerscale" || word == "shear" ||
                                    word == "rotate"))
            {
                scan.stage.transformed = true;
                ++at;
            }
            else if (depth == 2 && word == "colored")
            {
                for (int channel = 0; channel < 4; ++channel)
                {
                    MaterialExpression & expression = scan.stage.color[static_cast<std::size_t>(channel)];
                    expression = {};
                    ExpressionNode node;
                    node.op = ExpressionOp::Parm;
                    node.index = channel;
                    expression.nodes.push_back(node);
                    expression.root = 0;
                }

                ++at;
            }
            else if (depth == 2 && (word == "if" || word == "red" || word == "green" || word == "blue" || word == "alpha" || word == "rgb" ||
                                    word == "rgba"))
            {
                ++at;
                MaterialExpression expression;
                ParseStageExpression(tokens, at, expression, scan.info);

                if (word == "if")
                {
                    scan.stage.condition = expression;
                }
                else
                {
                    const int first = word == "green" ? 1 : word == "blue" ? 2 : word == "alpha" ? 3 : 0;
                    const int last = word == "rgb" ? 2 : word == "rgba" ? 3 : first;

                    for (int channel = first; channel <= last; ++channel)
                    {
                        scan.stage.color[static_cast<std::size_t>(channel)] = expression;
                    }
                }
            }
            else if (depth == 2 && word == "color")
            {
                ++at;

                for (int channel = 0; channel < 4; ++channel)
                {
                    if (channel > 0)
                    {
                        if (at >= tokens.size() || !Is(tokens[at], ","))
                        {
                            scan.info.expressionError = scan.info.expressionError.empty() ? "'color' lacks a ','" : scan.info.expressionError;
                            break;
                        }

                        ++at;
                    }

                    ParseStageExpression(tokens, at, scan.stage.color[static_cast<std::size_t>(channel)], scan.info);
                }
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
            CloseMaterial(scan);
            materials.emplace(name, std::move(scan.info));
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
