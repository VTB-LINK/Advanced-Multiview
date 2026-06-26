/*
OBS Advanced Multiview
Copyright (C) 2025 VTB-LINK

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "signal-lost-settings-dialog.hpp"
#include "amv-i18n.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QStandardItemModel>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

/* ---- enum <-> combo helpers (Signal-Lost v2) ---- */

namespace {

/* Axis B1: displayContent. Combo order: Black / LastFrame / Fallback / Clear. */
LostDisplayContent display_from_idx(int idx)
{
	switch (idx) {
	case 1:
		return LostDisplayContent::LastFrame;
	case 2:
		return LostDisplayContent::Fallback;
	case 3:
		return LostDisplayContent::ClearCell;
	default:
		return LostDisplayContent::Black;
	}
}

int idx_from_display(LostDisplayContent c)
{
	switch (c) {
	case LostDisplayContent::LastFrame:
		return 1;
	case LostDisplayContent::Fallback:
		return 2;
	case LostDisplayContent::ClearCell:
		return 3;
	default:
		return 0;
	}
}

/* Axis B2: statusBand. Combo order: None / SignalLost / Reconnecting / Auto. */
LostStatusBand band_from_idx(int idx)
{
	switch (idx) {
	case 0:
		return LostStatusBand::None;
	case 1:
		return LostStatusBand::SignalLost;
	case 2:
		return LostStatusBand::Reconnecting;
	default:
		return LostStatusBand::Auto;
	}
}

int idx_from_band(LostStatusBand b)
{
	switch (b) {
	case LostStatusBand::None:
		return 0;
	case LostStatusBand::SignalLost:
		return 1;
	case LostStatusBand::Reconnecting:
		return 2;
	default:
		return 3;
	}
}

/* Fallback type options (v2): image / PGM / PRVW / scene / source — all
 * enabled. PRVW (issue #5 stage C) renders the frontend's live preview scene
 * via the exact same path as a primary PRVW cell (current_preview_scene() ->
 * obs_source_video_render, with the obs_source_removed guard), which is
 * already shipped and safe, so it carries no extra risk. */
struct FallbackTypeOption {
	const char *token;
	const char *labelKey;
	bool enabled;
};

constexpr FallbackTypeOption kFallbackTypes[] = {
	{"image", "AMVPlugin.SignalLost.Fallback.StaticImage", true},
	{"pgm", "AMVPlugin.SignalLost.Fallback.Program", true},
	{"prvw", "AMVPlugin.SignalLost.Fallback.Preview", true},
	{"scene", "AMVPlugin.SignalLost.Fallback.Scene", true},
	{"source", "AMVPlugin.SignalLost.Fallback.Source", true},
};

constexpr int kFallbackTypeCount = sizeof(kFallbackTypes) / sizeof(kFallbackTypes[0]);

int idx_from_fallback_token(const std::string &token)
{
	for (int i = 0; i < kFallbackTypeCount; i++) {
		if (token == kFallbackTypes[i].token)
			return i;
	}
	return 0; /* default -> image */
}

const char *fallback_token_from_idx(int idx)
{
	if (idx >= 0 && idx < kFallbackTypeCount)
		return kFallbackTypes[idx].token;
	return "image";
}

/* Smart picker: gather scene names (frontend) or input-source names. Mirrors
 * SourcePicker's enumeration so the lists match what the main picker shows. */
QStringList enum_scene_names()
{
	QStringList names;
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const char *n = obs_source_get_name(scenes.sources.array[i]);
		if (n && *n)
			names.append(QString::fromUtf8(n));
	}
	obs_frontend_source_list_free(&scenes);
	names.sort(Qt::CaseInsensitive);
	return names;
}

QStringList enum_input_source_names()
{
	QStringList names;
	auto cb = [](void *param, obs_source_t *src) -> bool {
		auto *out = static_cast<QStringList *>(param);
		if (obs_source_get_type(src) == OBS_SOURCE_TYPE_SCENE)
			return true;
		uint32_t flags = obs_source_get_output_flags(src);
		if (!(flags & (OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO)))
			return true;
		const char *n = obs_source_get_name(src);
		if (n && *n)
			out->append(QString::fromUtf8(n));
		return true;
	};
	obs_enum_sources(cb, &names);
	names.sort(Qt::CaseInsensitive);
	return names;
}

