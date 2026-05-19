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
#include "grid-preview-widget.hpp"
#include "multiview-window.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <set>

/* Load an icon from the active OBS theme directory.
 * Detects light/dark via palette, then searches for the
 * matching theme folder next to the OBS executable. */
static QIcon obs_theme_icon(const char *name)
{
	/* Determine if we're in a light or dark theme via text luminance */
	QColor textColor = QApplication::palette().color(QPalette::WindowText);
	bool isDark = (textColor.lightnessF() > 0.5);

	QString app_dir = QApplication::applicationDirPath();
	QDir dir(app_dir);
	dir.cdUp(); /* 64bit -> bin */
	dir.cdUp(); /* bin -> root */

	/* Preferred theme order based on palette */
	QStringList themes;
	if (isDark)
		themes = {"Dark", "Yami", "Rachni", "Acri", "Light"};
	else
		themes = {"Light", "Acri", "Rachni", "Yami", "Dark"};

	for (auto &t : themes) {
		QString path = dir.absoluteFilePath(
			QStringLiteral("data/obs-studio/themes/%1/%2.svg").arg(t, QString::fromUtf8(name)));
		if (QFile::exists(path))
			return QIcon(path);
	}
	return QIcon();
}

/* Generate a northeast-arrow (↗) icon matching current theme color */
static QIcon make_open_icon()
{
	QColor c = QApplication::palette().color(QPalette::WindowText);
	QString color = c.name(); /* e.g. "#fefefe" or "#202020" */

	/* Simple SVG: arrow pointing to top-right with a short line */
	QString svg = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' "
				     "viewBox='0 0 16 16' width='16' height='16'>"
				     "<path d='M4 12 L12 4 M12 4 L6 4 M12 4 L12 10' "
				     "fill='none' stroke='%1' stroke-width='2' "
				     "stroke-linecap='round' stroke-linejoin='round'/>"
				     "</svg>")
			      .arg(color);

	QPixmap pix(16, 16);
	pix.fill(Qt::transparent);
	QSvgRenderer renderer(svg.toUtf8());
	QPainter painter(&pix);
	renderer.render(&painter);
	return QIcon(pix);
}

ManagerDialog::ManagerDialog(ConfigManager *config, QWidget *parent) : QDialog(parent), config_(config)
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
	auto *main_layout = new QVBoxLayout(this);
	main_layout->setContentsMargins(0, 0, 0, 0);

	tab_widget_ = new QTabWidget(this);

	auto *instances_tab = new QWidget();
	setup_instances_tab(instances_tab);
	tab_widget_->addTab(instances_tab, QStringLiteral("Instances"));

	auto *settings_tab = new QWidget();
	setup_settings_tab(settings_tab);
	tab_widget_->addTab(settings_tab, QStringLiteral("Settings"));

	main_layout->addWidget(tab_widget_);
}

void ManagerDialog::setup_instances_tab(QWidget *tab)
{
	auto *layout = new QHBoxLayout(tab);
	layout->setContentsMargins(0, 0, 0, 0);

	splitter_ = new QSplitter(Qt::Horizontal, tab);
	splitter_->setChildrenCollapsible(false);

	auto *left_panel = new QWidget(tab);
	setup_left_panel(left_panel);

	auto *right_panel = new QWidget(tab);
	setup_right_panel(right_panel);

	splitter_->addWidget(left_panel);
	splitter_->addWidget(right_panel);
	splitter_->setStretchFactor(0, 0);
	splitter_->setStretchFactor(1, 1);
	splitter_->setSizes({200, 600});

	layout->addWidget(splitter_);
}

