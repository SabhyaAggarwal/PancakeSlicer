#ifndef slic3r_Utils_UndoRedo_hpp_
#define slic3r_Utils_UndoRedo_hpp_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cassert>

#include <libslic3r/ObjectID.hpp>
#include <libslic3r/Config.hpp>

typedef double                          coordf_t;
typedef std::pair<coordf_t, coordf_t>   t_layer_height_range;

namespace Slic3r {

class Model;

namespace GUI {
	class Selection;
    class GLGizmosManager;
	class PartPlateList;
	class PartPlate;
}

namespace UndoRedo {

enum class SnapshotType : unsigned char {
	Action,
	GizmoAction,
	Selection,
	ProjectSeparator,
	EnteringGizmo,
	LeavingGizmoNoAction,
	LeavingGizmoWithAction,
};

struct SnapshotData
{
	SnapshotType        snapshot_type;
	PrinterTechnology 	printer_technology { ptUnknown };
	unsigned int        flags { 0 };
    int                 layer_range_idx { -1 };

	enum Flags {
		VARIABLE_LAYER_EDITING_ACTIVE = 1,
		SELECTED_SETTINGS_ON_SIDEBAR  = 2,
		SELECTED_LAYERROOT_ON_SIDEBAR = 4,
		SELECTED_LAYER_ON_SIDEBAR     = 8,
        RECALCULATE_SLA_SUPPORTS      = 16
	};
};

struct Snapshot
{
	Snapshot(size_t timestamp) : timestamp(timestamp) {}
	Snapshot(const std::string &name, size_t timestamp, size_t model_id, const SnapshotData &snapshot_data) :
		name(name), timestamp(timestamp), model_id(model_id), snapshot_data(snapshot_data) {}
	
	std::string 		name;
	size_t 				timestamp;
	size_t 				model_id;
	SnapshotData  		snapshot_data;

	bool		operator< (const Snapshot &rhs) const { return this->timestamp < rhs.timestamp; }
	bool		operator==(const Snapshot &rhs) const { return this->timestamp == rhs.timestamp; }

	bool 		is_topmost() const;
	bool 		is_topmost_captured() const { assert(this->is_topmost()); return model_id > 0; }

};

inline bool snapshot_modifies_project(SnapshotType type)
{
	return type == SnapshotType::Action || type == SnapshotType::GizmoAction || type == SnapshotType::ProjectSeparator;
}

inline bool snapshot_modifies_project(const Snapshot &snapshot)
{
	return snapshot_modifies_project(snapshot.snapshot_data.snapshot_type) && (snapshot.name.empty() || snapshot.name.back() != '!');
}

struct Selection : public Slic3r::ObjectBase {
	void clear() { mode = 0; volumes_and_instances.clear(); }
	unsigned char							mode = 0;
	std::vector<std::pair<size_t, size_t>>	volumes_and_instances;
	template<class Archive> void serialize(Archive &ar) { ar(mode, volumes_and_instances); }
};

class StackImpl;

class Stack
{
public:
	Stack();
	~Stack();

	void clear();
	bool empty() const;

	void set_memory_limit(size_t memsize);
	size_t get_memory_limit() const;

	size_t memsize() const;

	void release_least_recently_used();

    void take_snapshot(const std::string& snapshot_name, const Slic3r::Model& model, const Slic3r::GUI::Selection& selection, const Slic3r::GUI::GLGizmosManager& gizmos, const SnapshotData &snapshot_data);

    void take_snapshot(const std::string& snapshot_name, const Slic3r::Model& model, const Slic3r::GUI::Selection& selection, const Slic3r::GUI::GLGizmosManager& gizmos, const Slic3r::GUI::PartPlateList& plate_list, const SnapshotData& snapshot_data);

    void reduce_noisy_snapshots(const std::string& new_name);

	bool has_undo_snapshot() const;
	bool has_redo_snapshot() const;
	bool has_undo_snapshot(size_t time_to_load) const;

    bool undo(Slic3r::Model& model, const Slic3r::GUI::Selection& selection, Slic3r::GUI::GLGizmosManager& gizmos, Slic3r::GUI::PartPlateList& plate_list, const SnapshotData &snapshot_data, size_t time_to_load = SIZE_MAX);

    bool redo(Slic3r::Model& model, Slic3r::GUI::GLGizmosManager& gizmos, Slic3r::GUI::PartPlateList& plate_list, size_t time_to_load = SIZE_MAX);

	const std::vector<Snapshot>& snapshots() const;
	const Snapshot& 		     snapshot(size_t time) const;

	size_t active_snapshot_time() const;
	const Snapshot& active_snapshot() const { return this->snapshot(this->active_snapshot_time()); }
	bool   temp_snapshot_active() const;

    void   mark_current_as_saved();
    bool   project_modified() const;
	bool has_real_change_from(size_t time) const;

	const Selection& selection_deserialized() const;

private:
	friend class StackImpl;
	std::unique_ptr<StackImpl> 	pimpl;
};

};
};

#endif