/* Modal name picker with a live search box + list. Returns the chosen name,
 * or an empty string if cancelled. */
QString pick_name_with_search(QWidget *parent, const QString &title, const QStringList &names, const QString &current)
{
	QDialog dlg(parent);
	dlg.setWindowTitle(title);
	dlg.setMinimumSize(360, 420);
	auto *v = new QVBoxLayout(&dlg);

	auto *filter = new QLineEdit(&dlg);
	filter->setPlaceholderText(amv::text("AMVPlugin.SignalLost.PickFilter"));
	filter->setClearButtonEnabled(true);
	v->addWidget(filter);

	auto *list = new QListWidget(&dlg);
	v->addWidget(list, 1);
	for (const QString &n : names) {
		auto *it = new QListWidgetItem(n, list);
		if (n == current)
			list->setCurrentItem(it);
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	v->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, [&dlg](QListWidgetItem *) { dlg.accept(); });
	QObject::connect(filter, &QLineEdit::textChanged, &dlg, [list](const QString &text) {
		QListWidgetItem *firstVisible = nullptr;
		for (int i = 0; i < list->count(); i++) {
			QListWidgetItem *it = list->item(i);
			const bool hidden = !text.isEmpty() && !it->text().contains(text, Qt::CaseInsensitive);
			it->setHidden(hidden);
			if (!hidden && !firstVisible)
				firstVisible = it;
		}
		/* Keep a sensible selection visible as the user filters. */
		if (QListWidgetItem *cur = list->currentItem(); !cur || cur->isHidden())
			list->setCurrentItem(firstVisible);
	});
	filter->setFocus();

	if (dlg.exec() != QDialog::Accepted)
		return QString();
	QListWidgetItem *sel = list->currentItem();
	if (sel && !sel->isHidden())
		return sel->text();
	return QString();
}

} // namespace

/* ---- ctor / UI ---- */

SignalLostSettingsDialog::SignalLostSettingsDialog(Mode mode, QWidget *parent) : QDialog(parent), mode_(mode)
{
	setWindowTitle(mode_ == Mode::Global ? amv::text("AMVPlugin.SignalLost.Title.Global")
					     : amv::text("AMVPlugin.SignalLost.Title.Cell"));
	setMinimumWidth(440);
	build_ui();
	apply_settings(LostSignalSettings{}); /* initialize controls with defaults */
	update_enabled_state();
}