void ManagerDialog::setup_left_panel(QWidget *panel)
{
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(4, 4, 0, 4);

	instance_tree_ = new QTreeWidget(panel);
	instance_tree_->setHeaderHidden(true);
	instance_tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
	instance_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
	instance_tree_->setDragDropMode(QAbstractItemView::InternalMove);
	instance_tree_->setRootIsDecorated(false);
	instance_tree_->setIndentation(16);
	instance_tree_->setIconSize(QSize(0, 0));

	/* Style: remove leading space for leaf items, show arrows only
	 * on items that have children (folders). */
	{
		QString app_dir = QApplication::applicationDirPath();
		QDir d(app_dir);
		d.cdUp();
		d.cdUp();
		QColor tc = QApplication::palette().color(QPalette::WindowText);
		QString theme = (tc.lightnessF() > 0.5) ? "Dark" : "Light";
		QString closed_arrow =
			d.absoluteFilePath(QStringLiteral("data/obs-studio/themes/%1/expand.svg").arg(theme));
		QString open_arrow =
			d.absoluteFilePath(QStringLiteral("data/obs-studio/themes/%1/collapse.svg").arg(theme));

		instance_tree_->setStyleSheet(QStringLiteral("QTreeWidget::item { padding: 2px 0px; }"
							     "QTreeWidget::branch:has-children:!has-siblings:closed,"
							     "QTreeWidget::branch:closed:has-children:has-siblings {"
							     "  border-image: none;"
							     "  image: url(%1);"
							     "}"
							     "QTreeWidget::branch:open:has-children:!has-siblings,"
							     "QTreeWidget::branch:open:has-children:has-siblings {"
							     "  border-image: none;"
							     "  image: url(%2);"
							     "}"
							     "QTreeWidget::branch:!has-children {"
							     "  border-image: none;"
							     "  image: none;"
							     "}")
						      .arg(closed_arrow, open_arrow));
	}
	layout->addWidget(instance_tree_);

	connect(instance_tree_, &QTreeWidget::itemSelectionChanged, this,
		&ManagerDialog::on_instance_selection_changed);
	connect(instance_tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
		if (!item || is_folder_item(item))
			return;
		on_open_instance();
	});
	connect(instance_tree_, &QTreeWidget::customContextMenuRequested, this, &ManagerDialog::show_context_menu);

	/* Bottom toolbar - OBS style icon buttons */
	auto *toolbar = new QHBoxLayout();
	toolbar->setSpacing(2);

	const int btn_sz = 24;

	btn_new_ = new QPushButton(panel);
	btn_new_->setIcon(obs_theme_icon("plus"));
	btn_new_->setFixedSize(btn_sz, btn_sz);
	btn_new_->setFlat(true);
	btn_new_->setToolTip(QStringLiteral("New Instance"));
	toolbar->addWidget(btn_new_);

	btn_delete_ = new QPushButton(panel);
	btn_delete_->setIcon(obs_theme_icon("trash"));
	btn_delete_->setFixedSize(btn_sz, btn_sz);
	btn_delete_->setFlat(true);
	btn_delete_->setToolTip(QStringLiteral("Delete"));
	toolbar->addWidget(btn_delete_);

	btn_clone_ = new QPushButton(panel);
	btn_clone_->setIcon(obs_theme_icon("popout"));
	btn_clone_->setFixedSize(btn_sz, btn_sz);
	btn_clone_->setFlat(true);
	btn_clone_->setToolTip(QStringLiteral("Clone Instance"));
	toolbar->addWidget(btn_clone_);

	btn_open_ = new QPushButton(panel);
	btn_open_->setIcon(make_open_icon());
	btn_open_->setFixedSize(btn_sz, btn_sz);
	btn_open_->setFlat(true);
	btn_open_->setToolTip(QStringLiteral("Open Multiview Window"));
	toolbar->addWidget(btn_open_);

	toolbar->addStretch();

	btn_move_up_ = new QPushButton(panel);
	btn_move_up_->setIcon(obs_theme_icon("up"));
	btn_move_up_->setFixedSize(btn_sz, btn_sz);
	btn_move_up_->setFlat(true);
	btn_move_up_->setToolTip(QStringLiteral("Move Up"));
	toolbar->addWidget(btn_move_up_);

	btn_move_down_ = new QPushButton(panel);
	btn_move_down_->setIcon(obs_theme_icon("down"));
	btn_move_down_->setFixedSize(btn_sz, btn_sz);
	btn_move_down_->setFlat(true);
	btn_move_down_->setToolTip(QStringLiteral("Move Down"));
	toolbar->addWidget(btn_move_down_);

	layout->addLayout(toolbar);

	connect(btn_new_, &QPushButton::clicked, this, &ManagerDialog::on_new_instance);
	connect(btn_clone_, &QPushButton::clicked, this, &ManagerDialog::on_clone_instance);
	connect(btn_delete_, &QPushButton::clicked, this, &ManagerDialog::on_delete_instance);
	connect(btn_open_, &QPushButton::clicked, this, &ManagerDialog::on_open_instance);
	connect(btn_move_up_, &QPushButton::clicked, this, [this]() {
		/* TODO: reorder support */
	});
	connect(btn_move_down_, &QPushButton::clicked, this, [this]() {
		/* TODO: reorder support */
	});
}

