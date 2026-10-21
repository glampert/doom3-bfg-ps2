// ================================================================================================
// File: palette.cpp
// Brief: Build GS-exact color palettes for paletted textures, map images onto them with measured
// error, and group images that can share one palette within an error budget.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace assets
{
namespace
{

using Vector4 = std::array<double, 4>;

static std::uint32_t PackColor(const PaletteColor & color)
{
    return static_cast<std::uint32_t>(color.r) | (static_cast<std::uint32_t>(color.g) << 8) |
           (static_cast<std::uint32_t>(color.b) << 16) | (static_cast<std::uint32_t>(color.a) << 24);
}

// The 8-bit alpha whose GS conversion, (a * 128 + 127) / 255, is the nearest 0..128 level.
static std::uint8_t SnapAlpha(double alpha)
{
    const double level = std::clamp(std::round(std::clamp(alpha, 0.0, 255.0) * 128.0 / 255.0), 0.0, 128.0);
    return static_cast<std::uint8_t>(std::lround(level * 255.0 / 128.0));
}

static std::uint8_t RoundChannel(double value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
}

static Vector4 Components(const PaletteColor & color)
{
    return { static_cast<double>(color.r), static_cast<double>(color.g), static_cast<double>(color.b),
             static_cast<double>(color.a) };
}

// Squared maximum alpha of each texel's 3x3 neighborhood, normalized to 0..1.
static std::vector<double> NeighborhoodWeights(const RgbaImage & image)
{
    std::vector<double> weights(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));

    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            int maximum = 0;

            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int sx = std::clamp(x + dx, 0, image.width - 1);
                    const int sy = std::clamp(y + dy, 0, image.height - 1);
                    maximum = std::max(maximum, static_cast<int>(image.At(sx, sy)[3]));
                }
            }

            const double weight = maximum / 255.0;
            weights[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)] = weight * weight;
        }
    }

    return weights;
}

struct Sample
{
    Vector4 value{};
    double count = 0.0;
    double rgbWeight = 0.0;
};

static std::vector<Sample> Samples(const ColorHistogram & histogram)
{
    std::vector<Sample> samples;
    samples.reserve(histogram.Entries().size());

    for (const ColorHistogram::Entry & entry : histogram.Entries())
    {
        Sample sample;
        sample.value = Components(entry.color);
        sample.count = entry.count;
        // A color seen only on isolated transparent texels keeps a token RGB weight, so every
        // division below stays defined without letting it steer the RGB of an entry.
        sample.rgbWeight = std::max(entry.rgbWeight, 1e-6 * entry.count);
        samples.push_back(sample);
    }

    return samples;
}

static double SampleDistance(const Sample & sample, const Vector4 & center)
{
    const double dr = sample.value[0] - center[0];
    const double dg = sample.value[1] - center[1];
    const double db = sample.value[2] - center[2];
    const double da = sample.value[3] - center[3];
    return (sample.rgbWeight / sample.count) * (dr * dr + dg * dg + db * db) + da * da;
}

struct Box
{
    std::vector<std::size_t> members;
    double sse = 0.0;
};

static void BoxStatistics(const std::vector<Sample> & samples, const std::vector<std::size_t> & members, Vector4 & mean, double & sse)
{
    double rgbWeight = 0.0;
    double count = 0.0;
    Vector4 sums{};

    for (const std::size_t index : members)
    {
        const Sample & sample = samples[index];
        rgbWeight += sample.rgbWeight;
        count += sample.count;

        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            sums[channel] += sample.rgbWeight * sample.value[channel];
        }

        sums[3] += sample.count * sample.value[3];
    }

    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        mean[channel] = rgbWeight > 0.0 ? sums[channel] / rgbWeight : 0.0;
    }

    mean[3] = count > 0.0 ? sums[3] / count : 0.0;
    sse = 0.0;

    for (const std::size_t index : members)
    {
        const Sample & sample = samples[index];

        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            sse += sample.rgbWeight * (sample.value[channel] - mean[channel]) * (sample.value[channel] - mean[channel]);
        }

        sse += sample.count * (sample.value[3] - mean[3]) * (sample.value[3] - mean[3]);
    }
}

