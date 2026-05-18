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

#include "manager-dialog.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

ManagerDialog::ManagerDialog(ConfigManager *config, QWidget *parent)
	: QDialog(parent), config_(config)
{
	setWindowTitle(QStringLiteral("OBS Advanced Multiview"));
	setMinimumSize(800, 500);
	setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);

	setup_ui();
	refresh_instance_list();
	update_button_states();
}

ManagerDialog::~ManagerDialog() = default;

/* ---- UI setup ---- */

void ManagerDialog::setup_ui()
{
	auto *main_layout = new QHBoxLayout(this);

	auto *splitter = new QSplitter(Qt::Horizontal, this);

	auto *left_panel = new QWidget(this);
	setup_left_panel(left_panel);
	splitter->addWidget(left_panel);

	auto *right_panel = new QWidget(this);
	setup_right_panel(right_panel);
	splitter->addWidget(right_panel);

	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 2);

	main_layout->addWidget(splitter);
}

void ManagerDialog::setup_left_panel(QWidget *panel)
{
	auto *layout = new QVBoxLayout(panel);

	auto *title = new QLabel(
		QStringLiteral("Multiview Instances"), panel);
	title->setStyleSheet(QStringLiteral("font-weight: bold;"));
	layout->addWidget(title);

	instance_list_ = new QListWidget(panel);
	layout->addWidget(instance_list_);

	connect(instance_list_, &QListWidget::currentRowChanged, this,
		&ManagerDialog::on_instance_selection_changed);
	connect(instance_list_, &QListWidget::itemClicked, this,
		[this](QListWidgetItem *item) {
			if (!item)
				return;
			std::string uuid = item->data(Qt::UserRole)
						    .toString()
						    .toStdString();
			show_instance_detail(uuid);
		});
	connect(instance_list_, &QListWidget::itemDoubleClicked, this,
		&ManagerDialog::on_open_instance);

	/* buttons */
	auto *btn_layout = new QVBoxLayout();

	btn_new_ = new QPushButton(QStringLiteral("New"), panel);
	btn_rename_ = new QPushButton(QStringLiteral("Rename"), panel);
	btn_clone_ = new QPushButton(QStringLiteral("Clone"), panel);
	btn_delete_ = new QPushButton(QStringLiteral("Delete"), panel);
	btn_open_ = new QPushButton(QStringLiteral("Open"), panel);

	btn_layout->addWidget(btn_new_);
	btn_layout->addWidget(btn_rename_);
	btn_layout->addWidget(btn_clone_);
	btn_layout->addWidget(btn_delete_);
	btn_layout->addWidget(btn_open_);

	layout->addLayout(btn_layout);

	btn_global_settings_ =
		new QPushButton(QStringLiteral("Global Settings"), panel);
	layout->addWidget(btn_global_settings_);

	connect(btn_new_, &QPushButton::clicked, this,
		&ManagerDialog::on_new_instance);
	connect(btn_rename_, &QPushButton::clicked, this,
		&ManagerDialog::on_rename_instance);
	connect(btn_clone_, &QPushButton::clicked, this,
		&ManagerDialog::on_clone_instance);
	connect(btn_delete_, &QPushButton::clicked, this,
		&ManagerDialog::on_delete_instance);
	connect(btn_open_, &QPushButton::clicked, this,
		&ManagerDialog::on_open_instance);
	connect(btn_global_settings_, &QPushButton::clicked, this,
		&ManagerDialog::on_global_settings_clicked);
}

