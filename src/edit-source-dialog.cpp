/*
OBS Advanced Multiview - Edit Source dialog implementation

Copyright (C) 2025 VTB-LINK
License: GPL-2.0-or-later
*/

#include "edit-source-dialog.hpp"
#include "provider-settings-forms.hpp"
#include "signal-provider.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

EditSourceDialog::EditSourceDialog(const SignalConfig &cfg, QWidget *parent) : QDialog(parent), provider_(cfg.provider)
{
	setWindowTitle(QStringLiteral("Edit Source"));
	setModal(true);
	setMinimumSize(420, 480);
	resize(500, 600);

	auto *root = new QVBoxLayout(this);

	/* Heading line tells the user which provider this dialog is for so
	 * the surface stays clear when we add NDI / Spout / VLC variants. */
	QString title;
	switch (cfg.provider) {
	case SignalProviderType::Ffmpeg:
		title = QStringLiteral("FFmpeg media source");
		break;
	case SignalProviderType::Ndi:
		title = QStringLiteral("NDI source");
		break;
	case SignalProviderType::Spout:
		title = QStringLiteral("Spout sender");
		break;
	case SignalProviderType::Vlc:
		title = QStringLiteral("VLC media source");
		break;
	default:
		title = QStringLiteral("External source");
		break;
	}
	auto *heading = new QLabel(title, this);
	{
		QFont f = heading->font();
		f.setBold(true);
		heading->setFont(f);
	}
	root->addWidget(heading);

	/* Phase 3 / M6.2: when the cell's provider is not available in this
	 * OBS install (e.g. user opened a config saved in another OBS that
	 * had DistroAV but this one doesn't), surface that prominently and
	 * make Save unsafe. The form still loads so the user can inspect
	 * the existing config; we just refuse to write a new value through
	 * an absent provider. */
	bool provider_available = true;
	QString provider_unavailable_reason;
	if (cfg.provider != SignalProviderType::Unknown && !signal_provider_is_internal(cfg.provider)) {
		const auto *p = SignalProviderRegistry::instance().find(cfg.provider);
		if (!p || !p->is_available()) {
			provider_available = false;
			if (p) {
				const std::string r = p->unavailable_reason();
				provider_unavailable_reason = QString::fromStdString(r);
			}
			if (provider_unavailable_reason.isEmpty())
				provider_unavailable_reason = QStringLiteral("Required host plugin not installed.");
		}
	}
	if (!provider_available) {
		auto *banner = new QLabel(QStringLiteral("\u26A0  %1\n\nThe form below shows the persisted "
							 "configuration but Save is disabled until the missing host "
							 "plugin is installed and OBS is restarted.")
						  .arg(provider_unavailable_reason),
					  this);
		banner->setWordWrap(true);
		banner->setStyleSheet(QStringLiteral("color: #FFCC66; padding: 6px; "
						     "background: rgba(64,16,96,128); border-radius: 4px;"));
		root->addWidget(banner);
	}

	if (cfg.provider == SignalProviderType::Ffmpeg) {
		auto *scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		ffmpeg_form_ = new FfmpegMediaForm();
		ffmpeg_form_->load_from(cfg);
		scroll->setWidget(ffmpeg_form_);
		root->addWidget(scroll, 1);
	} else if (cfg.provider == SignalProviderType::Ndi) {
		auto *scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		ndi_form_ = new NdiSourceForm();
		ndi_form_->load_from(cfg);
		scroll->setWidget(ndi_form_);
		root->addWidget(scroll, 1);
	} else {
		/* Other external providers don't have an editor yet; their
		 * own milestones (M6.3 Spout, M6.4 VLC) will add sibling
		 * forms. Until then, show a plain message so the user knows
		 * nothing was saved. */
		auto *msg = new QLabel(QStringLiteral("Editing this provider's settings is not implemented yet. "
						      "Use Change Source... to replace the assignment instead."),
				       this);
		msg->setWordWrap(true);
		root->addWidget(msg);
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	if ((cfg.provider != SignalProviderType::Ffmpeg && cfg.provider != SignalProviderType::Ndi) ||
	    !provider_available) {
		/* No editable form for non-FFmpeg/non-NDI providers yet, OR
		 * the cell's provider is missing in this OBS install. Either
		 * way disable Save so cancel is the only safe action. */
		QPushButton *okBtn = buttons->button(QDialogButtonBox::Ok);
		if (okBtn)
			okBtn->setEnabled(false);
	}
	root->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::accepted, this, &EditSourceDialog::on_accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void EditSourceDialog::on_accept()
{
	if (provider_ == SignalProviderType::Ffmpeg && ffmpeg_form_) {
		if (!ffmpeg_form_->is_valid()) {
			QMessageBox::information(this, QStringLiteral("Media input required"),
						 ffmpeg_form_->invalid_reason());
			return;
		}
	} else if (provider_ == SignalProviderType::Ndi && ndi_form_) {
		if (!ndi_form_->is_valid()) {
			QMessageBox::information(this, QStringLiteral("NDI source required"),
						 ndi_form_->invalid_reason());
			return;
		}
	}
	accept();
}

SignalConfig EditSourceDialog::signal_config() const
{
	if (provider_ == SignalProviderType::Ffmpeg && ffmpeg_form_)
		return ffmpeg_form_->to_signal_config();
	if (provider_ == SignalProviderType::Ndi && ndi_form_)
		return ndi_form_->to_signal_config();
	return SignalConfig();
}