void ManagerDialog::setup_right_panel(QWidget *panel)
{
	auto *layout = new QVBoxLayout(panel);

	right_stack_ = new QStackedWidget(panel);

	/* Page 0: empty placeholder */
	page_empty_ = new QWidget();
	auto *empty_layout = new QVBoxLayout(page_empty_);
	auto *empty_label = new QLabel(QStringLiteral("Select an instance or create a new one"), page_empty_);
	empty_label->setAlignment(Qt::AlignCenter);
	empty_layout->addWidget(empty_label);
	right_stack_->addWidget(page_empty_);

	/* Page 1: instance detail */
	page_instance_detail_ = new QWidget();
	auto *detail_layout = new QVBoxLayout(page_instance_detail_);

	detail_name_label_ = new QLabel(page_instance_detail_);
	detail_name_label_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
	detail_layout->addWidget(detail_name_label_);

	detail_uuid_label_ = new QLabel(page_instance_detail_);
	detail_uuid_label_->setStyleSheet(QStringLiteral("color: gray;"));
	detail_layout->addWidget(detail_uuid_label_);

	detail_layout_label_ = new QLabel(page_instance_detail_);
	detail_layout->addWidget(detail_layout_label_);

	/* Gutter settings per instance */
	detail_use_global_gutter_ =
		new QCheckBox(QStringLiteral("Inherit gutter from Global Settings"), page_instance_detail_);
	detail_layout->addWidget(detail_use_global_gutter_);

	auto *gutter_inst_row = new QHBoxLayout();
	gutter_inst_row->addWidget(new QLabel(QStringLiteral("Gutter (px):"), page_instance_detail_));
	detail_gutter_spin_ = new QSpinBox(page_instance_detail_);
	detail_gutter_spin_->setRange(0, 50);
	gutter_inst_row->addWidget(detail_gutter_spin_);
	gutter_inst_row->addStretch();
	detail_layout->addLayout(gutter_inst_row);

	detail_gutter_effective_ = new QLabel(page_instance_detail_);
	detail_gutter_effective_->setStyleSheet(QStringLiteral("color: gray;"));
	detail_layout->addWidget(detail_gutter_effective_);

	connect(detail_use_global_gutter_, &QCheckBox::toggled, this, [this](bool checked) {
		detail_gutter_spin_->setEnabled(!checked);
		MultiviewInstance *inst = config_->find_instance(current_detail_uuid_);
		if (!inst)
			return;
		inst->useGlobalGutter = checked;
		if (!checked)
			inst->layout.gutterPx = detail_gutter_spin_->value();
		int eff = inst->effective_gutter(config_->global_settings().defaultGutterPx);
		detail_gutter_effective_->setText(QStringLiteral("Effective gutter: %1px").arg(eff));
		config_->save();
		notify_multiview_layout_changed(current_detail_uuid_);
	});

	connect(detail_gutter_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
		MultiviewInstance *inst = config_->find_instance(current_detail_uuid_);
		if (!inst || inst->useGlobalGutter)
			return;
		inst->layout.gutterPx = value;
		detail_gutter_effective_->setText(QStringLiteral("Effective gutter: %1px").arg(value));
		config_->save();
		notify_multiview_layout_changed(current_detail_uuid_);
	});

	detail_layout->addStretch();

	btn_edit_grid_ = new QPushButton(QStringLiteral("Edit Grid..."), page_instance_detail_);
	detail_layout->addWidget(btn_edit_grid_);
	connect(btn_edit_grid_, &QPushButton::clicked, this, &ManagerDialog::on_edit_grid_clicked);

	right_stack_->addWidget(page_instance_detail_);

	/* Page 2: grid editor */
	page_grid_editor_ = new QWidget();
	auto *ge_layout = new QVBoxLayout(page_grid_editor_);

	/* Title + back button row */
	auto *ge_top_row = new QHBoxLayout();
	btn_grid_back_ = new QPushButton(QStringLiteral("<< Back"), page_grid_editor_);
	ge_top_row->addWidget(btn_grid_back_);
	grid_editor_title_ = new QLabel(page_grid_editor_);
	grid_editor_title_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
	ge_top_row->addWidget(grid_editor_title_);
	ge_top_row->addStretch();
	ge_layout->addLayout(ge_top_row);

	/* Controls row: rows, cols, gutter */
	auto *ge_ctrl_row = new QHBoxLayout();

	ge_ctrl_row->addWidget(new QLabel(QStringLiteral("Rows:"), page_grid_editor_));
	grid_rows_spin_ = new QSpinBox(page_grid_editor_);
	grid_rows_spin_->setRange(1, 10);
	ge_ctrl_row->addWidget(grid_rows_spin_);

	ge_ctrl_row->addWidget(new QLabel(QStringLiteral("Cols:"), page_grid_editor_));
	grid_cols_spin_ = new QSpinBox(page_grid_editor_);
	grid_cols_spin_->setRange(1, 10);
	ge_ctrl_row->addWidget(grid_cols_spin_);

	ge_ctrl_row->addStretch();
	ge_layout->addLayout(ge_ctrl_row);

	/* Span controls row */
	auto *ge_span_row = new QHBoxLayout();
	btn_add_span_ = new QPushButton(QStringLiteral("Add Span..."), page_grid_editor_);
	btn_remove_span_ = new QPushButton(QStringLiteral("Remove Selected Span"), page_grid_editor_);
	ge_span_row->addWidget(btn_add_span_);
	ge_span_row->addWidget(btn_remove_span_);
	grid_span_info_ = new QLabel(page_grid_editor_);
	grid_span_info_->setStyleSheet(QStringLiteral("color: gray;"));
	ge_span_row->addWidget(grid_span_info_);
	ge_span_row->addStretch();
	ge_layout->addLayout(ge_span_row);

	/* Grid preview */
	grid_preview_ = new GridPreviewWidget(page_grid_editor_);
	ge_layout->addWidget(grid_preview_, 1);

	/* Save button */
	btn_grid_save_ = new QPushButton(QStringLiteral("Save Layout"), page_grid_editor_);
	ge_layout->addWidget(btn_grid_save_);

	/* Connections */
	connect(btn_grid_back_, &QPushButton::clicked, this, [this]() { show_instance_detail(grid_edit_uuid_); });

	connect(grid_rows_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
		grid_edit_layout_.rows = val;
		/* Remove spans that go out of bounds */
		auto &spans = grid_edit_layout_.spans;
		spans.erase(std::remove_if(spans.begin(), spans.end(),
					   [&](const SpanRegion &s) { return s.row + s.rowSpan > val; }),
			    spans.end());
		update_grid_preview();
	});

	connect(grid_cols_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
		grid_edit_layout_.columns = val;
		auto &spans = grid_edit_layout_.spans;
		spans.erase(std::remove_if(spans.begin(), spans.end(),
					   [&](const SpanRegion &s) { return s.col + s.colSpan > val; }),
			    spans.end());
		update_grid_preview();
	});

	connect(btn_add_span_, &QPushButton::clicked, this, [this]() {
		SelectionRect sr;
		if (!grid_preview_->selection_is_mergeable(sr)) {
			QMessageBox::information(this, QStringLiteral("Add Span"),
						 QStringLiteral("Select a rectangular group of cells "
								"(Ctrl+Click or Shift+Click) to merge."));
			return;
		}

		/* Check overlap with existing spans */
		if (grid_preview_->selection_overlaps_span()) {
			QMessageBox::warning(this, QStringLiteral("Add Span"),
					     QStringLiteral("Selection overlaps an existing span. "
							    "Remove it first."));
			return;
		}

		SpanRegion newSpan{sr.row, sr.col, sr.rowSpan, sr.colSpan};

		/* Validate with layout engine */
		LayoutEngine validator;
		validator.set_layout(grid_edit_layout_);
		if (validator.validate_span(newSpan) != LayoutEngine::SpanError::None) {
			QMessageBox::warning(this, QStringLiteral("Add Span"),
					     QStringLiteral("Cannot create this span. "
							    "It may overlap or be out of bounds."));
			return;
		}

		grid_edit_layout_.spans.push_back(newSpan);
		grid_preview_->clear_selection();
		update_grid_preview();
	});

	connect(btn_remove_span_, &QPushButton::clicked, this, [this]() {
		/* Find which span is selected (first selected position that is inside a span) */
		auto &sel = grid_preview_->selected_positions();
		if (sel.empty())
			return;

		std::set<int> span_indices;
		for (auto &[r, c] : sel) {
			for (int i = 0; i < (int)grid_edit_layout_.spans.size(); i++) {
				auto &s = grid_edit_layout_.spans[i];
				if (r >= s.row && r < s.row + s.rowSpan && c >= s.col && c < s.col + s.colSpan) {
					span_indices.insert(i);
				}
			}
		}

		if (span_indices.empty()) {
			QMessageBox::information(this, QStringLiteral("Remove Span"),
						 QStringLiteral("No span in current selection."));
			return;
		}

		/* Remove in reverse order to keep indices valid */
		for (auto it = span_indices.rbegin(); it != span_indices.rend(); ++it) {
			grid_edit_layout_.spans.erase(grid_edit_layout_.spans.begin() + *it);
		}

		grid_preview_->clear_selection();
		update_grid_preview();
	});

	connect(btn_grid_save_, &QPushButton::clicked, this, [this]() {
		MultiviewInstance *inst = config_->find_instance(grid_edit_uuid_);
		if (!inst)
			return;

		/* Validate all spans */
		auto err = LayoutEngine::validate_all_spans(grid_edit_layout_);
		if (err != LayoutEngine::SpanError::None) {
			QMessageBox::warning(this, QStringLiteral("Save Layout"),
					     QStringLiteral("Layout has invalid spans. "
							    "Please fix before saving."));
			return;
		}

		inst->layout.rows = grid_edit_layout_.rows;
		inst->layout.columns = grid_edit_layout_.columns;
		inst->layout.spans = grid_edit_layout_.spans;
		inst->layoutDirty = true;
		config_->save();

		obs_log(LOG_INFO, "layout saved: %dx%d, %d spans", inst->layout.rows, inst->layout.columns,
			(int)inst->layout.spans.size());

		show_instance_detail(grid_edit_uuid_);
	});

	connect(grid_preview_, &GridPreviewWidget::selection_changed, this, [this]() {
		auto &sel = grid_preview_->selected_positions();
		if (sel.empty()) {
			grid_span_info_->setText(QString());
			btn_add_span_->setEnabled(false);
			btn_remove_span_->setEnabled(false);
			return;
		}

		/* Check if selection contains a span */
		bool has_span = grid_preview_->selection_overlaps_span();

		/* Check if selection is a valid mergeable rectangle */
		SelectionRect sr;
		bool mergeable = grid_preview_->selection_is_mergeable(sr);

		/* Update button states */
		btn_add_span_->setEnabled(mergeable && !has_span);
		btn_remove_span_->setEnabled(has_span);

		/* Update info label */
		if (sel.size() == 1) {
			auto [r, c] = *sel.begin();
			if (has_span) {
				grid_span_info_->setText(QStringLiteral("Selected: span cell at %1,%2").arg(r).arg(c));
			} else {
				grid_span_info_->setText(QStringLiteral("Selected: cell %1,%2").arg(r).arg(c));
			}
		} else if (mergeable && !has_span) {
			grid_span_info_->setText(QStringLiteral("Selected: %1x%2 rectangle at %3,%4 - ready to merge")
							 .arg(sr.rowSpan)
							 .arg(sr.colSpan)
							 .arg(sr.row)
							 .arg(sr.col));
		} else if (!mergeable) {
			grid_span_info_->setText(QStringLiteral(
				"Selection is not a valid rectangle (must be contiguous rectangular area)"));
		} else {
			grid_span_info_->setText(QStringLiteral("Selection overlaps existing span - remove it first"));
		}
	});

	right_stack_->addWidget(page_grid_editor_);

	layout->addWidget(right_stack_);
}