void ManagerDialog::setup_right_panel(QWidget *panel)
{
	auto *layout = new QVBoxLayout(panel);

	right_stack_ = new QStackedWidget(panel);

	/* Page 0: empty placeholder */
	page_empty_ = new QWidget();
	auto *empty_layout = new QVBoxLayout(page_empty_);
	auto *empty_label = new QLabel(
		QStringLiteral("Select an instance or create a new one"),
		page_empty_);
	empty_label->setAlignment(Qt::AlignCenter);
	empty_layout->addWidget(empty_label);
	right_stack_->addWidget(page_empty_);

	/* Page 1: instance detail */
	page_instance_detail_ = new QWidget();
	auto *detail_layout = new QVBoxLayout(page_instance_detail_);

	detail_name_label_ = new QLabel(page_instance_detail_);
	detail_name_label_->setStyleSheet(
		QStringLiteral("font-size: 16px; font-weight: bold;"));
	detail_layout->addWidget(detail_name_label_);

	detail_uuid_label_ = new QLabel(page_instance_detail_);
	detail_uuid_label_->setStyleSheet(QStringLiteral("color: gray;"));
	detail_layout->addWidget(detail_uuid_label_);

	detail_layout_label_ = new QLabel(page_instance_detail_);
	detail_layout->addWidget(detail_layout_label_);

	/* Gutter settings per instance */
	detail_use_global_gutter_ = new QCheckBox(
		QStringLiteral("Inherit gutter from Global Settings"),
		page_instance_detail_);
	detail_layout->addWidget(detail_use_global_gutter_);

	auto *gutter_inst_row = new QHBoxLayout();
	gutter_inst_row->addWidget(
		new QLabel(QStringLiteral("Gutter (px):"),
			   page_instance_detail_));
	detail_gutter_spin_ = new QSpinBox(page_instance_detail_);
	detail_gutter_spin_->setRange(0, 50);
	gutter_inst_row->addWidget(detail_gutter_spin_);
	gutter_inst_row->addStretch();
	detail_layout->addLayout(gutter_inst_row);

	detail_gutter_effective_ = new QLabel(page_instance_detail_);
	detail_gutter_effective_->setStyleSheet(
		QStringLiteral("color: gray;"));
	detail_layout->addWidget(detail_gutter_effective_);

	connect(detail_use_global_gutter_, &QCheckBox::toggled, this,
		[this](bool checked) {
			detail_gutter_spin_->setEnabled(!checked);
			MultiviewInstance *inst =
				config_->find_instance(current_detail_uuid_);
			if (!inst)
				return;
			inst->useGlobalGutter = checked;
			if (!checked)
				inst->layout.gutterPx =
					detail_gutter_spin_->value();
			int eff = inst->effective_gutter(
				config_->global_settings().defaultGutterPx);
			detail_gutter_effective_->setText(
				QStringLiteral("Effective gutter: %1px")
					.arg(eff));
			config_->save();
		});

	connect(detail_gutter_spin_,
		QOverload<int>::of(&QSpinBox::valueChanged), this,
		[this](int value) {
			MultiviewInstance *inst =
				config_->find_instance(current_detail_uuid_);
			if (!inst || inst->useGlobalGutter)
				return;
			inst->layout.gutterPx = value;
			detail_gutter_effective_->setText(
				QStringLiteral("Effective gutter: %1px")
					.arg(value));
			config_->save();
		});

	detail_layout->addStretch();

	auto *detail_hint = new QLabel(
		QStringLiteral("Grid editor will be available in Milestone 2"),
		page_instance_detail_);
	detail_hint->setAlignment(Qt::AlignCenter);
	detail_hint->setStyleSheet(QStringLiteral("color: gray;"));
	detail_layout->addWidget(detail_hint);

	right_stack_->addWidget(page_instance_detail_);

	/* Page 2: global settings */
	page_global_settings_ = new QWidget();
	auto *gs_layout = new QVBoxLayout(page_global_settings_);

	auto *gs_title =
		new QLabel(QStringLiteral("Global Settings"),
			   page_global_settings_);
	gs_title->setStyleSheet(
		QStringLiteral("font-size: 16px; font-weight: bold;"));
	gs_layout->addWidget(gs_title);

	auto *gutter_row = new QHBoxLayout();
	gutter_row->addWidget(new QLabel(
		QStringLiteral("Default Gutter (px):"),
		page_global_settings_));
	spin_default_gutter_ = new QSpinBox(page_global_settings_);
	spin_default_gutter_->setRange(0, 50);
	spin_default_gutter_->setValue(
		config_->global_settings().defaultGutterPx);
	gutter_row->addWidget(spin_default_gutter_);
	gutter_row->addStretch();
	gs_layout->addLayout(gutter_row);

	auto *gs_apply =
		new QPushButton(QStringLiteral("Apply"), page_global_settings_);
	gs_layout->addWidget(gs_apply);
	gs_layout->addStretch();

	connect(gs_apply, &QPushButton::clicked, this, [this]() {
		config_->global_settings().defaultGutterPx =
			spin_default_gutter_->value();
		config_->save();
		obs_log(LOG_INFO, "global settings saved (gutter=%d)",
			spin_default_gutter_->value());
	});

	right_stack_->addWidget(page_global_settings_);

	layout->addWidget(right_stack_);
}

/* ---- instance list ---- */

void ManagerDialog::refresh_instance_list()
{
	instance_list_->clear();
	for (auto &inst : config_->instances()) {
		auto *item = new QListWidgetItem(
			QString::fromStdString(inst.name));
		item->setData(Qt::UserRole,
			      QString::fromStdString(inst.uuid));
		instance_list_->addItem(item);
	}
	update_button_states();
}

void ManagerDialog::update_button_states()
{
	bool has_selection = instance_list_->currentItem() != nullptr;
	btn_rename_->setEnabled(has_selection);
	btn_clone_->setEnabled(has_selection);
	btn_delete_->setEnabled(has_selection);
	btn_open_->setEnabled(has_selection);
}

/* ---- slots ---- */