// Splits the box at the position along its widest weighted axis that minimizes total SSE.
static Box SplitBox(const std::vector<Sample> & samples, Box & box)
{
    Vector4 mean{};
    BoxStatistics(samples, box.members, mean, box.sse);
    std::size_t axis = 0;
    double spread = -1.0;

    for (std::size_t channel = 0; channel < 4; ++channel)
    {
        double sum = 0.0;

        for (const std::size_t index : box.members)
        {
            const Sample & sample = samples[index];
            const double weight = channel < 3 ? sample.rgbWeight : sample.count;
            sum += weight * (sample.value[channel] - mean[channel]) * (sample.value[channel] - mean[channel]);
        }

        if (sum > spread)
        {
            spread = sum;
            axis = channel;
        }
    }

    std::sort(box.members.begin(), box.members.end(), [&](std::size_t first, std::size_t second)
              {
                  return samples[first].value[axis] < samples[second].value[axis];
              });
    const std::size_t count = box.members.size();
    std::vector<double> prefixRgb(count + 1, 0.0);
    std::vector<double> prefixCount(count + 1, 0.0);
    std::vector<Vector4> prefixSum(count + 1, Vector4{});
    std::vector<Vector4> prefixSquare(count + 1, Vector4{});

    for (std::size_t i = 0; i < count; ++i)
    {
        const Sample & sample = samples[box.members[i]];
        prefixRgb[i + 1] = prefixRgb[i] + sample.rgbWeight;
        prefixCount[i + 1] = prefixCount[i] + sample.count;

        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            const double weight = channel < 3 ? sample.rgbWeight : sample.count;
            prefixSum[i + 1][channel] = prefixSum[i][channel] + weight * sample.value[channel];
            prefixSquare[i + 1][channel] = prefixSquare[i][channel] + weight * sample.value[channel] * sample.value[channel];
        }
    }

    const auto RangeSse = [&](std::size_t begin, std::size_t end)
    {
        double total = 0.0;

        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            const double weight = channel < 3 ? prefixRgb[end] - prefixRgb[begin] : prefixCount[end] - prefixCount[begin];
            const double sum = prefixSum[end][channel] - prefixSum[begin][channel];
            const double square = prefixSquare[end][channel] - prefixSquare[begin][channel];

            if (weight > 0.0)
            {
                total += square - sum * sum / weight;
            }
        }

        return total;
    };

    std::size_t split = count / 2;
    double best = std::numeric_limits<double>::max();

    for (std::size_t i = 1; i < count; ++i)
    {
        if (samples[box.members[i]].value[axis] == samples[box.members[i - 1]].value[axis])
        {
            continue;
        }

        const double total = RangeSse(0, i) + RangeSse(i, count);

        if (total < best)
        {
            best = total;
            split = i;
        }
    }

    Box right;
    right.members.assign(box.members.begin() + static_cast<std::ptrdiff_t>(split), box.members.end());
    box.members.resize(split);
    BoxStatistics(samples, box.members, mean, box.sse);
    BoxStatistics(samples, right.members, mean, right.sse);
    return right;
}

static std::size_t Nearest(const Sample & sample, const std::vector<Vector4> & centers, double & distance)
{
    std::size_t best = 0;
    distance = std::numeric_limits<double>::max();

    for (std::size_t index = 0; index < centers.size(); ++index)
    {
        const double candidate = SampleDistance(sample, centers[index]);

        if (candidate < distance)
        {
            distance = candidate;
            best = index;
        }
    }

    return best;
}

struct PointSet
{
    std::vector<Sample> points;
    double intraSse = 0.0;
    double texels = 0.0;
};

// Collapses a histogram onto its palette: one weighted point per used entry, plus the error the
// palette already leaves inside each entry. Merges are then evaluated on points, not texels.
static PointSet Summarize(const ColorHistogram & histogram, const std::vector<PaletteColor> & palette)
{
    std::vector<Vector4> centers;

    for (const PaletteColor & color : palette)
    {
        centers.push_back(Components(color));
    }

    PointSet set;
    std::vector<Sample> points(palette.size());

    for (std::size_t index = 0; index < palette.size(); ++index)
    {
        points[index].value = centers[index];
    }

    for (const Sample & sample : Samples(histogram))
    {
        double distance = 0.0;
        const std::size_t index = Nearest(sample, centers, distance);
        points[index].count += sample.count;
        points[index].rgbWeight += sample.rgbWeight;
        set.intraSse += distance * sample.count;
        set.texels += sample.count;
    }

    for (const Sample & point : points)
    {
        if (point.count > 0.0)
        {
            set.points.push_back(point);
        }
    }

    return set;
}

