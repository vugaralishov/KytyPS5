#include "mandatoryLineEdit.h"

#include <QColor>
#include <QLine>
#include <QPainter>
#include <QPen>
#include <QStackedWidget>
#include <QVariant>
#include <QtCore>

class QPaintEvent;
class QWidget;

MandatoryLineEdit::MandatoryLineEdit(QWidget* parent): QLineEdit(parent) {
	this->setProperty(MANDATORY_AND_EMPTY, true);

	connect(this, &QLineEdit::textChanged, this, &MandatoryLineEdit::changed);
}

void MandatoryLineEdit::paintEvent(QPaintEvent* event) {
	QLineEdit::paintEvent(event);

	if (this->property(MANDATORY_AND_EMPTY).toBool() && this->isEnabled()) {
		QPainter painter(this);
		QLine    line(4, this->height() - 4, this->width() - 4, this->height() - 4);
		painter.setPen(QPen(QColor(255, 0, 0), 1.0, Qt::DotLine));
		painter.drawLine(line);
	}
}

// A field on an inactive page of a QStackedWidget is hidden but still part of the form.
static bool IsPartOfForm(const QWidget* widget, const QObject* root) {
	for (const auto* current = widget; current != nullptr && current != root;
	     current             = current->parentWidget()) {
		if (current->isHidden() &&
		    qobject_cast<const QStackedWidget*>(current->parentWidget()) == nullptr) {
			return false;
		}
	}
	return true;
}

QLineEdit* MandatoryLineEdit::FirstEmpty(QObject* p) {
	const QList<QLineEdit*>& ch_list = p->findChildren<QLineEdit*>();

	foreach (QLineEdit* ch, ch_list) {
		if (IsPartOfForm(ch, p) && ch->property(MANDATORY_AND_EMPTY).toBool() && ch->isEnabled()) {
			return ch;
		}
	}

	return nullptr;
}

bool MandatoryLineEdit::FindEmpty(QObject* p) {
	return FirstEmpty(p) != nullptr;
}

void MandatoryLineEdit::changed(const QString& text) {
	this->setProperty(MANDATORY_AND_EMPTY, text.isEmpty());
}
