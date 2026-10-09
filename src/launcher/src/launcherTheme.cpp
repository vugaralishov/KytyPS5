#include "launcherTheme.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFile>
#include <QObject>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>
#include <QWidget>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

#if defined(_WIN32)
// Native title bars otherwise differ between windows (Windows picks the color per window), so
// every launcher window gets the same caption, caption text and border colors. Windows 10 and
// older Windows 11 builds ignore the attributes they do not know.
void ApplyWindowFrameColors(QWidget* window) {
	using SetAttribute         = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
	static const auto set_attr = []() -> SetAttribute {
		const auto module = LoadLibraryW(L"dwmapi.dll");
		return module != nullptr
		           ? reinterpret_cast<SetAttribute>(GetProcAddress(module, "DwmSetWindowAttribute"))
		           : nullptr;
	}();
	if (set_attr == nullptr) {
		return;
	}

	constexpr DWORD USE_IMMERSIVE_DARK_MODE = 20;
	constexpr DWORD BORDER_COLOR            = 34;
	constexpr DWORD CAPTION_COLOR           = 35;
	constexpr DWORD TEXT_COLOR              = 36;
	const auto      to_ref                  = [](const QColor& c) {
		return static_cast<COLORREF>(RGB(c.red(), c.green(), c.blue()));
	};

	const auto     hwnd    = reinterpret_cast<HWND>(window->winId());
	const BOOL     dark    = TRUE;
	const COLORREF caption = to_ref(QColor(0x1e, 0x20, 0x26));
	const COLORREF text    = to_ref(window->palette().color(QPalette::WindowText));
	const COLORREF border  = to_ref(window->palette().color(QPalette::Mid));
	set_attr(hwnd, USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
	set_attr(hwnd, CAPTION_COLOR, &caption, sizeof(caption));
	set_attr(hwnd, TEXT_COLOR, &text, sizeof(text));
	set_attr(hwnd, BORDER_COLOR, &border, sizeof(border));
}

class WindowFrameFilter final: public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (event->type() == QEvent::Show) {
			if (auto* widget = qobject_cast<QWidget*>(watched);
			    widget != nullptr && widget->isWindow() &&
			    !(widget->windowFlags() & Qt::FramelessWindowHint) &&
			    (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog)) {
				ApplyWindowFrameColors(widget);
			}
		}
		return QObject::eventFilter(watched, event);
	}
};
#endif

class PaletteChangeFilter final: public QObject {
public:
	explicit PaletteChangeFilter(QApplication& application)
	    : QObject(&application), m_application(application) {}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (event->type() == QEvent::ApplicationPaletteChange && !m_refresh_pending) {
			m_refresh_pending = true;
			QTimer::singleShot(0, &m_application, [this] {
				m_application.setStyleSheet(m_application.styleSheet());
				m_refresh_pending = false;
			});
		}

		return QObject::eventFilter(watched, event);
	}

private:
	QApplication& m_application;
	bool          m_refresh_pending = false;
};

// Dark palette shared by every widget; the style sheet refers to these roles through palette().
QPalette DarkPalette() {
	const QColor window(0x11, 0x13, 0x18);
	const QColor surface(0x18, 0x1b, 0x22);
	const QColor raised(0x22, 0x26, 0x30);
	const QColor border(0x2b, 0x30, 0x3b);
	const QColor hover(0x2c, 0x31, 0x40);
	const QColor text(0xe7, 0xe9, 0xee);
	const QColor muted(0x8a, 0x92, 0xa3);
	const QColor disabled(0x5c, 0x63, 0x72);
	const QColor accent(0x3d, 0x7e, 0xff);

	QPalette palette;
	palette.setColor(QPalette::Window, window);
	palette.setColor(QPalette::WindowText, text);
	palette.setColor(QPalette::Base, surface);
	palette.setColor(QPalette::AlternateBase, raised);
	palette.setColor(QPalette::Text, text);
	palette.setColor(QPalette::PlaceholderText, muted);
	palette.setColor(QPalette::Button, raised);
	palette.setColor(QPalette::ButtonText, text);
	palette.setColor(QPalette::BrightText, Qt::white);
	palette.setColor(QPalette::Light, hover);
	palette.setColor(QPalette::Midlight, hover);
	palette.setColor(QPalette::Mid, border);
	palette.setColor(QPalette::Dark, window.darker(130));
	palette.setColor(QPalette::Shadow, Qt::black);
	palette.setColor(QPalette::Highlight, accent);
	palette.setColor(QPalette::HighlightedText, Qt::white);
	palette.setColor(QPalette::Link, accent.lighter(130));
	palette.setColor(QPalette::LinkVisited, accent.lighter(115));
	palette.setColor(QPalette::ToolTipBase, raised);
	palette.setColor(QPalette::ToolTipText, text);
	for (const auto role: {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
		palette.setColor(QPalette::Disabled, role, disabled);
	}
	palette.setColor(QPalette::Disabled, QPalette::Highlight, border);
	return palette;
}

} // namespace

namespace LauncherTheme {

void Initialize(QApplication& application) {
	// Fusion renders style sheets the same way on every platform, unlike the native styles.
	if (auto* fusion = QStyleFactory::create(QStringLiteral("Fusion")); fusion != nullptr) {
		QApplication::setStyle(fusion);
	}
	QApplication::setPalette(DarkPalette());
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	// Also gives native window decorations (the Windows title bar) the dark look.
	QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif

	QFile style_sheet_file(QStringLiteral(":/styles/launcher.qss"));
	if (style_sheet_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		application.setStyleSheet(QString::fromUtf8(style_sheet_file.readAll()));
		application.installEventFilter(new PaletteChangeFilter(application));
	}
#if defined(_WIN32)
	application.installEventFilter(new WindowFrameFilter(&application));
#endif
}
} // namespace LauncherTheme
