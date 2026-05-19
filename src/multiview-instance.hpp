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

#include <string>
#include <vector>

#include <obs-data.h>

struct CellAssignment {
	int row = -1;     // grid row position (-1 = legacy/unset)
	int col = -1;     // grid col position (-1 = legacy/unset)
	std::string type; // "pgm", "prvw", "scene", "source", ""
	std::string name; // scene/source name, empty for pgm/prvw/empty

	obs_data_t *to_obs_data() const;
	static CellAssignment from_obs_data(obs_data_t *data);
};

struct SpanRegion {
	int row;
	int col;
	int rowSpan;
	int colSpan;

	obs_data_t *to_obs_data() const;
	static SpanRegion from_obs_data(obs_data_t *data);
};

struct LayoutData {
	int rows = 4;
	int columns = 4;
	int gutterPx = 4;
	std::vector<SpanRegion> spans;

	obs_data_t *to_obs_data() const;
	static LayoutData from_obs_data(obs_data_t *data);
};

struct MultiviewInstance {
	std::string uuid;
	std::string name;
	std::string folder; /* UI-only grouping tag, empty = root */
	LayoutData layout;
	std::vector<CellAssignment> cellAssignments;

	bool useGlobalGutter = true;
	bool layoutDirty = false;
	bool signalDirty = false;

	int effective_gutter(int globalGutter) const;

	obs_data_t *to_obs_data() const;
	static MultiviewInstance from_obs_data(obs_data_t *data);

	static MultiviewInstance create_new(const std::string &name);
	MultiviewInstance clone_instance(const std::string &newName) const;
};

struct LayoutPreset {
	std::string uuid;
	std::string name;
	LayoutData layout;

	obs_data_t *to_obs_data() const;
	static LayoutPreset from_obs_data(obs_data_t *data);
};

struct GlobalSettings {
	int defaultGutterPx = 4;

	obs_data_t *to_obs_data() const;
	static GlobalSettings from_obs_data(obs_data_t *data);
};