void ManagerDialog::setup_settings_tab(QWidget *tab)
{
	auto *layout = new QVBoxLayout(tab);
	layout->setContentsMargins(12, 12, 12, 12);

	auto *gutter_row = new QHBoxLayout();
	gutter_row->addWidget(new QLabel(QStringLiteral("Default Gutter (px):"), tab));
	spin_default_gutter_ = new QSpinBox(tab);
	spin_default_gutter_->setRange(0, 50);
	spin_default_gutter_->setValue(config_->global_settings().defaultGutterPx);
	gutter_row->addWidget(spin_default_gutter_);
	gutter_row->addStretch();
	layout->addLayout(gutter_row);

	auto *gs_apply = new QPushButton(QStringLiteral("Apply"), tab);
	layout->addWidget(gs_apply);
	layout->addStretch();

	connect(gs_apply, &QPushButton::clicked, this, [this]() {
		config_->global_settings().defaultGutterPx = spin_default_gutter_->value();
		config_->save();
		obs_log(LOG_INFO, "global settings saved (gutter=%d)", spin_default_gutter_->value());
		notify_multiview_layout_changed();
	});
}

/* ---- instance list ---- */

void ManagerDialog::refresh_instance_list()
{
	instance_tree_->clear();

	for (auto &inst : config_->instances()) {
		QTreeWidgetItem *parent = nullptr;
		if (!inst.folder.empty())
			parent = find_or_create_folder_item(inst.folder);

		auto *item = new QTreeWidgetItem();
		item->setText(0, QString::fromStdString(inst.name));
		item->setData(0, Qt::UserRole, QString::fromStdString(inst.uuid));

		if (parent)
			parent->addChild(item);
		else
			instance_tree_->addTopLevelItem(item);
	}

	instance_tree_->expandAll();
	update_button_states();
}

