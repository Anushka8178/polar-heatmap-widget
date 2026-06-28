#include "colormap.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>

ColorMap::ColorMap() : ColorMap(ColorMapType::Default) {}
ColorMap::ColorMap(ColorMapType type) { *this = builtin(type); }
ColorMap::ColorMap(std::vector<ColorStop> stops, std::string name)
    : m_type(ColorMapType::Custom), m_name(std::move(name)), m_stops(std::move(stops))
{ sortStops(); }

void ColorMap::sortStops()
{
    std::sort(m_stops.begin(), m_stops.end(),
              [](const ColorStop& a, const ColorStop& b){ return a.position < b.position; });
}

ColorMap ColorMap::builtin(ColorMapType type)
{
    ColorMap cm({ {0.0f, Qt::black}, {1.0f, Qt::white} }, "Default");
    cm.m_type = type;

    switch (type)
    {
    case ColorMapType::Green:
            cm.m_name  = "Green";
            cm.m_stops = {
                {0.00f, QColor::fromRgb(8,   24,  16)},
                {0.25f, QColor::fromRgb(20,  70,  45)},
                {0.50f, QColor::fromRgb(45,  140, 90)},
                {0.75f, QColor::fromRgb(110, 200, 140)},
                {1.00f, QColor::fromRgb(180, 240, 190)},
            };
            break;

    case ColorMapType::Heat:
        cm.m_name  = "Heat";
        cm.m_stops = {
            {0.00f, QColor::fromRgbF(0.00, 0.00, 0.00)},
            {0.35f, QColor::fromRgbF(0.65, 0.00, 0.00)},
            {0.65f, QColor::fromRgbF(1.00, 0.45, 0.00)},
            {0.85f, QColor::fromRgbF(1.00, 0.90, 0.10)},
            {1.00f, QColor::fromRgbF(1.00, 1.00, 1.00)},
        };
        break;

    case ColorMapType::Cool:
        cm.m_name  = "Cool";
        cm.m_stops = {
            {0.00f, QColor::fromRgbF(0.00, 1.00, 1.00)},
            {0.50f, QColor::fromRgbF(0.50, 0.50, 1.00)},
            {1.00f, QColor::fromRgbF(1.00, 0.00, 1.00)},
        };
        break;

    case ColorMapType::Grayscale:
        cm.m_name  = "Grayscale";
        cm.m_stops = {
            {0.00f, QColor::fromRgbF(0.00, 0.00, 0.00)},
            {1.00f, QColor::fromRgbF(1.00, 1.00, 1.00)},
        };
        break;

    case ColorMapType::Viridis:
        cm.m_name  = "Viridis";
        cm.m_stops = {
            {0.00f, QColor::fromRgbF(0.267, 0.005, 0.329)},
            {0.25f, QColor::fromRgbF(0.230, 0.322, 0.546)},
            {0.50f, QColor::fromRgbF(0.128, 0.567, 0.551)},
            {0.75f, QColor::fromRgbF(0.369, 0.789, 0.383)},
            {1.00f, QColor::fromRgbF(0.993, 0.906, 0.144)},
        };
        break;



    case ColorMapType::Custom:
    default:
        cm.m_name  = "Custom";
        cm.m_stops = { {0.0f, Qt::black}, {1.0f, Qt::white} };
        break;
    }
    cm.sortStops();
    return cm;
}

ColorMap ColorMap::fromStops(std::vector<ColorStop> stops, std::string name)
{
    return ColorMap(std::move(stops), std::move(name));
}

