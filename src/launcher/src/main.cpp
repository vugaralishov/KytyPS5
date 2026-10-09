#include "mainDialog.h"
#include "launcherTheme.h"

#include <QApplication>

int main(int argc, char* argv[]) {
	QApplication a(argc, argv);
	// Per-user data (cheats, Sunshine apps file) goes to an AppData folder named after the app,
	// not after the executable.
	QCoreApplication::setApplicationName(QStringLiteral("KytyPS5"));
	LauncherTheme::Initialize(a);

	MainDialog w;

	w.emit Start();

	w.show();

	return QApplication::exec();
}