void SignalLostSettingsDialog::build_ui()
{
	auto *root = new QVBoxLayout(this);

	/* Inheritance row — visible only in Cell mode. Built in Global mode and
	 * hidden so widget pointers stay valid. */
	auto *inherit_row = new QHBoxLayout();
	inherit_row->addWidget(new QLabel(amv::text("AMVPlugin.Visual.Inheritance")));
	cmb_inherit_ = new QComboBox(this);
	cmb_inherit_->addItem(amv::text("AMVPlugin.SignalLost.Inherit.UseGlobal"));
	cmb_inherit_->addItem(amv::text("AMVPlugin.SignalLost.Inherit.OverrideCell"));
	inherit_row->addWidget(cmb_inherit_, 1);
	root->addLayout(inherit_row);
	if (mode_ == Mode::Global) {
		for (int i = 0; i < inherit_row->count(); i++) {
			QWidget *w = inherit_row->itemAt(i)->widget();
			if (w)
				w->hide();
		}
	}
	connect(cmb_inherit_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SignalLostSettingsDialog::on_inheritance_changed);

	/* Signal-Lost v2: unified "signal unavailable" group — replaces the old
	 * separate Internal-missing / External-lost sections. Applies to internal
	 * and external cells alike (the render path degrades options that don't
	 * apply to a given cell type). */
	auto *grp_unavail = new QGroupBox(amv::text("AMVPlugin.SignalLost.Unavailable.Title"), this);
	auto *form_unavail = new QFormLayout(grp_unavail);

	cmb_display_ = new QComboBox(grp_unavail);
	cmb_display_->addItem(amv::text("AMVPlugin.SignalLost.Display.Black"));
	cmb_display_->addItem(amv::text("AMVPlugin.SignalLost.Display.LastFrame"));
	cmb_display_->addItem(amv::text("AMVPlugin.SignalLost.Display.Fallback"));
	cmb_display_->addItem(amv::text("AMVPlugin.SignalLost.Display.ClearCell"));
	form_unavail->addRow(amv::text("AMVPlugin.SignalLost.Unavailable.Display"), cmb_display_);
	connect(cmb_display_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SignalLostSettingsDialog::on_display_changed);

	cmb_status_band_ = new QComboBox(grp_unavail);
	cmb_status_band_->addItem(amv::text("AMVPlugin.SignalLost.Band.None"));
	cmb_status_band_->addItem(amv::text("AMVPlugin.SignalLost.Band.SignalLost"));
	cmb_status_band_->addItem(amv::text("AMVPlugin.SignalLost.Band.Reconnecting"));
	cmb_status_band_->addItem(amv::text("AMVPlugin.SignalLost.Band.Auto"));
	form_unavail->addRow(amv::text("AMVPlugin.SignalLost.Unavailable.StatusBand"), cmb_status_band_);

	/* Axis A: recovery policy. Only FFmpeg/VLC honor it (NDI/Spout reconnect
	 * inside their host plugin, internal cells are event-driven); greying it
	 * per provider needs cell context the dialog doesn't carry yet, so it
	 * stays enabled for now and is simply a no-op for those cells. */
	cmb_recovery_policy_ = new QComboBox(grp_unavail);
	cmb_recovery_policy_->addItem(amv::text("AMVPlugin.SignalLost.Recovery.Auto"));
	cmb_recovery_policy_->addItem(amv::text("AMVPlugin.SignalLost.Recovery.ManualOnly"));
	cmb_recovery_policy_->setToolTip(amv::text("AMVPlugin.SignalLost.Recovery.Tooltip"));
	form_unavail->addRow(amv::text("AMVPlugin.SignalLost.Unavailable.Recovery"), cmb_recovery_policy_);
	root->addWidget(grp_unavail);

	/* Fallback group — relevant when displayContent == Fallback. */
	auto *grp_fallback = new QGroupBox(amv::text("AMVPlugin.SignalLost.Fallback.Title"), this);
	auto *form_fallback = new QFormLayout(grp_fallback);
	cmb_fallback_type_ = new QComboBox(grp_fallback);
	for (int i = 0; i < kFallbackTypeCount; i++)
		cmb_fallback_type_->addItem(amv::text(kFallbackTypes[i].labelKey));
	/* Grey out gated types (PRVW, stage C) — selectable again once their
	 * render path lands. Reach the model to clear the item flags. */
	if (auto *model = qobject_cast<QStandardItemModel *>(cmb_fallback_type_->model())) {
		for (int i = 0; i < kFallbackTypeCount; i++) {
			if (kFallbackTypes[i].enabled)
				continue;
			if (auto *item = model->item(i)) {
				Qt::ItemFlags flags = item->flags();
				flags &= ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
				item->setFlags(flags);
			}
		}
	}
	form_fallback->addRow(amv::text("AMVPlugin.SignalLost.Fallback.Type"), cmb_fallback_type_);

	auto *fallback_row = new QHBoxLayout();
	edit_fallback_name_ = new QLineEdit(grp_fallback);
	edit_fallback_name_->setPlaceholderText(amv::text("AMVPlugin.SignalLost.Fallback.NameOrPick"));
	fallback_row->addWidget(edit_fallback_name_, 1);
	auto *btn_fallback = new QToolButton(grp_fallback);
	btn_fallback->setText(QStringLiteral("..."));
	connect(btn_fallback, &QToolButton::clicked, this, &SignalLostSettingsDialog::on_browse_fallback);
	fallback_row->addWidget(btn_fallback);
	form_fallback->addRow(amv::text("AMVPlugin.SignalLost.Fallback.NamePath"), fallback_row);

	cmb_fallback_image_fit_ = new QComboBox(grp_fallback);
	cmb_fallback_image_fit_->addItem(amv::text("AMVPlugin.SignalLost.ImageFit.Stretch"));
	cmb_fallback_image_fit_->addItem(amv::text("AMVPlugin.SignalLost.ImageFit.Fit"));
	form_fallback->addRow(amv::text("AMVPlugin.SignalLost.Fallback.ImageFit"), cmb_fallback_image_fit_);
	root->addWidget(grp_fallback);

	connect(cmb_fallback_type_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&SignalLostSettingsDialog::on_fallback_type_changed);

	/* Manual reconnect throttle. Backoff is a fixed internal ladder (not
	 * user-exposed); this only rate-limits the Reconnect/Replay Now menu so
	 * a user can't spam it. */
	auto *grp_reconnect = new QGroupBox(amv::text("AMVPlugin.SignalLost.Reconnect.Title"), this);
	auto *form_reconnect = new QFormLayout(grp_reconnect);
	spin_manual_cooldown_ = new QSpinBox(grp_reconnect);
	spin_manual_cooldown_->setRange(0, 60000);
	spin_manual_cooldown_->setSuffix(QStringLiteral(" ms"));
	spin_manual_cooldown_->setToolTip(amv::text("AMVPlugin.SignalLost.Reconnect.ManualCooldownTooltip"));
	form_reconnect->addRow(amv::text("AMVPlugin.SignalLost.Reconnect.ManualCooldown"), spin_manual_cooldown_);
	root->addWidget(grp_reconnect);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	root->addWidget(buttons);
}

