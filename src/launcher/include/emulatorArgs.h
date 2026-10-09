#ifndef EMULATOR_ARGS_H
#define EMULATOR_ARGS_H

#include <QString>
#include <QStringList>

class Configuration;

namespace EmulatorArgs {

// The kyty_emulator executable next to the launcher, or in its parent directory. Returns the
// parent directory candidate when neither exists.
[[nodiscard]] QString FindEmulator();

// Command line arguments that start the game described by info; empty when the configuration is
// invalid.
[[nodiscard]] QStringList Create(const Configuration& info);

} // namespace EmulatorArgs

#endif // EMULATOR_ARGS_H
