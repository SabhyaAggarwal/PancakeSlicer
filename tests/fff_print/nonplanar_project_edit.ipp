#pragma once
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void edit_project(const std::string &path, Model &model, const DynamicPrintConfig &config, const PlateDataPtrs &plates,
                  ContextInstances &membership)
{
    boost::nowide::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    const nlohmann::json edits = nlohmann::json::parse(in).at("edits");
    PlateFiles           files{ fs::path(path).parent_path(), false, {} };
    std::map<int, ModelObject *> objects;
    std::map<std::pair<int, int>, ModelInstance *> instances;
    for (ModelObject *object : model.objects)
        for (ModelInstance *instance : object->instances)
            if (const auto it = membership.find(instance->id()); it != membership.end() && it->second.at("saved_object_id").is_number_integer()) {
                const int id = it->second.at("saved_object_id").get<int>();
                objects[id] = object;
                instances[{ id, it->second.at("saved_instance_id").get<int>() }] = instance;
            }
    auto object_of = [&](const nlohmann::json &edit) {
        const auto it = objects.find(edit.at("object").get<int>());
        if (it == objects.end())
            throw std::runtime_error("--edit: no object with saved id " + edit.at("object").dump());
        return it->second;
    };
    auto plate_of = [&](const nlohmann::json &edit, int fallback) {
        const int plate = edit.value("plate", fallback);
        if (std::none_of(plates.begin(), plates.end(), [plate](const PlateData *p) { return p && p->plate_index + 1 == plate; }))
            throw std::runtime_error("--edit: no plate " + std::to_string(plate) + " in this project");
        return plate;
    };
    auto join = [&](const ModelInstance *instance, int plate) {
        const auto name = std::find_if(plates.begin(), plates.end(), [plate](const PlateData *p) { return p && p->plate_index + 1 == plate; });
        membership[instance->id()] = { { "saved_object_id", nullptr }, { "saved_instance_id", nullptr }, { "source_plate_index", plate },
                                       { "source_plate_name", (*name)->plate_name }, { "saved_transform", nullptr } };
    };
    std::set<ModelInstance *> deleted_instances;
    std::set<ModelObject *>   deleted_objects;
    for (const nlohmann::json &edit : edits) {
        const std::string op = edit.at("op").get<std::string>();
        if (op == "add_object") {
            ModelObject *object = add_plate_object(model, edit, files, config);
            const int    plate  = plate_of(edit, 1);
            add_placed_instances(object, edit, context_plate_origin(config, plates, plate));
            for (const ModelInstance *instance : object->instances)
                join(instance, plate);
        } else if (op == "add_volume") {
            ModelObject *object = object_of(edit);
            add_plate_volume(*object, edit, files);
            object->sort_volumes(true);
            object->invalidate_bounding_box();
        } else if (op == "add_instance") {
            ModelObject *object = object_of(edit);
            const auto   first  = membership.find(object->instances.front()->id());
            const int    plate  = plate_of(edit, first != membership.end() && first->second.at("source_plate_index").is_number_integer() ?
                                                     first->second.at("source_plate_index").get<int>() : 1);
            ModelInstance *instance = object->add_instance(*object->instances.front());
            if (edit.contains("rot"))
                instance->set_rotation(Z, Geometry::deg2rad(edit.at("rot").get<double>()));
            instance->printable = edit.value("printable", true);
            const BoundingBoxf3 box    = object->instance_bounding_box(object->instances.size() - 1);
            const Vec3d         origin = context_plate_origin(config, plates, plate);
            instance->set_offset(instance->get_offset() + Vec3d(origin.x() + edit.at("x").get<double>() - box.center().x(),
                                                                origin.y() + edit.at("y").get<double>() - box.center().y(), 0.));
            object->invalidate_bounding_box();
            join(instance, plate);
        } else if (op == "delete") {
            ModelObject *object = object_of(edit);
            if (!edit.contains("instance"))
                deleted_objects.insert(object);
            else {
                const auto it = instances.find({ edit.at("object").get<int>(), edit.at("instance").get<int>() });
                if (it == instances.end())
                    throw std::runtime_error("--edit: object " + edit.at("object").dump() + " has no saved instance " + edit.at("instance").dump());
                deleted_instances.insert(it->second);
            }
        } else if (op == "set_paint") {
            ModelObject      *object = object_of(edit);
            const std::string where  = "--edit set_paint object " + edit.at("object").dump();
            ModelVolume      *volume = nullptr;
            if (edit.contains("volume")) {
                const int index = edit.at("volume").get<int>();
                if (index < 0 || index >= int(object->volumes.size()))
                    throw std::runtime_error(where + ": no volume " + std::to_string(index) + " (the object has " +
                                             std::to_string(object->volumes.size()) + ")");
                volume = object->volumes[index];
            } else {
                for (ModelVolume *v : object->volumes)
                    if (v->is_model_part()) {
                        if (volume)
                            throw std::runtime_error(where + ": the object has several parts; give \"volume\"");
                        volume = v;
                    }
            }
            if (!volume || !volume->is_model_part())
                throw std::runtime_error(where + ": paint goes on a part");
            if (!edit.contains("paint") || !edit.at("paint").is_object())
                throw std::runtime_error(where + ": paint is a map of kind to triangle paint strings");
            apply_plate_paint(*volume, edit.at("paint"), where);
        } else if (op == "set_extruder") {
            const int extruder = edit.at("extruder").get<int>();
            const int filaments = int(config.option<ConfigOptionStrings>("filament_colour")->size());
            if (extruder < 1 || extruder > filaments)
                throw std::runtime_error("--edit: extruder " + std::to_string(extruder) + " is not one of the project's " +
                                         std::to_string(filaments) + " filaments");
            object_of(edit)->config.set("extruder", extruder);
        } else
            throw std::runtime_error("--edit: unknown op " + op);
    }
    for (size_t o = model.objects.size(); o-- > 0;) {
        ModelObject *object = model.objects[o];
        for (size_t i = object->instances.size(); i-- > 0;)
            if (deleted_instances.count(object->instances[i])) {
                membership.erase(object->instances[i]->id());
                object->delete_instance(i);
            }
        if (deleted_objects.count(object) || object->instances.empty()) {
            for (const ModelInstance *instance : object->instances)
                membership.erase(instance->id());
            model.delete_object(o);
        } else
            object->invalidate_bounding_box();
    }
    if (model.objects.empty())
        throw std::runtime_error("--edit deletes every object of the project");
}

}
