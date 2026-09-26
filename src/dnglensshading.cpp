/*
 * SPDX-FileCopyrightText: 2026 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dnglensshading.h"

#include <QDataStream>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace {

// DNG opcode id for GainMap (DNG spec 1.4+, "Opcode List Overview" table).
const quint32 OpcodeIdGainMap = 9;
// Encoded as MajorMajorMinorMinor bytes, e.g. 1.3.0.0 -> 0x01030000. GainMap
// was introduced in DNG 1.3, so opcodes are tagged with that spec version.
const quint32 DngSpecVersion_1_3_0_0 = 0x01030000;
// Bit 0 of OpcodeFlags: readers that don't understand this opcode may skip
// it and still produce a usable (uncorrected) image, rather than rejecting
// the file outright.
const quint32 OpcodeFlagOptional = 1;

// Maps each of the four CFA phases, visited in (row, col) =
// (0,0),(0,1),(1,0),(1,1) order, to the plane name used in the calibration
// JSON. Must be kept in sync with cfaPatternBytes() in
// declarativecameraextensions.cpp and with cfa_phase_names() in
// tools/calibration/generate_lens_shading.py.
QStringList cfaPhaseNames(const QString &cfa)
{
    if (cfa == QLatin1String("RGGB")) {
        return { QStringLiteral("R"), QStringLiteral("Gr"), QStringLiteral("Gb"), QStringLiteral("B") };
    } else if (cfa == QLatin1String("GRBG")) {
        return { QStringLiteral("Gr"), QStringLiteral("R"), QStringLiteral("B"), QStringLiteral("Gb") };
    } else if (cfa == QLatin1String("GBRG")) {
        return { QStringLiteral("Gb"), QStringLiteral("B"), QStringLiteral("R"), QStringLiteral("Gr") };
    } else if (cfa == QLatin1String("BGGR")) {
        return { QStringLiteral("B"), QStringLiteral("Gb"), QStringLiteral("Gr"), QStringLiteral("R") };
    }
    return {};
}

void writeGainMapOpcode(QDataStream &stream, quint32 top, quint32 left,
                        quint32 bottom, quint32 right, quint32 mapPointsV,
                        quint32 mapPointsH, double mapSpacingV, double mapSpacingH,
                        const QVector<float> &mapGains)
{
    const quint32 parameterSize =
            10 * sizeof(quint32) + 4 * sizeof(double) + sizeof(quint32)
            + quint32(mapGains.size()) * sizeof(float);

    stream << OpcodeIdGainMap;
    stream << DngSpecVersion_1_3_0_0;
    stream << OpcodeFlagOptional;
    stream << parameterSize;

    stream << top << left << bottom << right;
    stream << quint32(0) /* Plane */ << quint32(1) /* Planes */;
    stream << quint32(2) /* RowPitch */ << quint32(2) /* ColPitch */;
    stream << mapPointsV << mapPointsH;
    stream << mapSpacingV << mapSpacingH;
    stream << double(0.0) /* MapOriginV */ << double(0.0) /* MapOriginH */;
    stream << quint32(1) /* MapPlanes */;
    for (float gain : mapGains) {
        stream << gain;
    }
}

}

namespace DngLensShading {

QByteArray buildOpcodeList1(const QString &calibrationDir, const QString &cameraId,
                            const QString &cfaPattern, int width, int height,
                            QString *warning)
{
    const QString path = calibrationDir + QLatin1String("/lens_shading_camera")
            + cameraId + QLatin1String(".json");
    QFile file(path);
    if (!file.exists()) {
        // No calibration for this camera: not an error, just nothing to add.
        return QByteArray();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *warning = QStringLiteral("Cannot read lens shading calibration: %1").arg(path);
        return QByteArray();
    }

    QJsonParseError parseError;
    const QJsonObject calibration =
            QJsonDocument::fromJson(file.readAll(), &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        *warning = QStringLiteral("Lens shading calibration is not valid JSON: %1").arg(path);
        return QByteArray();
    }

    if (calibration.value(QStringLiteral("version")).toInt() != 1) {
        *warning = QStringLiteral("Unsupported lens shading calibration version: %1").arg(path);
        return QByteArray();
    }
    if (calibration.value(QStringLiteral("cfa_pattern")).toString() != cfaPattern) {
        *warning = QStringLiteral("Lens shading calibration CFA pattern does not match capture: %1").arg(path);
        return QByteArray();
    }
    if (calibration.value(QStringLiteral("image_width")).toInt() != width
            || calibration.value(QStringLiteral("image_height")).toInt() != height) {
        // Most likely a calibration captured at a different resolution/binning
        // mode than the current capture: applying it would misalign the grid.
        *warning = QStringLiteral("Lens shading calibration resolution does not match capture: %1").arg(path);
        return QByteArray();
    }

    const int gridRows = calibration.value(QStringLiteral("grid_rows")).toInt();
    const int gridCols = calibration.value(QStringLiteral("grid_cols")).toInt();
    if (gridRows < 2 || gridCols < 2) {
        *warning = QStringLiteral("Lens shading calibration grid is too small: %1").arg(path);
        return QByteArray();
    }

    const QStringList phaseNames = cfaPhaseNames(cfaPattern);
    if (phaseNames.size() != 4) {
        *warning = QStringLiteral("Unsupported CFA pattern for lens shading: %1").arg(cfaPattern);
        return QByteArray();
    }

    const QJsonObject planes = calibration.value(QStringLiteral("planes")).toObject();
    QHash<QString, QVector<float>> planeGains;
    for (const QString &name : phaseNames) {
        const QJsonArray values = planes.value(name).toArray();
        if (values.size() != gridRows * gridCols) {
            *warning = QStringLiteral("Lens shading calibration plane \"%1\" has the wrong size: %2")
                    .arg(name, path);
            return QByteArray();
        }
        QVector<float> gains;
        gains.reserve(values.size());
        for (const QJsonValue &value : values) {
            gains.append(float(value.toDouble(1.0)));
        }
        planeGains.insert(name, gains);
    }

    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    // DNG opcode list data is always big-endian, regardless of the TIFF
    // file's own byte order (DNG spec, "Opcode List" chapter).
    stream.setByteOrder(QDataStream::BigEndian);

    stream << quint32(4); // opcode count: one GainMap per CFA phase

    for (int phase = 0; phase < 4; ++phase) {
        const int top = phase / 2;
        const int left = phase % 2;
        const int planeHeight = (height - top + 1) / 2;
        const int planeWidth = (width - left + 1) / 2;
        const double spacingV = planeHeight > 1
                ? double(planeHeight - 1) / double(gridRows - 1) : 0.0;
        const double spacingH = planeWidth > 1
                ? double(planeWidth - 1) / double(gridCols - 1) : 0.0;

        writeGainMapOpcode(stream, quint32(top), quint32(left),
                           quint32(height - 1), quint32(width - 1),
                           quint32(gridRows), quint32(gridCols),
                           spacingV, spacingH,
                           planeGains.value(phaseNames.at(phase)));
    }

    return buffer;
}

}
