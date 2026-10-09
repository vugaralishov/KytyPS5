#include "remoteGamesDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

RemoteGamesDialog::RemoteGamesDialog(const RemotePlay::Settings& settings, QWidget* parent)
    : QDialog(parent, Qt::WindowCloseButtonHint), m_settings(settings) {
	setWindowTitle(tr("Home PC"));
	setMinimumSize(520, 440);
	resize(600, 520);
	setSizeGripEnabled(true);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 12);
	layout->setSpacing(10);

	auto* title = new QLabel(m_settings.host.isEmpty() ? tr("Home PC") : m_settings.host, this);
	title->setObjectName(QStringLiteral("section_title"));
	layout->addWidget(title);

	m_hint = new QLabel(this);
	m_hint->setObjectName(QStringLiteral("section_hint"));
	m_hint->setWordWrap(true);
	layout->addWidget(m_hint);

	m_list = new QListWidget(this);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setToolTip(tr("Games shared by your home PC. Double-click a game to play it."));
	layout->addWidget(m_list, 1);

	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("remote_play_status"));
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_status);

	auto* button_layout = new QHBoxLayout;
	button_layout->setSpacing(10);
	m_pair_button = new QPushButton(tr("Pair..."), this);
	m_pair_button->setToolTip(tr("Pair this device with your home PC. This is needed once."));
	m_pair_button->setAutoDefault(false);
	button_layout->addWidget(m_pair_button);
	m_settings_button = new QPushButton(tr("Settings..."), this);
	m_settings_button->setToolTip(tr("Open Settings > Remote play."));
	m_settings_button->setAutoDefault(false);
	button_layout->addWidget(m_settings_button);
	button_layout->addStretch(1);

	auto* buttons    = new QDialogButtonBox(this);
	m_refresh_button = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
	m_refresh_button->setToolTip(tr("Ask your home PC for its games again."));
	m_refresh_button->setAutoDefault(false);
	m_play_button = buttons->addButton(tr("Play"), QDialogButtonBox::ActionRole);
	m_play_button->setToolTip(tr("Stream the selected game with Moonlight."));
	m_play_button->setDefault(true);
	auto* close_button = buttons->addButton(QDialogButtonBox::Close);
	close_button->setToolTip(tr("Close this window. Games that are streaming keep running."));
	close_button->setAutoDefault(false);
	button_layout->addWidget(buttons);
	layout->addLayout(button_layout);

	connect(m_refresh_button, &QPushButton::clicked, this, &RemoteGamesDialog::Refresh);
	connect(m_play_button, &QPushButton::clicked, this, &RemoteGamesDialog::Play);
	connect(m_pair_button, &QPushButton::clicked, this, &RemoteGamesDialog::Pair);
	connect(m_settings_button, &QPushButton::clicked, this, [this]() {
		m_open_settings = true;
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_list, &QListWidget::itemDoubleClicked, this, &RemoteGamesDialog::Play);
	connect(m_list, &QListWidget::itemSelectionChanged, this, &RemoteGamesDialog::UpdateButtons);

	if (m_settings.host.isEmpty()) {
		m_hint->setText(tr("Play the games on your home PC from this device with Moonlight."));
		m_list->setVisible(false);
		// Without the list, keep the text at the top and the window small.
		layout->insertStretch(layout->indexOf(m_status) + 1, 1);
		setMinimumSize(520, 0);
		resize(600, 300);
		ShowStatus(tr("No home PC is set up yet. Open Settings > Remote play and enter your home "
		              "PC's address, for example its Tailscale name."));
		m_settings_button->setDefault(true);
		UpdateButtons();
		return;
	}

	m_hint->setText(tr("Games that %1 shares through Sunshine. Double-click a game to play it in "
	                   "Moonlight.")
	                    .arg(m_settings.host));
	Refresh();
}

void RemoteGamesDialog::ShowStatus(const QString& text) {
	m_status->setText(text);
	m_status->setVisible(!text.isEmpty());
}

void RemoteGamesDialog::UpdateButtons() {
	const bool has_host = !m_settings.host.isEmpty();
	m_pair_button->setEnabled(has_host);
	m_refresh_button->setEnabled(has_host && !m_loading);
	m_play_button->setEnabled(has_host && !m_list->selectedItems().isEmpty());
}

void RemoteGamesDialog::Refresh() {
	const int request = ++m_request;
	m_loading         = true;
	m_list->clear();
	ShowStatus(tr("Loading games from %1...").arg(m_settings.host));
	UpdateButtons();

	RemotePlay::ListApps(this, m_settings.moonlight, m_settings.host,
	                     [this, request](const RemotePlay::AppList& list) {
		                     if (request != m_request) {
			                     return;
		                     }
		                     m_loading = false;
		                     m_list->clear();
		                     m_list->addItems(list.apps);
		                     if (m_list->count() > 0) {
			                     m_list->setCurrentRow(0);
		                     }
		                     ShowStatus(list.message);
		                     // Make pairing the obvious next step when it is what is missing.
		                     m_pair_button->setDefault(list.error ==
		                                               RemotePlay::ListError::NotPaired);
		                     m_play_button->setDefault(list.error == RemotePlay::ListError::None);
		                     UpdateButtons();
	                     });
}

void RemoteGamesDialog::Play() {
	const auto* item = m_list->currentItem();
	if (item == nullptr || m_settings.host.isEmpty()) {
		return;
	}
	const auto error = RemotePlay::Stream(m_settings.moonlight, m_settings.host, item->text());
	if (!error.isEmpty()) {
		QMessageBox::warning(this, tr("Play"), error);
		return;
	}
	ShowStatus(tr("Starting %1 in Moonlight...").arg(item->text()));
}

void RemoteGamesDialog::Pair() {
	RemotePlay::Pair(this, m_settings.moonlight, m_settings.host, [this]() { Refresh(); });
}
