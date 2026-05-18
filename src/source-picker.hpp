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

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QTabWidget>

class SourcePicker : public QDialog {
	Q_OBJECT

public:
	explicit SourcePicker(QWidget *parent = nullptr);

	CellAssignment result_assignment() const { return result_; }

private slots:
	void on_filter_changed(const QString &text);
	void on_item_double_clicked(QListWidgetItem *item);
	void on_accept();

private:
	void populate_list();

	QTabWidget *tabs_;
	QLineEdit *filter_edit_;
	QListWidget *special_list_;
	QListWidget *scene_list_;
	QListWidget *source_list_;

	CellAssignment result_;
};