QTreeWidgetItem *ManagerDialog::find_or_create_folder_item(const std::string &folder)
{
	/* Check existing top-level items */
	for (int i = 0; i < instance_tree_->topLevelItemCount(); i++) {
		auto *item = instance_tree_->topLevelItem(i);
		if (is_folder_item(item) && item->text(0).toStdString() == folder)
			return item;
	}

	/* Create new folder item */
	auto *item = new QTreeWidgetItem();
	item->setText(0, QString::fromStdString(folder));
	item->setData(0, Qt::UserRole, QStringLiteral("__folder__"));
	item->setFlags(item->flags() | Qt::ItemIsDropEnabled);
	QFont f = item->font(0);
	f.setBold(true);
	item->setFont(0, f);
	instance_tree_->addTopLevelItem(item);
	return item;
}

std::string ManagerDialog::get_item_uuid(QTreeWidgetItem *item) const
{
	if (!item)
		return {};
	return item->data(0, Qt::UserRole).toString().toStdString();
}

bool ManagerDialog::is_folder_item(QTreeWidgetItem *item) const
{
	if (!item)
		return false;
	return item->data(0, Qt::UserRole).toString() == QStringLiteral("__folder__");
}

void ManagerDialog::update_button_states()
{
	auto selected = instance_tree_->selectedItems();

	/* Filter to non-folder items */
	bool has_instance = false;
	for (auto *item : selected) {
		if (!is_folder_item(item)) {
			has_instance = true;
			break;
		}
	}

	btn_clone_->setEnabled(has_instance && selected.size() == 1);
	btn_delete_->setEnabled(!selected.isEmpty());
	btn_open_->setEnabled(has_instance);
	btn_move_up_->setEnabled(has_instance);
	btn_move_down_->setEnabled(has_instance);
}