static double PointsError(const std::vector<Sample> & points, const std::vector<Vector4> & centers)
{
    double total = 0.0;

    for (const Sample & point : points)
    {
        double distance = 0.0;
        Nearest(point, centers, distance);
        total += distance * point.count;
    }

    return total;
}

// The largest move any point makes to its nearest center, in the distance's 8-bit units.
static double PointsMaxShift(const std::vector<Sample> & points, const std::vector<Vector4> & centers)
{
    double largest = 0.0;

    for (const Sample & point : points)
    {
        double distance = 0.0;
        Nearest(point, centers, distance);
        largest = std::max(largest, distance);
    }

    return std::sqrt(largest);
}

static std::vector<PaletteColor> PaletteFromPoints(const std::vector<Sample> & points, int colors)
{
    ColorHistogram histogram;

    for (const Sample & point : points)
    {
        const PaletteColor color{ RoundChannel(point.value[0]), RoundChannel(point.value[1]), RoundChannel(point.value[2]),
                                  RoundChannel(point.value[3]) };
        histogram.Add(color, point.count, point.rgbWeight);
    }

    return BuildPalette(histogram, colors);
}

// How badly each set's points are covered by the other's: a cheap ordering for merge attempts.
static double ChamferCost(const std::vector<Sample> & first, const std::vector<Sample> & second)
{
    const auto Cover = [](const std::vector<Sample> & from, const std::vector<Sample> & to)
    {
        double total = 0.0;

        for (const Sample & point : from)
        {
            double best = std::numeric_limits<double>::max();

            for (const Sample & other : to)
            {
                best = std::min(best, SampleDistance(point, other.value));
            }

            total += best * point.count;
        }

        return total;
    };

    return Cover(first, second) + Cover(second, first);
}

static double Rmse(double sse, double texels)
{
    return texels > 0.0 ? std::sqrt(sse / (4.0 * texels)) : 0.0;
}

} // namespace

void ColorHistogram::Add(const RgbaImage & image)
{
    const std::vector<double> weights = NeighborhoodWeights(image);

    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            const std::uint8_t * texel = image.At(x, y);
            const PaletteColor color{ texel[0], texel[1], texel[2], texel[3] };
            Add(color, 1.0, weights[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)]);
        }
    }
}

void ColorHistogram::Add(const PaletteColor & color, double count, double rgbWeight)
{
    const auto [found, inserted] = m_index.try_emplace(PackColor(color), m_entries.size());

    if (inserted)
    {
        m_entries.push_back({ color, 0.0, 0.0 });
    }

    Entry & entry = m_entries[found->second];
    entry.count += count;
    entry.rgbWeight += rgbWeight;
    m_texels += count;
}

void ColorHistogram::Merge(const ColorHistogram & other)
{
    for (const Entry & entry : other.m_entries)
    {
        Add(entry.color, entry.count, entry.rgbWeight);
    }
}

const std::vector<ColorHistogram::Entry> & ColorHistogram::Entries() const
{
    return m_entries;
}

double ColorHistogram::Texels() const
{
    return m_texels;
}

bool ColorHistogram::WhiteRgb() const
{
    return std::all_of(m_entries.begin(), m_entries.end(), [](const Entry & entry)
                       {
                           return entry.color.r == 255 && entry.color.g == 255 && entry.color.b == 255;
                       });
}

double ColorDistance(const ColorHistogram::Entry & entry, const PaletteColor & color)
{
    Sample sample;
    sample.value = Components(entry.color);
    sample.count = entry.count;
    sample.rgbWeight = std::max(entry.rgbWeight, 1e-6 * entry.count);
    return SampleDistance(sample, Components(color));
}