/* ---- public setters / getters ---- */

void SignalLostSettingsDialog::set_cell_position(int row, int col)
{
	cell_row_ = row;
	cell_col_ = col;
	if (mode_ == Mode::Cell && row >= 0 && col >= 0)
		setWindowTitle(amv::text("AMVPlugin.SignalLost.Title.CellPosition").arg(row).arg(col));
}

void SignalLostSettingsDialog::set_recovery_applicable(bool applicable)
{
	recovery_applicable_ = applicable;
	update_enabled_state();
}

void SignalLostSettingsDialog::set_global_settings(const LostSignalSettings &s)
{
	apply_settings(s);
	update_enabled_state();
}

LostSignalSettings SignalLostSettingsDialog::get_global_settings() const
{
	return collect_settings();
}

void SignalLostSettingsDialog::set_cell_settings(const CellLostSignalSettings &c)
{
	cell_row_ = c.row;
	cell_col_ = c.col;
	cmb_inherit_->setCurrentIndex(c.mode == InheritanceMode::Override ? 1 : 0);
	apply_settings(c.settings);
	update_enabled_state();
}

CellLostSignalSettings SignalLostSettingsDialog::get_cell_settings() const
{
	CellLostSignalSettings c;
	c.row = cell_row_;
	c.col = cell_col_;
	c.mode = cmb_inherit_->currentIndex() == 1 ? InheritanceMode::Override : InheritanceMode::Inherit;
	c.settings = collect_settings();
	return c;
}

/* ---- private helpers ---- */

void SignalLostSettingsDialog::apply_settings(const LostSignalSettings &s)
{
	cmb_display_->setCurrentIndex(idx_from_display(s.displayContent));
	cmb_status_band_->setCurrentIndex(idx_from_band(s.statusBand));
	cmb_recovery_policy_->setCurrentIndex(s.recoveryPolicy == RecoveryPolicy::ManualOnly ? 1 : 0);

	/* fallbackType may be empty on a never-configured fallback; default the
	 * combo to image so the row is meaningful once the user picks Fallback. */
	cmb_fallback_type_->setCurrentIndex(idx_from_fallback_token(s.fallbackType.empty() ? "image" : s.fallbackType));
	edit_fallback_name_->setText(QString::fromStdString(s.fallbackName));
	cmb_fallback_image_fit_->setCurrentIndex(s.fallbackImageFitMode == ImageFitMode::Fit ? 1 : 0);

	spin_manual_cooldown_->setValue(s.manualReconnectCooldownMs);
}

