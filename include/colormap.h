#pragma once

#include <QColor>
#include <QString>
#include <vector>
#include <string>

struct ColorStop
{
    float  position = 0.0f;
    QColor color;
};

enum class ColorMapType
{
    Default,
    Heat,
    Cool,
    Grayscale,
    Viridis,
    Green,    // Req 3: black -> green only, R=0 B=0 G=0..255
    Custom
};

class ColorMap
{
public:
    ColorMap();
    explicit ColorMap(ColorMapType type);
    ColorMap(std::vector<ColorStop> stops, std::string name = "Custom");

    QColor sample(float t) const;
    QColor sampleValue(int value, int valueMax = 255) const;

    ColorMapType type() const { return m_type; }
    const std::string& name() const { return m_name; }
    const std::vector<ColorStop>& stops() const { return m_stops; }

    static ColorMap builtin(ColorMapType type);
    static ColorMap fromStops(std::vector<ColorStop> stops, std::string name = "Custom");

    static bool loadFromJsonFile(const QString& path, ColorMap& outMap, std::string* errorOut = nullptr);
    static bool loadFromJson(const QByteArray& jsonBytes, ColorMap& outMap, std::string* errorOut = nullptr);

    static QString typeToString(ColorMapType type);
    static ColorMapType typeFromString(const QString& name);

private:
    void sortStops();

    ColorMapType           m_type = ColorMapType::Default;
    std::string            m_name = "Default";
    std::vector<ColorStop> m_stops;
};