QColor ColorMap::sample(float t) const
{
    if (m_stops.empty()) return Qt::black;
    t = std::max(0.0f, std::min(1.0f, t));
    if (m_stops.size() == 1) return m_stops.front().color;
    if (t <= m_stops.front().position) return m_stops.front().color;
    if (t >= m_stops.back().position)  return m_stops.back().color;

    for (size_t i = 0; i + 1 < m_stops.size(); ++i)
    {
        const ColorStop& a = m_stops[i];
        const ColorStop& b = m_stops[i + 1];
        if (t >= a.position && t <= b.position)
        {
            float span = b.position - a.position;
            float u    = (span > 1e-6f) ? (t - a.position) / span : 0.0f;
            float r  = float(a.color.redF())   + (float(b.color.redF())   - float(a.color.redF()))   * u;
            float g  = float(a.color.greenF()) + (float(b.color.greenF()) - float(a.color.greenF())) * u;
            float bl = float(a.color.blueF())  + (float(b.color.blueF())  - float(a.color.blueF()))  * u;
            float al = float(a.color.alphaF()) + (float(b.color.alphaF()) - float(a.color.alphaF())) * u;
            return QColor::fromRgbF(double(r), double(g), double(bl), double(al));
        }
    }
    return m_stops.back().color;
}

QColor ColorMap::sampleValue(int value, int valueMax) const
{
    valueMax = std::max(1, valueMax);
    return sample(float(std::max(0, std::min(valueMax, value))) / float(valueMax));
}

QString ColorMap::typeToString(ColorMapType type)
{
    switch (type)
    {
    case ColorMapType::Default:   return "Default";
    case ColorMapType::Heat:      return "Heat";
    case ColorMapType::Cool:      return "Cool";
    case ColorMapType::Grayscale: return "Grayscale";
    case ColorMapType::Viridis:   return "Viridis";
    case ColorMapType::Green:     return "Green";
    case ColorMapType::Custom:    return "Custom";
    }
    return "Default";
}

ColorMapType ColorMap::typeFromString(const QString& name)
{
    const QString n = name.trimmed().toLower();
    if (n == "heat")                          return ColorMapType::Heat;
    if (n == "cool")                          return ColorMapType::Cool;
    if (n == "grayscale" || n == "greyscale") return ColorMapType::Grayscale;
    if (n == "viridis")                       return ColorMapType::Viridis;
    if (n == "green")                         return ColorMapType::Green;
    if (n == "custom")                        return ColorMapType::Custom;
    return ColorMapType::Default;
}

bool ColorMap::loadFromJson(const QByteArray& jsonBytes, ColorMap& outMap, std::string* errorOut)
{
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &perr);
    if (perr.error != QJsonParseError::NoError)
    { if (errorOut) *errorOut = "JSON parse error: " + perr.errorString().toStdString(); return false; }
    if (!doc.isObject())
    { if (errorOut) *errorOut = "Root must be object."; return false; }

    QJsonObject root = doc.object();
    std::string name = root.value("name").toString("Custom").toStdString();
    if (!root.contains("stops") || !root.value("stops").isArray())
    { if (errorOut) *errorOut = "Missing 'stops' array."; return false; }

    std::vector<ColorStop> stops;
    for (const QJsonValue& v : root.value("stops").toArray())
    {
        if (!v.isObject()) continue;
        QJsonObject so = v.toObject();
        ColorStop cs;
        cs.position = float(so.value("position").toDouble(0.0));
        cs.color = QColor(std::clamp(so.value("r").toInt(0),   0, 255),
                          std::clamp(so.value("g").toInt(0),   0, 255),
                          std::clamp(so.value("b").toInt(0),   0, 255),
                          std::clamp(so.value("a").toInt(255), 0, 255));
        stops.push_back(cs);
    }
    if (stops.size() < 2)
    { if (errorOut) *errorOut = "Need at least 2 stops."; return false; }

    outMap = ColorMap::fromStops(stops, name);
    if (errorOut) errorOut->clear();
    return true;
}

bool ColorMap::loadFromJsonFile(const QString& path, ColorMap& outMap, std::string* errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    { if (errorOut) *errorOut = "Cannot open: " + path.toStdString(); return false; }
    return loadFromJson(f.readAll(), outMap, errorOut);
}
