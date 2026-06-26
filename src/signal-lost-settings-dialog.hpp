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

#pragma once

#include "multiview-instance.hpp"

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QString>

/* Phase 3 / M5.2: Signal Lost Settings dialog.
 *
 * Two-layer model only (Global + Cell). Per the design document
 * [docs/phase-3-signal-lost-and-external-sources-design.md] §5/§9 we keep
 * this dialog completely separate from CellDisplaySettingsDialog so visual
 * config and runtime/strategy config don't bleed into each other.
 */
class SignalLostSettingsDialog : public QDialog {
	Q_OBJECT
public:
	enum class Mode {
		Global, /* edit project-wide default */
		Cell,   /* edit per-cell override */
	};

	explicit SignalLostSettingsDialog(Mode mode, QWidget *parent = nullptr);

	void set_cell_position(int row, int col);

	/* Signal-Lost v2 axis A: the recovery-policy control only applies to
	 * providers Multiview can actively reconnect (FFmpeg / VLC). For NDI /
	 * Spout (host plugin owns reconnect) and internal cells it is greyed out.
	 * Call before set_cell_settings(); defaults to applicable (e.g. Global
	 * mode, which covers cells of every type). */
	void set_recovery_applicable(bool applicable);

	/* Global mode: payload is the LostSignalSettings struct directly. */
	void set_global_settings(const LostSignalSettings &s);
	LostSignalSettings get_global_settings() const;

	/* Cell mode: payload is the CellLostSignalSettings wrapper (mode + payload). */
	void set_cell_settings(const CellLostSignalSettings &c);
	CellLostSignalSettings get_cell_settings() const;

private slots:
	void on_inheritance_changed(int idx);
	void on_display_changed(int idx);
	void on_fallback_type_changed(int idx);
	void on_browse_fallback(); /* smart: image file / scene / source picker */

private:
	void build_ui();
	void apply_settings(const LostSignalSettings &s);
	LostSignalSettings collect_settings() const;
	void update_enabled_state();

	const Mode mode_;
	int cell_row_ = -1;
	int cell_col_ = -1;
	bool recovery_applicable_ = true;

	/* Inheritance combo \u2014 only meaningful in Cell mode; hidden in Global. */
	QComboBox *cmb_inherit_ = nullptr;

	/* Signal-Lost v2 unified "signal unavailable" group. */
	QComboBox *cmb_display_ = nullptr;         /* axis B1: displayContent */
	QComboBox *cmb_status_band_ = nullptr;     /* axis B2: statusBand */
	QComboBox *cmb_recovery_policy_ = nullptr; /* axis A: recoveryPolicy */

	/* Fallback group (relevant when displayContent == Fallback). */
	QComboBox *cmb_fallback_type_ = nullptr;
	QLineEdit *edit_fallback_name_ = nullptr;
	QComboBox *cmb_fallback_image_fit_ = nullptr;

	/* Manual reconnect throttle (anti-spam on Reconnect/Replay Now). */
	QSpinBox *spin_manual_cooldown_ = nullptr;
};
