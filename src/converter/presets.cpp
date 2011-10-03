#include "presets.h"
#include "ffmpeginterface.h"
#include <QMultiMap>
#include <QXmlStreamReader>
#include <QFile>
#include <QDebug>

struct Presets::Private
{
    QMultiMap<QString, Preset> presets; ///< map extension to presets

    /*! @brief Store the previously assigned id
     *  Each preset is assigned a unique id to indicate the order it is read.
     *  This id is later used in sorting presets.
     *  When a new preset is read from the file, increment m_prev_id and assign
     *  it to the new preset, i.e. preset.id = ++m_prev_id;
     */
    unsigned int m_prev_id;

    Private() : m_prev_id(0) { }

    bool parseXmlFile(QFile& file);
    bool parsePreset(QXmlStreamReader& xml);
    bool readElementData(QXmlStreamReader& xml, Preset& target);
    void removeUnavailablePresets();
};

bool Presets::Private::parseXmlFile(QFile &file)
{
    Q_ASSERT_X(file.isOpen(), "parse xml file", "file is not opened");
    QXmlStreamReader xml(&file);

    presets.clear();

    int level = 0; // depth of current tag
    QString top_level_tag; // name of the tag at level 0
    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartDocument) {
            continue;   // skip StartDocument token
        } else if (token == QXmlStreamReader::StartElement) {

            if (level == 0) {
                top_level_tag = xml.name().toString();
                ++level;
            } else if (level == 1 && top_level_tag == "presets") {
                if (!parsePreset(xml))
                    return false;
            }

        } else if (token == QXmlStreamReader::EndElement) {
            --level;
        }
    }

    if (xml.hasError()) {
        return false;
    }

    removeUnavailablePresets();

    return true;
}

bool Presets::Private::parsePreset(QXmlStreamReader &xml)
{
    if (xml.tokenType() != QXmlStreamReader::StartElement)
        return false;

    QString tag_name = xml.name().toString();
    Preset preset;

    xml.readNext();

    while (xml.tokenType() != QXmlStreamReader::EndElement
           || xml.name() != tag_name) {
        if (xml.tokenType() == QXmlStreamReader::StartElement) {
            if (!readElementData(xml, preset)) return false;
        }
        xml.readNext();
    }

    // Add preset to the multimap. Use extension as the key.
    if (!preset.extension.isEmpty()) {
        preset.id = ++m_prev_id; // assign an id to preset
        presets.insert(preset.extension, preset);
    }

    return true;
}

bool Presets::Private::readElementData(QXmlStreamReader &xml, Preset& target)
{
    if (xml.tokenType() != QXmlStreamReader::StartElement)
        return false;
    QString property_name = xml.name().toString();

    xml.readNext();

    if (xml.tokenType() != QXmlStreamReader::Characters)
        return true; // empty content

    QString property_value = xml.text().toString().trimmed();

    if (property_name == "label") {
        target.label = property_value;
    } else if (property_name == "params") {
        target.parameters = property_value;
    } else if (property_name == "extension") {
        target.extension = property_value;
    } else if (property_name == "category") {
        target.category = property_value;
    }

    return true;
}

void Presets::Private::removeUnavailablePresets()
{
    QSet<QString> audio_encoders, video_encoders, subtitle_encoders;

    if (!FFmpegInterface::getAudioEncoders(audio_encoders))
        Q_ASSERT(audio_encoders.isEmpty());
    if (!FFmpegInterface::getVideoEncoders(video_encoders))
        Q_ASSERT(video_encoders.isEmpty());
    if (!FFmpegInterface::getSubtitleEncoders(subtitle_encoders))
        Q_ASSERT(subtitle_encoders.isEmpty());

    QRegExp audio_codec_pattern("-acodec\\s+([^ ]+)");
    QRegExp video_codec_pattern("-vcodec\\s+([^ ]+)");
    QRegExp subtitle_codec_pattern("-scodec\\s+([^ ]+)");

    QMultiMap<QString, Preset>::iterator it = presets.begin();
    while (it!=presets.end()) {
        bool remove = false;
        QString& params = it.value().parameters;

        // Check unavailable audio presets
        if (audio_codec_pattern.indexIn(params) != -1) {
            if (!audio_encoders.contains(audio_codec_pattern.cap(1))) {
                remove = true;
            }
        }

        // Check unavailable video presets
        if (!remove && video_codec_pattern.indexIn(params) != -1) {
            if (!video_encoders.contains(video_codec_pattern.cap(1))) {
                remove = true;
            }
        }

        // Check unavailable subtitle presets
        if (!remove && subtitle_codec_pattern.indexIn(params) != -1) {
            if (!subtitle_encoders.contains(subtitle_codec_pattern.cap(1))) {
                remove = true;
            }
        }

        if (remove) // remove the preset if any of the codecs are unavailable
            presets.erase(it++);
        else
            ++it;
    }
}

Presets::Presets(QObject *parent) :
    QObject(parent), p(new Private)
{
}

Presets::~Presets()
{
}

bool Presets::readFromFile(const QString &filename)
{
    QFile xmlfile(filename);
    if (!xmlfile.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    qDebug() << "Read XML preset file " + filename;

    bool ret = p->parseXmlFile(xmlfile);

    xmlfile.close();
    return ret;
}

bool Presets::readFromFile(const char *filename)
{
    return readFromFile(QString(filename));
}

bool Presets::getExtensions(QList<QString> &target) const
{
    QList<Preset> presetList = p->presets.values();
    qSort(presetList); // sort presets by id

    target.clear();
    QString extension("");
    foreach (Preset preset, presetList) {
        if (extension != preset.extension) { // new extension appears
            extension = preset.extension;
            target.push_back(extension);
        }
    }
    return true;
}

bool Presets::findPresetById(unsigned int id, Preset &target) const
{
    QMultiMap<QString, Preset>::iterator it = p->presets.begin();
    for (; it!=p->presets.end(); ++it) {
        if (it->id == id) {
            target = *it;
            return true;
        }
    }
    return false;
}

bool Presets::getPresets(QList<Preset> &target)
{
    target.clear();
    target = p->presets.values();
    return true;
}

bool Presets::getPresets(const QString &extension, QList<Preset> &target)
{
    target = p->presets.values(extension);
    return true;
}

bool Presets::getPresets(const char *extension, QList<Preset> &target)
{
    return getPresets(QString(extension), target);
}