#pragma once
const char *const slice_config_entry = "Metadata/nonplanar_slice_tool.config";

void load_slice_config(const std::string &path, DynamicPrintConfig &config, bool &project_settings)
{
    mz_zip_archive archive{};
    if (!open_zip_reader(&archive, path))
        throw std::runtime_error("cannot read " + path);
    ScopeGuard close([&] { close_zip_reader(&archive); });
    if (mz_zip_reader_locate_file(&archive, slice_config_entry, nullptr, 0) < 0)
        return;
    size_t size = 0;
    void *data = mz_zip_reader_extract_file_to_heap(&archive, slice_config_entry, &size, 0);
    if (!data)
        throw std::runtime_error("cannot read saved slice configuration: " + path);
    ScopeGuard free([&] { mz_free(data); });
    const auto saved = nlohmann::json::parse(static_cast<const char *>(data), static_cast<const char *>(data) + size);
    if (saved.at("schema") != "orca.nonplanar_slice_tool.config.v1")
        throw std::runtime_error("unsupported saved slice configuration: " + path);
    DynamicPrintConfig loaded;
    for (const auto &[key, value] : saved.at("settings").items()) {
        ConfigOption *option = loaded.option(key, true);
        if (!option || !option->deserialize(value.get<std::string>()))
            throw std::runtime_error("invalid saved slice option: " + key);
    }
    config = std::move(loaded);
    project_settings = saved.at("project_settings").get<bool>();
}

void repair_project_flush(DynamicPrintConfig &config)
{
    const size_t slots = config.option<ConfigOptionStrings>("filament_colour")->size();
    const size_t nozzles = config.option<ConfigOptionFloats>("nozzle_diameter")->size();
    auto *matrix = config.option<ConfigOptionFloats>("flush_volumes_matrix");
    auto *volumes = config.option<ConfigOptionFloats>("flush_volumes_vector");
    auto *multipliers = config.option<ConfigOptionFloats>("flush_multiplier");
    if (!slots || !nozzles)
        throw std::runtime_error("invalid project flush data: filament colours and nozzle diameters must not be empty");
    if (slots == 1 && !multipliers->values.empty())
        return;
    if (matrix->size() == slots * slots * nozzles && volumes->size() == 2 * slots && multipliers->size() == nozzles)
        return;
    PresetBundle repair;
    repair.printers.get_edited_preset().config = config;
    repair.filament_presets.assign(std::max(slots, nozzles), std::string());
    repair.project_config.apply_only(config, {"flush_volumes_matrix", "flush_volumes_vector", "flush_multiplier"});
    auto &values = repair.project_config.option<ConfigOptionFloats>("flush_volumes_vector")->values;
    const size_t old_size = values.size();
    const double unload = old_size > 0 ? values[0] : 140., load = old_size > 1 ? values[1] : 140.;
    values.resize(2 * slots);
    for (size_t i = old_size; i < values.size(); ++i)
        values[i] = i % 2 ? load : unload;
    auto &old_multipliers = repair.project_config.option<ConfigOptionFloats>("flush_multiplier")->values;
    if (old_multipliers.empty()) old_multipliers.assign(nozzles, 1.);
    repair.update_multi_material_filament_presets();
    config.apply_only(repair.project_config, {"flush_volumes_matrix", "flush_volumes_vector", "flush_multiplier"});
    config.set_num_filaments(unsigned(slots));
    std::cerr << "warning: resized invalid project flush data for " << slots << " filament slots\n";
}

void complete_project_ranges(Model &model, const DynamicPrintConfig &config)
{
    for (ModelObject *object : model.objects)
        for (auto &[range, settings] : object->layer_config_ranges)
            if (!settings.has("layer_height")) {
                const ConfigOption *height = object->config.option("layer_height");
                settings.set_key_value("layer_height", (height ? height : config.option("layer_height"))->clone());
            }
}

bool preserves_project_geometry(const Args &args)
{
    return boost::algorithm::iequals(fs::path(args.model).extension().string(), ".3mf") &&
           !args.center_set && !args.auto_orient && args.parts.empty() && args.paint.empty() &&
           args.events.empty() && args.layer_profile.empty() && args.calib.empty();
}

void save_source_project(const Args &args, const DynamicPrintConfig &config, bool project_settings)
{
    nlohmann::json settings = nlohmann::json::object();
    for (const std::string &key : config.keys())
        settings[key] = config.opt_serialize(key);
    if (const auto *head = config.option<ConfigOptionString>("nonplanar_head"); head && !head->value.empty()) {
        PrintConfig printer;
        printer.apply(config, true);
        const std::string envelope = Nonplanar::save_head_envelope(Nonplanar::resolve_head_envelope(printer));
        const ConfigOptionString frozen(nlohmann::json{{"reference", head->value},
            {"envelope", nlohmann::json::parse(envelope)}}.dump());
        settings["nonplanar_head_snapshot"] = frozen.serialize();
    }
    const std::string snapshot = nlohmann::json{{"schema", "orca.nonplanar_slice_tool.config.v1"},
        {"project_settings", project_settings}, {"settings", std::move(settings)}}.dump() + "\n";
    mz_zip_archive source{}, target{};
    if (!open_zip_reader(&source, args.model))
        throw std::runtime_error("cannot read " + args.model);
    ScopeGuard close_source([&] { close_zip_reader(&source); });
    const fs::path temporary = fs::path(args.save_3mf).parent_path() / fs::unique_path(".nonplanar-save-%%%%%%%%.3mf");
    ScopeGuard remove_temporary([&] { boost::system::error_code ec; fs::remove(temporary, ec); });
    if (!open_zip_writer(&target, temporary.string()))
        throw std::runtime_error("cannot write " + args.save_3mf);
    ScopeGuard close_target([&] { close_zip_writer(&target); });
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&source); ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&source, i, &stat))
            throw std::runtime_error("cannot read project entry: " + args.model);
        if (std::string(stat.m_filename) == slice_config_entry)
            continue;
        if (!mz_zip_writer_add_from_zip_reader(&target, &source, i))
            throw std::runtime_error("cannot retain project entry: " + std::string(stat.m_filename));
    }
    MZ_TIME_T modified = 315619200;
    if (!mz_zip_writer_add_mem_ex_v2(&target, slice_config_entry, snapshot.data(), snapshot.size(), nullptr, 0,
                                    MZ_DEFAULT_COMPRESSION, 0, 0, &modified, nullptr, 0, nullptr, 0) ||
        !mz_zip_writer_finalize_archive(&target))
        throw std::runtime_error("cannot finalize " + args.save_3mf);
    close_zip_writer(&target);
    close_target.reset();
    close_zip_reader(&source);
    close_source.reset();
    if (const std::error_code error = Slic3r::rename_file(temporary.string(), args.save_3mf))
        throw std::runtime_error("cannot replace " + args.save_3mf + ": " + error.message());
}