std::vector<PaletteColor> BuildPalette(const ColorHistogram & histogram, int colors, int iterations)
{
    const std::vector<Sample> samples = Samples(histogram);

    if (samples.empty() || colors < 1)
    {
        return { PaletteColor{} };
    }

    std::vector<Box> boxes(1);
    boxes[0].members.resize(samples.size());
    std::iota(boxes[0].members.begin(), boxes[0].members.end(), std::size_t{ 0 });
    Vector4 mean{};
    BoxStatistics(samples, boxes[0].members, mean, boxes[0].sse);

    while (static_cast<int>(boxes.size()) < colors)
    {
        std::size_t pick = boxes.size();
        double largest = 0.0;

        for (std::size_t index = 0; index < boxes.size(); ++index)
        {
            if (boxes[index].members.size() > 1 && boxes[index].sse > largest)
            {
                largest = boxes[index].sse;
                pick = index;
            }
        }

        if (pick == boxes.size())
        {
            break;
        }

        Box right = SplitBox(samples, boxes[pick]);
        boxes.push_back(std::move(right));
    }

    std::vector<Vector4> centers;

    for (const Box & box : boxes)
    {
        double sse = 0.0;
        BoxStatistics(samples, box.members, mean, sse);
        centers.push_back(mean);
    }

    // Lloyd refinement; an emptied entry restarts at the worst-served sample.
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        std::vector<Vector4> sums(centers.size(), Vector4{});
        std::vector<double> rgbWeights(centers.size(), 0.0);
        std::vector<double> counts(centers.size(), 0.0);
        double worst = -1.0;
        std::size_t worstSample = 0;
        bool changed = false;

        for (std::size_t index = 0; index < samples.size(); ++index)
        {
            const Sample & sample = samples[index];
            double distance = 0.0;
            const std::size_t center = Nearest(sample, centers, distance);

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                sums[center][channel] += sample.rgbWeight * sample.value[channel];
            }

            sums[center][3] += sample.count * sample.value[3];
            rgbWeights[center] += sample.rgbWeight;
            counts[center] += sample.count;

            if (distance * sample.count > worst)
            {
                worst = distance * sample.count;
                worstSample = index;
            }
        }

        for (std::size_t center = 0; center < centers.size(); ++center)
        {
            Vector4 next = centers[center];

            if (counts[center] <= 0.0)
            {
                next = samples[worstSample].value;
            }
            else
            {
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    next[channel] = rgbWeights[center] > 0.0 ? sums[center][channel] / rgbWeights[center] : centers[center][channel];
                }

                next[3] = sums[center][3] / counts[center];
            }

            changed = changed || next != centers[center];
            centers[center] = next;
        }

        if (!changed)
        {
            break;
        }
    }

    std::vector<PaletteColor> palette;

    for (const Vector4 & center : centers)
    {
        palette.push_back({ RoundChannel(center[0]), RoundChannel(center[1]), RoundChannel(center[2]), SnapAlpha(center[3]) });
    }

    return palette;
}

std::vector<PaletteColor> BuildAlphaPalette(const ColorHistogram & histogram, int levels)
{
    std::array<double, 256> counts{};

    for (const ColorHistogram::Entry & entry : histogram.Entries())
    {
        counts[entry.color.a] += entry.count;
    }

    // Fixed transparent and opaque ends; the interior levels start evenly spaced.
    const int interior = std::max(0, levels - 2);
    std::vector<double> centers;

    for (int index = 1; index <= interior; ++index)
    {
        centers.push_back(255.0 * index / (interior + 1));
    }

    for (int iteration = 0; iteration < 64 && !centers.empty(); ++iteration)
    {
        std::vector<double> sums(centers.size(), 0.0);
        std::vector<double> weights(centers.size(), 0.0);

        for (std::size_t alpha = 1; alpha < 255; ++alpha)
        {
            if (counts[alpha] <= 0.0)
            {
                continue;
            }

            double best = std::min(static_cast<double>(alpha), 255.0 - static_cast<double>(alpha));
            std::size_t pick = centers.size();

            for (std::size_t index = 0; index < centers.size(); ++index)
            {
                const double distance = std::fabs(static_cast<double>(alpha) - centers[index]);

                if (distance < best)
                {
                    best = distance;
                    pick = index;
                }
            }

            if (pick != centers.size())
            {
                sums[pick] += counts[alpha] * static_cast<double>(alpha);
                weights[pick] += counts[alpha];
            }
        }

        bool changed = false;

        for (std::size_t index = 0; index < centers.size(); ++index)
        {
            if (weights[index] > 0.0 && sums[index] / weights[index] != centers[index])
            {
                centers[index] = sums[index] / weights[index];
                changed = true;
            }
        }

        if (!changed)
        {
            break;
        }
    }

    std::vector<PaletteColor> palette{ { 255, 255, 255, 0 }, { 255, 255, 255, 255 } };

    for (const double center : centers)
    {
        palette.push_back({ 255, 255, 255, SnapAlpha(center) });
    }

    return palette;
}