LostSignalSettings SignalLostSettingsDialog::collect_settings() const
{
	LostSignalSettings s;
	s.displayContent = display_from_idx(cmb_display_->currentIndex());
	s.statusBand = band_from_idx(cmb_status_band_->currentIndex());
	s.recoveryPolicy = cmb_recovery_policy_->currentIndex() == 1 ? RecoveryPolicy::ManualOnly
								     : RecoveryPolicy::Auto;

	s.fallbackType = fallback_token_from_idx(cmb_fallback_type_->currentIndex());
	s.fallbackName = edit_fallback_name_->text().toStdString();
	s.fallbackImageFitMode = cmb_fallback_image_fit_->currentIndex() == 1 ? ImageFitMode::Fit
									      : ImageFitMode::Stretch;

	s.manualReconnectCooldownMs = spin_manual_cooldown_->value();

	/* Populate the legacy render fields so the (unchanged) render path sees
	 * consistent values immediately — before this is persisted + reloaded. */
	derive_legacy_lost_fields(s);
	return s;
}

void SignalLostSettingsDialog::update_enabled_state()
{
	const bool inherit_locks_form = mode_ == Mode::Cell && cmb_inherit_->currentIndex() == 0;

	const auto setRowEnabled = [&](QWidget *w, bool enabled) {
		if (w)
			w->setEnabled(enabled && !inherit_locks_form);
	};

	setRowEnabled(cmb_display_, true);
	setRowEnabled(cmb_status_band_, true);
	/* Recovery only applies to FFmpeg/VLC cells; greyed for NDI/Spout/internal
	 * (set via set_recovery_applicable from the cell's provider). */
	setRowEnabled(cmb_recovery_policy_, recovery_applicable_);

	const bool fallback_active = display_from_idx(cmb_display_->currentIndex()) == LostDisplayContent::Fallback;
	const std::string fb_token = fallback_token_from_idx(cmb_fallback_type_->currentIndex());

	setRowEnabled(cmb_fallback_type_, fallback_active);
	/* Name / path: only image / scene / source need a value (PGM/PRVW don't). */
	setRowEnabled(edit_fallback_name_,
		      fallback_active && (fb_token == "image" || fb_token == "scene" || fb_token == "source"));
	/* Image fit only applies to a static image (sources/scenes letterbox). */
	setRowEnabled(cmb_fallback_image_fit_, fallback_active && fb_token == "image");

	setRowEnabled(spin_manual_cooldown_, true);
}

/* ---- slots ---- */

void SignalLostSettingsDialog::on_inheritance_changed(int)
{
	update_enabled_state();
}

void SignalLostSettingsDialog::on_display_changed(int)
{
	update_enabled_state();
}

void SignalLostSettingsDialog::on_fallback_type_changed(int)
{
	update_enabled_state();
}

void SignalLostSettingsDialog::on_browse_fallback()
{
	const std::string token = fallback_token_from_idx(cmb_fallback_type_->currentIndex());
	if (token == "image") {
		QString file = QFileDialog::getOpenFileName(this, amv::text("AMVPlugin.SignalLost.FileDialog.Fallback"),
							    QString(),
							    amv::text("AMVPlugin.SignalLost.FileDialog.ImageFilter"));
		if (!file.isEmpty())
			edit_fallback_name_->setText(file);
		return;
	}
	if (token == "scene" || token == "source") {
		const bool scenes = (token == "scene");
		QStringList names = scenes ? enum_scene_names() : enum_input_source_names();
		if (names.isEmpty())
			return;
		QString chosen = pick_name_with_search(this,
						       amv::text(scenes ? "AMVPlugin.SignalLost.PickScene.Title"
									: "AMVPlugin.SignalLost.PickSource.Title"),
						       names, edit_fallback_name_->text());
		if (!chosen.isEmpty())
			edit_fallback_name_->setText(chosen);
	}
	/* pgm/prvw need no name — the field/button are disabled for them. */
}