void ManagerDialog::on_instance_selection_changed()
{
	update_button_states();
	auto *item = instance_list_->currentItem();
	if (!item) {
		right_stack_->setCurrentIndex(PAGE_EMPTY);
		return;
	}
	std::string uuid = item->data(Qt::UserRole).toString().toStdString();
	show_instance_detail(uuid);
}

void ManagerDialog::on_new_instance()
{
	bool ok;
	QString name = QInputDialog::getText(this, QStringLiteral("New Instance"),
					     QStringLiteral("Instance name:"),
					     QLineEdit::Normal, QString(), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	MultiviewInstance *inst =
		config_->add_instance(name.trimmed().toStdString());
	(void)inst; /* useGlobalGutter=true by default */
	config_->save();
	refresh_instance_list();
	instance_list_->setCurrentRow(instance_list_->count() - 1);
}

void ManagerDialog::on_rename_instance()
{
	auto *item = instance_list_->currentItem();
	if (!item)
		return;

	std::string uuid = item->data(Qt::UserRole).toString().toStdString();

	bool ok;
	QString name = QInputDialog::getText(
		this, QStringLiteral("Rename Instance"),
		QStringLiteral("New name:"), QLineEdit::Normal,
		item->text(), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	config_->rename_instance(uuid, name.trimmed().toStdString());
	config_->save();
	int row = instance_list_->currentRow();
	refresh_instance_list();
	instance_list_->setCurrentRow(row);
}

void ManagerDialog::on_clone_instance()
{
	auto *item = instance_list_->currentItem();
	if (!item)
		return;

	std::string uuid = item->data(Qt::UserRole).toString().toStdString();

	bool ok;
	QString name = QInputDialog::getText(
		this, QStringLiteral("Clone Instance"),
		QStringLiteral("Name for cloned instance:"),
		QLineEdit::Normal,
		item->text() + QStringLiteral(" (Copy)"), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	config_->clone_instance(uuid, name.trimmed().toStdString());
	config_->save();
	refresh_instance_list();
	instance_list_->setCurrentRow(instance_list_->count() - 1);
}

void ManagerDialog::on_delete_instance()
{
	auto *item = instance_list_->currentItem();
	if (!item)
		return;

	std::string uuid = item->data(Qt::UserRole).toString().toStdString();

	auto ret = QMessageBox::question(
		this, QStringLiteral("Delete Instance"),
		QStringLiteral("Delete instance \"%1\"?").arg(item->text()),
		QMessageBox::Yes | QMessageBox::No);
	if (ret != QMessageBox::Yes)
		return;

	config_->delete_instance(uuid);
	config_->save();
	refresh_instance_list();
	right_stack_->setCurrentIndex(PAGE_EMPTY);
}

void ManagerDialog::on_open_instance()
{
	auto *item = instance_list_->currentItem();
	if (!item)
		return;

	/* Placeholder: actual MultiviewWindow will be created in Milestone 3 */
	obs_log(LOG_INFO, "open instance: %s (window creation pending M3)",
		item->data(Qt::UserRole).toString().toUtf8().constData());
}

void ManagerDialog::on_global_settings_clicked()
{
	instance_list_->clearSelection();
	instance_list_->setCurrentRow(-1);
	show_global_settings();
}

/* ---- right panel pages ---- */

void ManagerDialog::show_instance_detail(const std::string &uuid)
{
	MultiviewInstance *inst = config_->find_instance(uuid);
	if (!inst) {
		right_stack_->setCurrentIndex(PAGE_EMPTY);
		return;
	}

	current_detail_uuid_ = uuid;

	detail_name_label_->setText(QString::fromStdString(inst->name));
	detail_uuid_label_->setText(
		QStringLiteral("UUID: %1")
			.arg(QString::fromStdString(inst->uuid)));
	detail_layout_label_->setText(
		QStringLiteral("Layout: %1 x %2")
			.arg(inst->layout.rows)
			.arg(inst->layout.columns));

	/* Block signals to avoid triggering save during UI update */
	detail_use_global_gutter_->blockSignals(true);
	detail_gutter_spin_->blockSignals(true);

	detail_use_global_gutter_->setChecked(inst->useGlobalGutter);
	detail_gutter_spin_->setValue(inst->layout.gutterPx);
	detail_gutter_spin_->setEnabled(!inst->useGlobalGutter);

	int eff = inst->effective_gutter(
		config_->global_settings().defaultGutterPx);
	detail_gutter_effective_->setText(
		QStringLiteral("Effective gutter: %1px").arg(eff));

	detail_use_global_gutter_->blockSignals(false);
	detail_gutter_spin_->blockSignals(false);

	right_stack_->setCurrentIndex(PAGE_INSTANCE_DETAIL);
}

void ManagerDialog::show_global_settings()
{
	spin_default_gutter_->setValue(
		config_->global_settings().defaultGutterPx);
	right_stack_->setCurrentIndex(PAGE_GLOBAL_SETTINGS);
}