double HistogramError(const ColorHistogram & histogram, const std::vector<PaletteColor> & palette)
{
    std::vector<Vector4> centers;

    for (const PaletteColor & color : palette)
    {
        centers.push_back(Components(color));
    }

    return PointsError(Samples(histogram), centers);
}

QuantizeStats MapToPalette(const RgbaImage & image, const std::vector<PaletteColor> & palette, std::vector<std::uint8_t> & indices)
{
    std::vector<Vector4> centers;

    for (const PaletteColor & color : palette)
    {
        centers.push_back(Components(color));
    }

    const std::vector<double> weights = NeighborhoodWeights(image);
    std::unordered_map<std::uint64_t, std::uint8_t> cache;
    indices.assign(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height), 0);
    RgbaImage mapped;
    mapped.Allocate(image.width, image.height);

    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x);
            const std::uint8_t * texel = image.At(x, y);
            const PaletteColor color{ texel[0], texel[1], texel[2], texel[3] };
            // Equal colors with equal (quantized) neighborhood weight map identically.
            const std::uint64_t weightKey = static_cast<std::uint64_t>(std::lround(weights[at] * 1023.0));
            const std::uint64_t key = static_cast<std::uint64_t>(PackColor(color)) | (weightKey << 32);
            const auto found = cache.find(key);
            std::uint8_t index = 0;

            if (found != cache.end())
            {
                index = found->second;
            }
            else
            {
                Sample sample;
                sample.value = Components(color);
                sample.count = 1.0;
                sample.rgbWeight = std::max(weights[at], 1e-6);
                double distance = 0.0;
                index = static_cast<std::uint8_t>(Nearest(sample, centers, distance));
                cache.emplace(key, index);
            }

            indices[at] = index;
            const PaletteColor & entry = palette[index];
            std::uint8_t * output = mapped.At(x, y);
            output[0] = entry.r;
            output[1] = entry.g;
            output[2] = entry.b;
            output[3] = entry.a;
        }
    }

    return CompareImages(image, mapped);
}

QuantizeStats CompareImages(const RgbaImage & reference, const RgbaImage & candidate)
{
    QuantizeStats stats;
    double sumSquares = 0.0;
    double over = 0.0;

    for (int y = 0; y < reference.height; ++y)
    {
        for (int x = 0; x < reference.width; ++x)
        {
            const std::uint8_t * first = reference.At(x, y);
            const std::uint8_t * second = candidate.At(x, y);
            int worst = 0;

            for (int channel = 0; channel < 3; ++channel)
            {
                const double difference = first[channel] * first[3] / 255.0 - second[channel] * second[3] / 255.0;
                sumSquares += difference * difference;
                worst = std::max(worst, static_cast<int>(std::lround(std::fabs(difference))));
            }

            const int alpha = std::abs(static_cast<int>(first[3]) - static_cast<int>(second[3]));
            sumSquares += static_cast<double>(alpha) * alpha;
            worst = std::max(worst, alpha);
            stats.maxError = std::max(stats.maxError, worst);
            over += worst > 8 ? 1.0 : 0.0;
            ++stats.texels;
        }
    }

    if (stats.texels > 0)
    {
        stats.rmse = std::sqrt(sumSquares / (4.0 * static_cast<double>(stats.texels)));
        stats.over8 = over / static_cast<double>(stats.texels);
    }

    return stats;
}

