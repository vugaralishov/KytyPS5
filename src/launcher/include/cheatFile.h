#ifndef CHEAT_FILE_H
#define CHEAT_FILE_H

#include <QByteArray>
#include <QJsonDocument>
#include <QList>
#include <QString>

namespace Cheats {

[[nodiscard]] bool    IsSupportedTitleId(const QString& title_id);
// Cheat file to read (and pass to the emulator): the per-user file, or one saved next to the
// launcher by an older version.
[[nodiscard]] QString PlanPath(const QString& title_id);
// Per-user location where cheat files are saved.
[[nodiscard]] QString WritablePlanPath(const QString& title_id);

// Local files retain their existing defaults; remote imports receive additional validation.
[[nodiscard]] QString       ParseDocument(const QByteArray& data, QJsonDocument* document);
[[nodiscard]] QString       ValidateImport(const QJsonDocument& document, const QString& title_id,
                                           const QString& version, const QString& process);
[[nodiscard]] QJsonDocument WithSelection(const QJsonDocument& document,
                                          const QList<bool>&   enabled);
// Refuse to replace a file changed since it was loaded; empty data also permits creation.
[[nodiscard]] QString SaveDocument(const QString& path, const QJsonDocument& document,
                                   const QByteArray& expected_data);
// SaveDocument() for a title's cheat file; moves a legacy file to the per-user location.
[[nodiscard]] QString SavePlan(const QString& title_id, const QJsonDocument& document,
                               const QByteArray& expected_data);

} // namespace Cheats

#endif // CHEAT_FILE_H