/* ---- slots ---- */

void ManagerDialog::on_instance_selection_changed()
{
	update_button_states();
	auto selected = instance_tree_->selectedItems();
	if (selected.size() != 1) {
		right_stack_->setCurrentIndex(PAGE_EMPTY);
		return;
	}
	auto *item = selected.first();
	if (is_folder_item(item)) {
		right_stack_->setCurrentIndex(PAGE_EMPTY);
		return;
	}
	std::string uuid = get_item_uuid(item);
	if (!uuid.empty())
		show_instance_detail(uuid);
}

void ManagerDialog::on_new_instance()
{
	bool ok;
	QString name = QInputDialog::getText(this, QStringLiteral("New Instance"), QStringLiteral("Instance name:"),
					     QLineEdit::Normal, QString(), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	MultiviewInstance *inst = config_->add_instance(name.trimmed().toStdString());
	(void)inst;
	config_->save();
	refresh_instance_list();

	/* Select the new item (last instance at root) */
	int count = instance_tree_->topLevelItemCount();
	for (int i = count - 1; i >= 0; i--) {
		auto *item = instance_tree_->topLevelItem(i);
		if (!is_folder_item(item)) {
			instance_tree_->setCurrentItem(item);
			break;
		}
	}
}

void ManagerDialog::on_rename_instance()
{
	auto selected = instance_tree_->selectedItems();
	if (selected.size() != 1)
		return;
	auto *item = selected.first();
	if (is_folder_item(item))
		return;

	std::string uuid = get_item_uuid(item);

	bool ok;
	QString name = QInputDialog::getText(this, QStringLiteral("Rename Instance"), QStringLiteral("New name:"),
					     QLineEdit::Normal, item->text(0), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	config_->rename_instance(uuid, name.trimmed().toStdString());
	config_->save();
	refresh_instance_list();
	notify_multiview_name_changed(uuid);
}

void ManagerDialog::on_clone_instance()
{
	auto selected = instance_tree_->selectedItems();
	if (selected.size() != 1)
		return;
	auto *item = selected.first();
	if (is_folder_item(item))
		return;

	std::string uuid = get_item_uuid(item);

	bool ok;
	QString name = QInputDialog::getText(this, QStringLiteral("Clone Instance"),
					     QStringLiteral("Name for cloned instance:"), QLineEdit::Normal,
					     item->text(0) + QStringLiteral(" (Copy)"), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	config_->clone_instance(uuid, name.trimmed().toStdString());
	config_->save();
	refresh_instance_list();
}

void ManagerDialog::on_delete_instance()
{
	auto selected = instance_tree_->selectedItems();
	if (selected.isEmpty())
		return;

	/* Collect UUIDs of instances to delete */
	QStringList names;
	std::vector<std::string> uuids;
	bool deleting_folders = false;

	for (auto *item : selected) {
		if (is_folder_item(item)) {
			deleting_folders = true;
			/* Delete all instances inside the folder */
			for (int i = 0; i < item->childCount(); i++) {
				auto *child = item->child(i);
				uuids.push_back(get_item_uuid(child));
				names.append(child->text(0));
			}
		} else {
			uuids.push_back(get_item_uuid(item));
			names.append(item->text(0));
		}
	}

	/* Build confirmation message */
	QString msg;
	if (uuids.empty() && deleting_folders) {
		msg = QStringLiteral("Delete empty folder(s)?");
	} else if (uuids.size() == 1) {
		msg = QStringLiteral("Delete instance \"%1\"?").arg(names.first());
	} else if (!uuids.empty()) {
		msg = QStringLiteral("Delete %1 instance(s)?").arg(uuids.size());
	} else {
		return;
	}

	auto ret = QMessageBox::question(this, QStringLiteral("Delete"), msg, QMessageBox::Yes | QMessageBox::No);
	if (ret != QMessageBox::Yes)
		return;

	for (auto &uuid : uuids)
		config_->delete_instance(uuid);

	/* Remove folder tags for deleted folders (clear folder field
	 * for any remaining instances that referenced them) */
	if (deleting_folders) {
		for (auto *item : selected) {
			if (!is_folder_item(item))
				continue;
			std::string folder_name = item->text(0).toStdString();
			for (auto &inst : const_cast<std::vector<MultiviewInstance> &>(config_->instances())) {
				if (inst.folder == folder_name)
					inst.folder.clear();
			}
		}
	}

	config_->save();
	refresh_instance_list();
	right_stack_->setCurrentIndex(PAGE_EMPTY);
}

void ManagerDialog::on_open_instance()
{
	auto selected = instance_tree_->selectedItems();
	for (auto *item : selected) {
		if (is_folder_item(item))
			continue;
		std::string uuid = get_item_uuid(item);
		if (!uuid.empty())
			open_multiview_window(uuid);
	}
}

void ManagerDialog::on_new_folder()
{
	bool ok;
	QString name = QInputDialog::getText(this, QStringLiteral("New Folder"), QStringLiteral("Folder name:"),
					     QLineEdit::Normal, QString(), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	/* Just create the folder node - it persists only if instances use it */
	find_or_create_folder_item(name.trimmed().toStdString());
}

void ManagerDialog::on_move_to_folder()
{
	auto selected = instance_tree_->selectedItems();
	if (selected.isEmpty())
		return;

	/* Gather existing folder names */
	QStringList folders;
	folders.append(QStringLiteral("(Root - No Folder)"));
	for (int i = 0; i < instance_tree_->topLevelItemCount(); i++) {
		auto *item = instance_tree_->topLevelItem(i);
		if (is_folder_item(item))
			folders.append(item->text(0));
	}

	bool ok;
	QString chosen = QInputDialog::getItem(this, QStringLiteral("Move to Folder"), QStringLiteral("Select folder:"),
					       folders, 0, true, &ok);
	if (!ok)
		return;

	std::string target_folder;
	if (chosen != QStringLiteral("(Root - No Folder)"))
		target_folder = chosen.toStdString();

	for (auto *item : selected) {
		if (is_folder_item(item))
			continue;
		std::string uuid = get_item_uuid(item);
		MultiviewInstance *inst = config_->find_instance(uuid);
		if (inst)
			inst->folder = target_folder;
	}
	config_->save();
	refresh_instance_list();
}

void ManagerDialog::on_rename_folder(QTreeWidgetItem *folder_item)
{
	if (!folder_item || !is_folder_item(folder_item))
		return;

	std::string old_name = folder_item->text(0).toStdString();
	bool ok;
	QString new_name = QInputDialog::getText(this, QStringLiteral("Rename Folder"),
						 QStringLiteral("New folder name:"), QLineEdit::Normal,
						 folder_item->text(0), &ok);
	if (!ok || new_name.trimmed().isEmpty())
		return;

	std::string new_folder = new_name.trimmed().toStdString();
	/* Update all instances that belong to this folder */
	for (auto &inst : const_cast<std::vector<MultiviewInstance> &>(config_->instances())) {
		if (inst.folder == old_name)
			inst.folder = new_folder;
	}
	config_->save();
	refresh_instance_list();
}

/* ---- context menu ---- */

void ManagerDialog::show_context_menu(const QPoint &pos)
{
	QMenu menu(this);

	auto *item = instance_tree_->itemAt(pos);
	bool is_on_folder = item && is_folder_item(item);
	bool is_on_instance = item && !is_folder_item(item);
	bool has_selection = !instance_tree_->selectedItems().isEmpty();

	/* Check if selection contains any non-folder items */
	bool sel_has_instance = false;
	for (auto *sel : instance_tree_->selectedItems()) {
		if (!is_folder_item(sel)) {
			sel_has_instance = true;
			break;
		}
	}

	QAction *act_new = menu.addAction(QStringLiteral("New Instance"));
	QAction *act_new_folder = menu.addAction(QStringLiteral("New Folder"));
	menu.addSeparator();

	QAction *act_open = menu.addAction(QStringLiteral("Open"));
	QAction *act_rename = menu.addAction(QStringLiteral("Rename"));
	QAction *act_clone = menu.addAction(QStringLiteral("Clone"));
	QAction *act_move = menu.addAction(QStringLiteral("Move to Folder..."));
	menu.addSeparator();
	QAction *act_delete = menu.addAction(QStringLiteral("Delete"));

	act_open->setEnabled(is_on_instance);
	act_rename->setEnabled(is_on_instance || is_on_folder);
	act_clone->setEnabled(is_on_instance);
	act_move->setEnabled(sel_has_instance && !is_on_folder);
	act_delete->setEnabled(has_selection);

	QAction *chosen = menu.exec(instance_tree_->mapToGlobal(pos));
	if (!chosen)
		return;

	if (chosen == act_new)
		on_new_instance();
	else if (chosen == act_new_folder)
		on_new_folder();
	else if (chosen == act_open)
		on_open_instance();
	else if (chosen == act_rename) {
		if (is_on_folder)
			on_rename_folder(item);
		else
			on_rename_instance();
	} else if (chosen == act_clone)
		on_clone_instance();
	else if (chosen == act_move)
		on_move_to_folder();
	else if (chosen == act_delete)
		on_delete_instance();
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
	detail_uuid_label_->setText(QStringLiteral("UUID: %1").arg(QString::fromStdString(inst->uuid)));
	detail_layout_label_->setText(
		QStringLiteral("Layout: %1 x %2").arg(inst->layout.rows).arg(inst->layout.columns));

	/* Block signals to avoid triggering save during UI update */
	detail_use_global_gutter_->blockSignals(true);
	detail_gutter_spin_->blockSignals(true);

	detail_use_global_gutter_->setChecked(inst->useGlobalGutter);
	detail_gutter_spin_->setValue(inst->layout.gutterPx);
	detail_gutter_spin_->setEnabled(!inst->useGlobalGutter);

	int eff = inst->effective_gutter(config_->global_settings().defaultGutterPx);
	detail_gutter_effective_->setText(QStringLiteral("Effective gutter: %1px").arg(eff));

	detail_use_global_gutter_->blockSignals(false);
	detail_gutter_spin_->blockSignals(false);

	right_stack_->setCurrentIndex(PAGE_INSTANCE_DETAIL);
}

void ManagerDialog::on_edit_grid_clicked()
{
	if (current_detail_uuid_.empty())
		return;
	show_grid_editor(current_detail_uuid_);
}

void ManagerDialog::show_grid_editor(const std::string &uuid)
{
	MultiviewInstance *inst = config_->find_instance(uuid);
	if (!inst)
		return;

	grid_edit_uuid_ = uuid;
	grid_edit_layout_ = inst->layout;
	/* Grid editor uses gutter=0 for seamless preview */
	grid_edit_layout_.gutterPx = 0;

	grid_editor_title_->setText(QStringLiteral("Edit Grid: %1").arg(QString::fromStdString(inst->name)));

	grid_rows_spin_->blockSignals(true);
	grid_cols_spin_->blockSignals(true);

	grid_rows_spin_->setValue(grid_edit_layout_.rows);
	grid_cols_spin_->setValue(grid_edit_layout_.columns);

	grid_rows_spin_->blockSignals(false);
	grid_cols_spin_->blockSignals(false);

	grid_span_info_->setText(QStringLiteral("%1 span(s)").arg(grid_edit_layout_.spans.size()));
	btn_add_span_->setEnabled(false);
	btn_remove_span_->setEnabled(false);

	update_grid_preview();
	right_stack_->setCurrentIndex(PAGE_GRID_EDITOR);
}

void ManagerDialog::update_grid_preview()
{
	grid_preview_->set_layout(grid_edit_layout_);
	grid_span_info_->setText(QStringLiteral("%1 span(s)").arg(grid_edit_layout_.spans.size()));
}
