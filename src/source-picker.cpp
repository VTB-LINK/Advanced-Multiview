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

#include "source-picker.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

SourcePicker::SourcePicker(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(QStringLiteral("Select Source"));
	setMinimumSize(360, 400);
	resize(400, 500);

	auto *mainLayout = new QVBoxLayout(this);

	/* Filter */
	auto *filterLayout = new QHBoxLayout;
	filterLayout->addWidget(new QLabel(QStringLiteral("Filter:")));
	filter_edit_ = new QLineEdit;
	filter_edit_->setPlaceholderText(
		QStringLiteral("Type to filter..."));
	filterLayout->addWidget(filter_edit_);
	mainLayout->addLayout(filterLayout);

	/* Tabs: Special | Scenes | Sources */
	tabs_ = new QTabWidget;
	mainLayout->addWidget(tabs_);

	special_list_ = new QListWidget;
	tabs_->addTab(special_list_, QStringLiteral("Special"));

	scene_list_ = new QListWidget;
	tabs_->addTab(scene_list_, QStringLiteral("Scenes"));

	source_list_ = new QListWidget;
	tabs_->addTab(source_list_, QStringLiteral("Sources"));

	/* Buttons */
	auto *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok |
				     QDialogButtonBox::Cancel);
	mainLayout->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::accepted, this,
		&SourcePicker::on_accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(filter_edit_, &QLineEdit::textChanged, this,
		&SourcePicker::on_filter_changed);
	connect(special_list_, &QListWidget::itemDoubleClicked, this,
		&SourcePicker::on_item_double_clicked);
	connect(scene_list_, &QListWidget::itemDoubleClicked, this,
		&SourcePicker::on_item_double_clicked);
	connect(source_list_, &QListWidget::itemDoubleClicked, this,
		&SourcePicker::on_item_double_clicked);

	populate_list();
}

void SourcePicker::populate_list()
{
	/* Special entries */
	auto *pgmItem = new QListWidgetItem(QStringLiteral("Program (PGM)"));
	pgmItem->setData(Qt::UserRole, QStringLiteral("pgm"));
	pgmItem->setData(Qt::UserRole + 1, QString());
	special_list_->addItem(pgmItem);

	auto *prvwItem = new QListWidgetItem(QStringLiteral("Preview (PRVW)"));
	prvwItem->setData(Qt::UserRole, QStringLiteral("prvw"));
	prvwItem->setData(Qt::UserRole + 1, QString());
	special_list_->addItem(prvwItem);

	/* Scenes */
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *src = scenes.sources.array[i];
		const char *name = obs_source_get_name(src);
		if (!name)
			continue;

		auto *item = new QListWidgetItem(QString::fromUtf8(name));
		item->setData(Qt::UserRole, QStringLiteral("scene"));
		item->setData(Qt::UserRole + 1, QString::fromUtf8(name));
		scene_list_->addItem(item);
	}

	obs_frontend_source_list_free(&scenes);

	/* Sources (non-scene) */
	auto enum_cb = [](void *param, obs_source_t *src) -> bool {
		auto *list = static_cast<QListWidget *>(param);

		/* Skip scenes, they're already in scene tab */
		if (obs_source_get_type(src) == OBS_SOURCE_TYPE_SCENE)
			return true;

		/* Only show video-capable sources */
		uint32_t flags = obs_source_get_output_flags(src);
		if (!(flags & OBS_SOURCE_VIDEO))
			return true;

		const char *name = obs_source_get_name(src);
		if (!name || name[0] == '\0')
			return true;

		auto *item = new QListWidgetItem(QString::fromUtf8(name));
		item->setData(Qt::UserRole, QStringLiteral("source"));
		item->setData(Qt::UserRole + 1, QString::fromUtf8(name));
		list->addItem(item);

		return true;
	};

	obs_enum_sources(enum_cb, source_list_);
}

void SourcePicker::on_filter_changed(const QString &text)
{
	auto filter_list = [&text](QListWidget *list) {
		for (int i = 0; i < list->count(); i++) {
			auto *item = list->item(i);
			bool visible =
				text.isEmpty() ||
				item->text().contains(text,
						      Qt::CaseInsensitive);
			item->setHidden(!visible);
		}
	};

	filter_list(special_list_);
	filter_list(scene_list_);
	filter_list(source_list_);
}

void SourcePicker::on_item_double_clicked(QListWidgetItem *item)
{
	Q_UNUSED(item);
	on_accept();
}

void SourcePicker::on_accept()
{
	QListWidget *activeList = nullptr;
	int idx = tabs_->currentIndex();
	if (idx == 0)
		activeList = special_list_;
	else if (idx == 1)
		activeList = scene_list_;
	else
		activeList = source_list_;

	auto *current = activeList->currentItem();
	if (!current) {
		reject();
		return;
	}

	result_.type = current->data(Qt::UserRole).toString().toStdString();
	result_.name =
		current->data(Qt::UserRole + 1).toString().toStdString();

	accept();
}