std::vector<int> GroupPalettes(const std::vector<const ColorHistogram *> & images, const GroupPolicy & policy)
{
    const std::size_t count = images.size();
    std::vector<PointSet> summaries(count);
    std::vector<double> limits(count, 0.0);

    for (std::size_t index = 0; index < count; ++index)
    {
        const std::vector<PaletteColor> palette = BuildPalette(*images[index], policy.colors);
        summaries[index] = Summarize(*images[index], palette);
        limits[index] = std::max(policy.maxRmse, Rmse(summaries[index].intraSse, summaries[index].texels) + policy.maxIncrease);
    }

    // Each live group keeps its members and a compact palette summary for cost ordering.
    struct Group
    {
        std::vector<std::size_t> members;
        std::vector<Sample> points;
        bool live = true;
    };

    std::vector<Group> groups(count);

    for (std::size_t index = 0; index < count; ++index)
    {
        groups[index].members = { index };
        groups[index].points = summaries[index].points;
    }

    struct Candidate
    {
        double cost = 0.0;
        std::size_t first = 0;
        std::size_t second = 0;
    };

    std::vector<Candidate> candidates;

    const auto AddCandidates = [&](std::size_t group)
    {
        for (std::size_t other = 0; other < groups.size(); ++other)
        {
            if (other != group && groups[other].live)
            {
                candidates.push_back({ ChamferCost(groups[group].points, groups[other].points), std::min(group, other),
                                       std::max(group, other) });
            }
        }
    };

    for (std::size_t group = 0; group < count; ++group)
    {
        for (std::size_t other = group + 1; other < count; ++other)
        {
            candidates.push_back({ ChamferCost(groups[group].points, groups[other].points), group, other });
        }
    }

    while (!candidates.empty())
    {
        const auto cheapest = std::min_element(candidates.begin(), candidates.end(), [](const Candidate & first, const Candidate & second)
                                               {
                                                   return first.cost < second.cost;
                                               });
        const Candidate candidate = *cheapest;
        candidates.erase(cheapest);

        if (!groups[candidate.first].live || !groups[candidate.second].live)
        {
            continue;
        }

        std::vector<std::size_t> members = groups[candidate.first].members;
        members.insert(members.end(), groups[candidate.second].members.begin(), groups[candidate.second].members.end());
        std::vector<Sample> points;

        for (const std::size_t member : members)
        {
            points.insert(points.end(), summaries[member].points.begin(), summaries[member].points.end());
        }

        const std::vector<PaletteColor> merged = PaletteFromPoints(points, policy.colors);
        std::vector<Vector4> centers;

        for (const PaletteColor & color : merged)
        {
            centers.push_back(Components(color));
        }

        bool accepted = true;

        for (const std::size_t member : members)
        {
            const double sse = summaries[member].intraSse + PointsError(summaries[member].points, centers);
            accepted = accepted && Rmse(sse, summaries[member].texels) <= limits[member] &&
                       (policy.maxError <= 0 || PointsMaxShift(summaries[member].points, centers) <= policy.maxError);
        }

        if (!accepted)
        {
            continue;
        }

        groups[candidate.first].live = false;
        groups[candidate.second].live = false;
        Group combined;
        combined.members = std::move(members);
        // Later costs compare the merged palette, weighted by everything it now serves.
        std::vector<Sample> summary(merged.size());

        for (std::size_t index = 0; index < merged.size(); ++index)
        {
            summary[index].value = centers[index];
        }

        for (const Sample & point : points)
        {
            double distance = 0.0;
            const std::size_t index = Nearest(point, centers, distance);
            summary[index].count += point.count;
            summary[index].rgbWeight += point.rgbWeight;
        }

        for (const Sample & point : summary)
        {
            if (point.count > 0.0)
            {
                combined.points.push_back(point);
            }
        }

        groups.push_back(std::move(combined));
        AddCandidates(groups.size() - 1);
    }

    std::vector<int> assignment(count, -1);
    int next = 0;

    for (std::size_t image = 0; image < count; ++image)
    {
        if (assignment[image] >= 0)
        {
            continue;
        }

        for (const Group & group : groups)
        {
            if (group.live && std::find(group.members.begin(), group.members.end(), image) != group.members.end())
            {
                for (const std::size_t member : group.members)
                {
                    assignment[member] = next;
                }

                ++next;
                break;
            }
        }
    }

    return assignment;
}

} // namespace assets
