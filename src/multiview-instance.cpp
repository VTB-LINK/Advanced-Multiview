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

#include "multiview-instance.hpp"

#include <QUuid>
#include <obs-data.h>
#include <plugin-support.h>

/* ---------- CellAssignment ---------- */

obs_data_t *CellAssignment::to_obs_data() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, "row", row);
	obs_data_set_int(data, "col", col);
	obs_data_set_string(data, "type", type.c_str());
	obs_data_set_string(data, "name", name.c_str());
	return data;
}

CellAssignment CellAssignment::from_obs_data(obs_data_t *data)
{
	CellAssignment ca;
	ca.row = (int)obs_data_get_int(data, "row");
	ca.col = (int)obs_data_get_int(data, "col");
	/* Legacy data without row/col will read as 0,0 - we use a sentinel
	 * to detect this case: if "row" key doesn't exist, mark as -1 */
	if (!obs_data_has_user_value(data, "row"))
		ca.row = -1;
	if (!obs_data_has_user_value(data, "col"))
		ca.col = -1;
	ca.type = obs_data_get_string(data, "type");
	ca.name = obs_data_get_string(data, "name");
	return ca;
}

/* ---------- SpanRegion ---------- */

obs_data_t *SpanRegion::to_obs_data() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, "row", row);
	obs_data_set_int(data, "col", col);
	obs_data_set_int(data, "rowSpan", rowSpan);
	obs_data_set_int(data, "colSpan", colSpan);
	return data;
}

SpanRegion SpanRegion::from_obs_data(obs_data_t *data)
{
	SpanRegion s;
	s.row = (int)obs_data_get_int(data, "row");
	s.col = (int)obs_data_get_int(data, "col");
	s.rowSpan = (int)obs_data_get_int(data, "rowSpan");
	s.colSpan = (int)obs_data_get_int(data, "colSpan");
	if (s.rowSpan < 1)
		s.rowSpan = 1;
	if (s.colSpan < 1)
		s.colSpan = 1;
	return s;
}

/* ---------- LayoutData ---------- */

obs_data_t *LayoutData::to_obs_data() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, "rows", rows);
	obs_data_set_int(data, "columns", columns);
	obs_data_set_int(data, "gutterPx", gutterPx);

	obs_data_array_t *arr = obs_data_array_create();
	for (auto &s : spans) {
		obs_data_t *item = s.to_obs_data();
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "spans", arr);
	obs_data_array_release(arr);

	return data;
}

LayoutData LayoutData::from_obs_data(obs_data_t *data)
{
	LayoutData ld;
	ld.rows = (int)obs_data_get_int(data, "rows");
	ld.columns = (int)obs_data_get_int(data, "columns");
	ld.gutterPx = (int)obs_data_get_int(data, "gutterPx");

	if (ld.rows < 1)
		ld.rows = 4;
	if (ld.rows > 10)
		ld.rows = 10;
	if (ld.columns < 1)
		ld.columns = 4;
	if (ld.columns > 10)
		ld.columns = 10;
	if (ld.gutterPx < 0)
		ld.gutterPx = 0;
	if (ld.gutterPx > 50)
		ld.gutterPx = 50;

	obs_data_array_t *arr = obs_data_get_array(data, "spans");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(arr, i);
			ld.spans.push_back(SpanRegion::from_obs_data(item));
			obs_data_release(item);
		}
		obs_data_array_release(arr);
	}

	return ld;
}

/* ---------- MultiviewInstance ---------- */

obs_data_t *MultiviewInstance::to_obs_data() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "uuid", uuid.c_str());
	obs_data_set_string(data, "name", name.c_str());
	if (!folder.empty())
		obs_data_set_string(data, "folder", folder.c_str());
	obs_data_set_bool(data, "useGlobalGutter", useGlobalGutter);

	obs_data_t *layoutData = layout.to_obs_data();
	obs_data_set_obj(data, "layout", layoutData);
	obs_data_release(layoutData);

	obs_data_array_t *arr = obs_data_array_create();
	for (auto &ca : cellAssignments) {
		if (ca.type.empty())
			continue; /* Don't save empty assignments */
		obs_data_t *item = ca.to_obs_data();
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "cellAssignments", arr);
	obs_data_array_release(arr);

	return data;
}

MultiviewInstance MultiviewInstance::from_obs_data(obs_data_t *data)
{
	MultiviewInstance inst;
	inst.uuid = obs_data_get_string(data, "uuid");
	inst.name = obs_data_get_string(data, "name");
	inst.folder = obs_data_get_string(data, "folder");

	if (obs_data_has_user_value(data, "useGlobalGutter"))
		inst.useGlobalGutter = obs_data_get_bool(data, "useGlobalGutter");
	else
		inst.useGlobalGutter = true;

	obs_data_t *layoutData = obs_data_get_obj(data, "layout");
	if (layoutData) {
		inst.layout = LayoutData::from_obs_data(layoutData);
		obs_data_release(layoutData);
	}

	obs_data_array_t *arr = obs_data_get_array(data, "cellAssignments");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(arr, i);
			CellAssignment ca = CellAssignment::from_obs_data(item);
			obs_data_release(item);
			/* Skip empty assignments */
			if (ca.type.empty())
				continue;
			/* Legacy migration: if row/col not stored, compute from flat index */
			if (ca.row < 0 || ca.col < 0) {
				int cols = inst.layout.columns;
				if (cols < 1)
					cols = 4;
				ca.row = (int)i / cols;
				ca.col = (int)i % cols;
			}
			inst.cellAssignments.push_back(ca);
		}
		obs_data_array_release(arr);
	}

	return inst;
}

MultiviewInstance MultiviewInstance::create_new(const std::string &instanceName)
{
	MultiviewInstance inst;
	inst.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
	inst.name = instanceName;
	inst.useGlobalGutter = true;
	return inst;
}

int MultiviewInstance::effective_gutter(int globalGutter) const
{
	return useGlobalGutter ? globalGutter : layout.gutterPx;
}

MultiviewInstance MultiviewInstance::clone_instance(const std::string &newName) const
{
	MultiviewInstance cloned = *this;
	cloned.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
	cloned.name = newName;
	cloned.layoutDirty = false;
	cloned.signalDirty = false;
	return cloned;
}

/* ---------- LayoutPreset ---------- */

obs_data_t *LayoutPreset::to_obs_data() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "uuid", uuid.c_str());
	obs_data_set_string(data, "name", name.c_str());

	obs_data_t *layoutData = layout.to_obs_data();
	obs_data_set_obj(data, "layout", layoutData);
	obs_data_release(layoutData);

	return data;
}

LayoutPreset LayoutPreset::from_obs_data(obs_data_t *data)
{
	LayoutPreset lp;
	lp.uuid = obs_data_get_string(data, "uuid");
	lp.name = obs_data_get_string(data, "name");

	obs_data_t *layoutData = obs_data_get_obj(data, "layout");
	if (layoutData) {
		lp.layout = LayoutData::from_obs_data(layoutData);
		obs_data_release(layoutData);
	}

	return lp;
}

/* ---------- GlobalSettings ---------- */

obs_data_t *GlobalSettings::to_obs_data() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, "defaultGutterPx", defaultGutterPx);
	return data;
}

GlobalSettings GlobalSettings::from_obs_data(obs_data_t *data)
{
	GlobalSettings gs;
	gs.defaultGutterPx = (int)obs_data_get_int(data, "defaultGutterPx");
	if (gs.defaultGutterPx < 0)
		gs.defaultGutterPx = 0;
	if (gs.defaultGutterPx > 50)
		gs.defaultGutterPx = 50;
	return gs;
}
