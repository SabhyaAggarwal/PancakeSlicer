#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <boost/algorithm/string/predicate.hpp>
#include <limits>
#include <miniz.h>
#include <string.h>
#include <map>
#include <string>
#include <expat.h>

#include <boost/nowide/cstdio.hpp>

#include "../Exception.hpp"
#include "../Model.hpp"
#include "../Geometry.hpp"
#include "../LocalesUtils.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "AMF.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <vector>
#include <utility>

#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include "miniz_extension.hpp"

#if 0
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r
{

#define L(s) (s)
#define _(s) Slic3r::I18N::translate(s)

struct AMFParserContext
{
    AMFParserContext(XML_Parser parser, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, Model* model) :
        m_parser(parser),
        m_model(*model),
        m_config(config),
        m_config_substitutions(config_substitutions)
    {
        m_path.reserve(12);
    }

    void stop(const std::string &msg = std::string())
    {
        assert(! m_error);
        assert(m_error_message.empty());
        m_error = true;
        m_error_message = msg;
        XML_StopParser(m_parser, 0);
    }

    bool        error()         const { return m_error; }
    const char* error_message() const {
        return m_error ?
            (m_error_message.empty() ? "Parse AMF file failed" : m_error_message.c_str()) :
            XML_ErrorString(XML_GetErrorCode(m_parser));
    }

    void startElement(const char *name, const char **atts);
    void endElement(const char *name);
    void endDocument();
    void characters(const XML_Char *s, int len);

    static void XMLCALL startElement(void *userData, const char *name, const char **atts)
    {
        AMFParserContext *ctx = (AMFParserContext*)userData;
        ctx->startElement(name, atts);
    }

    static void XMLCALL endElement(void *userData, const char *name)
    {
        AMFParserContext *ctx = (AMFParserContext*)userData;
        ctx->endElement(name);
    }

    static void XMLCALL characters(void *userData, const XML_Char *s, int len)
    {
        AMFParserContext *ctx = (AMFParserContext*)userData;
        ctx->characters(s, len);
    }

    static const char* get_attribute(const char **atts, const char *id) {
        if (atts == nullptr)
            return nullptr;
        while (*atts != nullptr) {
            if (strcmp(*(atts ++), id) == 0)
                return *atts;
            ++ atts;
        }
        return nullptr;
    }

    enum AMFNodeType {
        NODE_TYPE_INVALID = 0,
        NODE_TYPE_UNKNOWN,
        NODE_TYPE_AMF,
        NODE_TYPE_MATERIAL,
        NODE_TYPE_OBJECT,
        NODE_TYPE_MESH,
        NODE_TYPE_VERTICES,
        NODE_TYPE_VERTEX,
        NODE_TYPE_COORDINATES,
        NODE_TYPE_COORDINATE_X,
        NODE_TYPE_COORDINATE_Y,
        NODE_TYPE_COORDINATE_Z,
        NODE_TYPE_VOLUME,
        NODE_TYPE_TRIANGLE,
        NODE_TYPE_VERTEX1,
        NODE_TYPE_VERTEX2,
        NODE_TYPE_VERTEX3,
        NODE_TYPE_CONSTELLATION,
        NODE_TYPE_INSTANCE,
        NODE_TYPE_DELTAX,
        NODE_TYPE_DELTAY,
        NODE_TYPE_DELTAZ,
        NODE_TYPE_RX,
        NODE_TYPE_RY,
        NODE_TYPE_RZ,
        NODE_TYPE_SCALE,
        NODE_TYPE_SCALEX,
        NODE_TYPE_SCALEY,
        NODE_TYPE_SCALEZ,
        NODE_TYPE_MIRRORX,
        NODE_TYPE_MIRRORY,
        NODE_TYPE_MIRRORZ,
        NODE_TYPE_PRINTABLE,
        NODE_TYPE_METADATA,
    };

    struct Instance {
        Instance()
            : deltax_set(false), deltay_set(false), deltaz_set(false)
            , rx_set(false), ry_set(false), rz_set(false)
            , scalex_set(false), scaley_set(false), scalez_set(false)
            , mirrorx_set(false), mirrory_set(false), mirrorz_set(false)
            , printable(true) {}
        float deltax;
        bool  deltax_set;
        float deltay;
        bool  deltay_set;
        float deltaz;
        bool  deltaz_set;
        float rx;
        bool  rx_set;
        float ry;
        bool  ry_set;
        float rz;
        bool  rz_set;
        float scalex;
        bool  scalex_set;
        float scaley;
        bool  scaley_set;
        float scalez;
        bool  scalez_set;
        float mirrorx;
        bool  mirrorx_set;
        float mirrory;
        bool  mirrory_set;
        float mirrorz;
        bool  mirrorz_set;
        bool  printable;

        bool anything_set() const { return deltax_set || deltay_set || deltaz_set ||
                                           rx_set || ry_set || rz_set ||
                                           scalex_set || scaley_set || scalez_set ||
                                           mirrorx_set || mirrory_set || mirrorz_set; }
    };

    struct Object {
        Object() : idx(-1) {}
        int                     idx;
        std::vector<Instance>   instances;
    };

    XML_Parser               m_parser;
    bool                     m_error { false };
    std::string              m_error_message;
    Model                   &m_model;
    std::vector<AMFNodeType> m_path;
    ModelObject             *m_object { nullptr };
    std::map<std::string, Object> m_object_instances_map;
    std::vector<Vec3f>       m_object_vertices;
    ModelVolume             *m_volume { nullptr };
    std::vector<Vec3i32>       m_volume_facets;
    Transform3d 			 m_volume_transform;
    ModelMaterial           *m_material { nullptr };
    Instance                *m_instance { nullptr };
    std::string              m_value[5];
    DynamicPrintConfig      *m_config { nullptr };
    ConfigSubstitutionContext *m_config_substitutions { nullptr };
    bool                     m_use_inches { false };

private:
    AMFParserContext& operator=(AMFParserContext&);
};

void AMFParserContext::startElement(const char *name, const char **atts)
{
    AMFNodeType node_type_new = NODE_TYPE_UNKNOWN;
    switch (m_path.size()) {
    case 0: {
        node_type_new = NODE_TYPE_AMF;
        if (strcmp(name, "amf") != 0) this->stop();
        const char *units = get_attribute(atts, "unit");
        if (units && (strcmp(units, "inch") == 0)) { m_use_inches = true; }
        break;
    }
    case 1:
        if (strcmp(name, "metadata") == 0) {
            const char *type = get_attribute(atts, "type");
            if (type != nullptr) {
                m_value[0] = type;
                node_type_new = NODE_TYPE_METADATA;
            }
        } else if (strcmp(name, "material") == 0) {
            const char *material_id = get_attribute(atts, "id");
            m_material = m_model.add_material((material_id == nullptr) ? "_" : material_id);
            node_type_new = NODE_TYPE_MATERIAL;
        } else if (strcmp(name, "object") == 0) {
            const char *object_id = get_attribute(atts, "id");
            if (object_id == nullptr)
                this->stop();
            else {
				assert(m_object_vertices.empty());
                m_object = m_model.add_object();
                m_object_instances_map[object_id].idx = int(m_model.objects.size())-1;
                node_type_new = NODE_TYPE_OBJECT;
            }
        } else if (strcmp(name, "constellation") == 0) {
            node_type_new = NODE_TYPE_CONSTELLATION;
        }
        break;
    case 2:
        if (strcmp(name, "metadata") == 0) {
            if (m_path[1] == NODE_TYPE_MATERIAL || m_path[1] == NODE_TYPE_OBJECT) {
                const char *type = get_attribute(atts, "type");
                if (type == nullptr)
                    this->stop();
                else {
                    m_value[0] = type;
                    node_type_new = NODE_TYPE_METADATA;
                }
            }
        }
        else if (strcmp(name, "mesh") == 0) {
            if (m_path[1] == NODE_TYPE_OBJECT)
                node_type_new = NODE_TYPE_MESH;
        } else if (strcmp(name, "instance") == 0) {
            if (m_path[1] == NODE_TYPE_CONSTELLATION) {
                const char *object_id = get_attribute(atts, "objectid");
                if (object_id == nullptr)
                    this->stop();
                else {
                    m_object_instances_map[object_id].instances.push_back(AMFParserContext::Instance());
                    m_instance = &m_object_instances_map[object_id].instances.back();
                    node_type_new = NODE_TYPE_INSTANCE;
                }
            }
            else
                this->stop();
        }
        break;
    case 3:
        if (m_path[2] == NODE_TYPE_MESH) {
			assert(m_object);
            if (strcmp(name, "vertices") == 0)
                node_type_new = NODE_TYPE_VERTICES;
			else if (strcmp(name, "volume") == 0) {
				assert(! m_volume);
                m_volume = m_object->add_volume(TriangleMesh());
                m_volume_transform = Transform3d::Identity();
                node_type_new = NODE_TYPE_VOLUME;
			}
        }
        else if (m_path[2] == NODE_TYPE_INSTANCE) {
            assert(m_instance);
            if (strcmp(name, "deltax") == 0)
                node_type_new = NODE_TYPE_DELTAX;
            else if (strcmp(name, "deltay") == 0)
                node_type_new = NODE_TYPE_DELTAY;
            else if (strcmp(name, "deltaz") == 0)
                node_type_new = NODE_TYPE_DELTAZ;
            else if (strcmp(name, "rx") == 0)
                node_type_new = NODE_TYPE_RX;
            else if (strcmp(name, "ry") == 0)
                node_type_new = NODE_TYPE_RY;
            else if (strcmp(name, "rz") == 0)
                node_type_new = NODE_TYPE_RZ;
            else if (strcmp(name, "scalex") == 0)
                node_type_new = NODE_TYPE_SCALEX;
            else if (strcmp(name, "scaley") == 0)
                node_type_new = NODE_TYPE_SCALEY;
            else if (strcmp(name, "scalez") == 0)
                node_type_new = NODE_TYPE_SCALEZ;
            else if (strcmp(name, "scale") == 0)
                node_type_new = NODE_TYPE_SCALE;
            else if (strcmp(name, "mirrorx") == 0)
                node_type_new = NODE_TYPE_MIRRORX;
            else if (strcmp(name, "mirrory") == 0)
                node_type_new = NODE_TYPE_MIRRORY;
            else if (strcmp(name, "mirrorz") == 0)
                node_type_new = NODE_TYPE_MIRRORZ;
            else if (strcmp(name, "printable") == 0)
                node_type_new = NODE_TYPE_PRINTABLE;
        }
        break;
    case 4:
        if (m_path[3] == NODE_TYPE_VERTICES) {
            if (strcmp(name, "vertex") == 0)
                node_type_new = NODE_TYPE_VERTEX;
        } else if (m_path[3] == NODE_TYPE_VOLUME) {
            if (strcmp(name, "metadata") == 0) {
                const char *type = get_attribute(atts, "type");
                if (type == nullptr)
                    this->stop();
                else {
                    m_value[0] = type;
                    node_type_new = NODE_TYPE_METADATA;
                }
            } else if (strcmp(name, "triangle") == 0)
                node_type_new = NODE_TYPE_TRIANGLE;
        }
        break;
    case 5:
        if (strcmp(name, "coordinates") == 0) {
            if (m_path[4] == NODE_TYPE_VERTEX) {
                node_type_new = NODE_TYPE_COORDINATES;
            } else
                this->stop();
        } else if (name[0] == 'v' && name[1] >= '1' && name[1] <= '3' && name[2] == 0) {
            if (m_path[4] == NODE_TYPE_TRIANGLE) {
                node_type_new = AMFNodeType(NODE_TYPE_VERTEX1 + name[1] - '1');
            } else
                this->stop();
        }
        break;
    case 6:
        if ((name[0] == 'x' || name[0] == 'y' || name[0] == 'z') && name[1] == 0) {
            if (m_path[5] == NODE_TYPE_COORDINATES)
                node_type_new = AMFNodeType(NODE_TYPE_COORDINATE_X + name[0] - 'x');
            else
                this->stop();
        }
        break;
    default:
        break;
    }

    m_path.push_back(node_type_new);
}

void AMFParserContext::characters(const XML_Char *s, int len)
{
    if (m_path.back() == NODE_TYPE_METADATA) {
        m_value[1].append(s, len);
    }
    else
    {
        switch (m_path.size()) {
        case 4:
            if (m_path.back() == NODE_TYPE_DELTAX ||
                m_path.back() == NODE_TYPE_DELTAY ||
                m_path.back() == NODE_TYPE_DELTAZ ||
                m_path.back() == NODE_TYPE_RX ||
                m_path.back() == NODE_TYPE_RY ||
                m_path.back() == NODE_TYPE_RZ ||
                m_path.back() == NODE_TYPE_SCALEX ||
                m_path.back() == NODE_TYPE_SCALEY ||
                m_path.back() == NODE_TYPE_SCALEZ ||
                m_path.back() == NODE_TYPE_SCALE ||
                m_path.back() == NODE_TYPE_MIRRORX ||
                m_path.back() == NODE_TYPE_MIRRORY ||
                m_path.back() == NODE_TYPE_MIRRORZ ||
                m_path.back() == NODE_TYPE_PRINTABLE)
                m_value[0].append(s, len);
            break;
        case 6:
            switch (m_path.back()) {
                case NODE_TYPE_VERTEX1: m_value[0].append(s, len); break;
                case NODE_TYPE_VERTEX2: m_value[1].append(s, len); break;
                case NODE_TYPE_VERTEX3: m_value[2].append(s, len); break;
                default: break;
            }
        case 7:
            switch (m_path.back()) {
                case NODE_TYPE_COORDINATE_X: m_value[0].append(s, len); break;
                case NODE_TYPE_COORDINATE_Y: m_value[1].append(s, len); break;
                case NODE_TYPE_COORDINATE_Z: m_value[2].append(s, len); break;
                default: break;
            }
        default:
            break;
        }
    }
}

void AMFParserContext::endElement(const char * )
{
    assert(is_decimal_separator_point());
    switch (m_path.back()) {

    case NODE_TYPE_DELTAX:
        assert(m_instance);
        m_instance->deltax = float(atof(m_value[0].c_str()));
        m_instance->deltax_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_DELTAY:
        assert(m_instance);
        m_instance->deltay = float(atof(m_value[0].c_str()));
        m_instance->deltay_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_DELTAZ:
        assert(m_instance);
        m_instance->deltaz = float(atof(m_value[0].c_str()));
        m_instance->deltaz_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_RX:
        assert(m_instance);
        m_instance->rx = float(atof(m_value[0].c_str()));
        m_instance->rx_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_RY:
        assert(m_instance);
        m_instance->ry = float(atof(m_value[0].c_str()));
        m_instance->ry_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_RZ:
        assert(m_instance);
        m_instance->rz = float(atof(m_value[0].c_str()));
        m_instance->rz_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_SCALE:
        assert(m_instance);
        m_instance->scalex = float(atof(m_value[0].c_str()));
        m_instance->scalex_set = true;
        m_instance->scaley = float(atof(m_value[0].c_str()));
        m_instance->scaley_set = true;
        m_instance->scalez = float(atof(m_value[0].c_str()));
        m_instance->scalez_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_SCALEX:
        assert(m_instance);
        m_instance->scalex = float(atof(m_value[0].c_str()));
        m_instance->scalex_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_SCALEY:
        assert(m_instance);
        m_instance->scaley = float(atof(m_value[0].c_str()));
        m_instance->scaley_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_SCALEZ:
        assert(m_instance);
        m_instance->scalez = float(atof(m_value[0].c_str()));
        m_instance->scalez_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_MIRRORX:
        assert(m_instance);
        m_instance->mirrorx = float(atof(m_value[0].c_str()));
        m_instance->mirrorx_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_MIRRORY:
        assert(m_instance);
        m_instance->mirrory = float(atof(m_value[0].c_str()));
        m_instance->mirrory_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_MIRRORZ:
        assert(m_instance);
        m_instance->mirrorz = float(atof(m_value[0].c_str()));
        m_instance->mirrorz_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_PRINTABLE:
        assert(m_instance);
        m_instance->printable = bool(atoi(m_value[0].c_str()));
        m_value[0].clear();
        break;

    case NODE_TYPE_VERTEX:
        assert(m_object);
        m_object_vertices.emplace_back(float(atof(m_value[0].c_str())), float(atof(m_value[1].c_str())), float(atof(m_value[2].c_str())));
        m_value[0].clear();
        m_value[1].clear();
        m_value[2].clear();
        break;

    case NODE_TYPE_TRIANGLE:
        assert(m_object && m_volume);
        m_volume_facets.emplace_back(atoi(m_value[0].c_str()), atoi(m_value[1].c_str()), atoi(m_value[2].c_str()));
        m_value[0].clear();
        m_value[1].clear();
        m_value[2].clear();
        break;

    case NODE_TYPE_VOLUME:
    {
		assert(m_object && m_volume);
        if (m_volume_facets.empty()) {
            this->stop("Found an empty triangle mesh");
            return;
        }

        {
            int min_id = m_volume_facets.front()[0];
            int max_id = min_id;
            for (const Vec3i32& face : m_volume_facets) {
                for (const int tri_id : face) {
                    if (tri_id < 0 || tri_id >= int(m_object_vertices.size())) {
                        this->stop("Found a malformed triangle mesh");
                        return;
                    }
                    min_id = std::min(min_id, tri_id);
                    max_id = std::max(max_id, tri_id);
                }
            }

            for (Vec3i32 &face : m_volume_facets)
                face -= Vec3i32(min_id, min_id, min_id);

            indexed_triangle_set its { std::move(m_volume_facets), { m_object_vertices.begin() + min_id, m_object_vertices.begin() + max_id + 1 } };
            its_compactify_vertices(its);
            if (its_volume(its) < 0)
                its_flip_triangles(its);
            m_volume->set_mesh(std::move(its));
        }

        if (bool has_transform = !m_volume_transform.isApprox(Transform3d::Identity(), 1e-10); has_transform)
            m_volume->source.transform = Slic3r::Geometry::Transformation(m_volume_transform);

        if (m_volume->source.input_file.empty() && (m_volume->type() == ModelVolumeType::MODEL_PART)) {
            m_volume->source.object_idx = (int)m_model.objects.size() - 1;
            m_volume->source.volume_idx = (int)m_model.objects.back()->volumes.size() - 1;
            m_volume->center_geometry_after_creation();
        } else
            m_volume->center_geometry_after_creation(m_volume->source.input_file.empty());

        m_volume->calculate_convex_hull();
        m_volume_facets.clear();
        m_volume = nullptr;
        break;
    }

    case NODE_TYPE_OBJECT:
        assert(m_object);
        m_object_vertices.clear();
        m_object = nullptr;
        break;

    case NODE_TYPE_MATERIAL:
        assert(m_material);
        m_material = nullptr;
        break;

    case NODE_TYPE_INSTANCE:
        assert(m_instance);
        m_instance = nullptr;
        break;

    case NODE_TYPE_METADATA:
        
if (m_path.size() == 3) {
            if (m_path[1] == NODE_TYPE_MATERIAL) {
                if (m_material)
                    m_material->attributes[m_value[0]] = m_value[1];
            } else if (m_path[1] == NODE_TYPE_OBJECT) {
                if (m_object && m_value[0] == "name")
                    m_object->name = std::move(m_value[1]);
            }
        } else if (m_path.size() == 5 && m_path[3] == NODE_TYPE_VOLUME) {
            if (m_volume && m_value[0] == "name")
                m_volume->name = std::move(m_value[1]);
        }

        m_value[0].clear();
        m_value[1].clear();
        break;
    default:
        break;
    }
    m_path.pop_back();
}

void AMFParserContext::endDocument()
{
    for (const auto &object : m_object_instances_map) {
        if (object.second.idx == -1) {
            BOOST_LOG_TRIVIAL(error) << "Undefined object " << object.first.c_str() << " referenced in constellation";
            continue;
        }
        int index = 0;
        for (const Instance &instance : object.second.instances) {
            ModelObject *model_object = m_model.objects[object.second.idx];
            index++;
            if (index > 1) {
                ModelObject *new_model_object = m_model.add_object(*model_object);
                new_model_object->clear_instances();
                model_object = new_model_object;
            }

            if (instance.anything_set()) {
                ModelInstance *mi = model_object->add_instance();
                mi->set_offset(Vec3d(instance.deltax_set ? (double) instance.deltax : 0.0, instance.deltay_set ? (double) instance.deltay : 0.0,
                                     instance.deltaz_set ? (double) instance.deltaz : 0.0));
                mi->set_rotation(Vec3d(instance.rx_set ? (double) instance.rx : 0.0, instance.ry_set ? (double) instance.ry : 0.0, instance.rz_set ? (double) instance.rz : 0.0));
                mi->set_scaling_factor(Vec3d(instance.scalex_set ? (double) instance.scalex : 1.0, instance.scaley_set ? (double) instance.scaley : 1.0,
                                             instance.scalez_set ? (double) instance.scalez : 1.0));
                mi->set_mirror(Vec3d(instance.mirrorx_set ? (double) instance.mirrorx : 1.0, instance.mirrory_set ? (double) instance.mirrory : 1.0,
                                     instance.mirrorz_set ? (double) instance.mirrorz : 1.0));
                mi->printable = instance.printable;
            }
        }
    }
}

bool load_amf_file(const char *path, DynamicPrintConfig *config, ConfigSubstitutionContext *config_substitutions, Model *model, bool *use_inches)
{
    if ((path == nullptr) || (model == nullptr))
        return false;

    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) {
        BOOST_LOG_TRIVIAL(error) << "Couldn't allocate memory for parser";
        return false;
    }

    FILE *pFile = boost::nowide::fopen(path, "rt");
    if (pFile == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "Cannot open file " << path;
        return false;
    }

    AMFParserContext ctx(parser, config, config_substitutions, model);
    XML_SetUserData(parser, (void*)&ctx);
    XML_SetElementHandler(parser, AMFParserContext::startElement, AMFParserContext::endElement);
    XML_SetCharacterDataHandler(parser, AMFParserContext::characters);

    char buff[8192];
    bool result = false;
    for (;;) {
        int len = (int)fread(buff, 1, 8192, pFile);
        if (ferror(pFile)) {
            BOOST_LOG_TRIVIAL(error) << "AMF parser: Read error";
            break;
        }
        int done = feof(pFile);
        if (XML_Parse(parser, buff, len, done) == XML_STATUS_ERROR || ctx.error()) {
            BOOST_LOG_TRIVIAL(error) << "AMF parser: Parse error at line " << int(XML_GetCurrentLineNumber(parser)) << ": " << ctx.error_message();
            break;
        }
        if (done) {
            result = true;
            break;
        }
    }

    XML_ParserFree(parser);
    ::fclose(pFile);

    if (result)
        ctx.endDocument();
    if (use_inches) {
        *use_inches = ctx.m_use_inches;
    }

    for (ModelObject* o : model->objects)
    {
        for (ModelVolume* v : o->volumes)
        {
            if (v->source.input_file.empty() && (v->type() == ModelVolumeType::MODEL_PART))
                v->source.input_file = path;
        }
    }

    return result;
}

bool extract_model_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, Model* model, bool* use_inches)
{
    if (stat.m_uncomp_size == 0)
    {
        BOOST_LOG_TRIVIAL(error) << "Found invalid size";
        close_zip_reader(&archive);
        return false;
    }

    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) {
        BOOST_LOG_TRIVIAL(error) << "Couldn't allocate memory for parser";
        close_zip_reader(&archive);
        return false;
    }

    AMFParserContext ctx(parser, config, config_substitutions, model);
    XML_SetUserData(parser, (void*)&ctx);
    XML_SetElementHandler(parser, AMFParserContext::startElement, AMFParserContext::endElement);
    XML_SetCharacterDataHandler(parser, AMFParserContext::characters);

    struct CallbackData
    {
        XML_Parser& parser;
        AMFParserContext& ctx;
        const mz_zip_archive_file_stat& stat;

        CallbackData(XML_Parser& parser, AMFParserContext& ctx, const mz_zip_archive_file_stat& stat) : parser(parser), ctx(ctx), stat(stat) {}
    };

    CallbackData data(parser, ctx, stat);

    mz_bool res = 0;

    try
    {
        res = mz_zip_reader_extract_file_to_callback(&archive, stat.m_filename, [](void* pOpaque, mz_uint64 file_ofs, const void* pBuf, size_t n)->size_t {
            CallbackData* data = (CallbackData*)pOpaque;
            if (!XML_Parse(data->parser, (const char*)pBuf, (int)n, (file_ofs + n == data->stat.m_uncomp_size) ? 1 : 0) || data->ctx.error())
            {
                char error_buf[1024];
                ::sprintf(error_buf, "Parsing file %s error at line %d: {%s}", data->stat.m_filename, (int) XML_GetCurrentLineNumber(data->parser), data->ctx.error_message());
                throw Slic3r::FileIOError(error_buf);
            }

            return n;
            }, &data, 0);
    }
    catch (std::exception& e)
    {
        BOOST_LOG_TRIVIAL(error) << "Error reading AMF file: " << e.what();
        close_zip_reader(&archive);
        return false;
    }

    if (res == 0)
    {
        BOOST_LOG_TRIVIAL(error) << "Error while extracting model data from zip archive";
        close_zip_reader(&archive);
        return false;
    }

    ctx.endDocument();
    if (use_inches) {
        *use_inches = ctx.m_use_inches;
    }

    return true;
}

bool load_amf_archive(const char* path, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, Model* model, bool* use_inches)
{
    if ((path == nullptr) || (model == nullptr))
        return false;

    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);

    if (!open_zip_reader(&archive, path))
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to init zip reader";
        return false;
    }

    mz_uint num_entries = mz_zip_reader_get_num_files(&archive);

    mz_zip_archive_file_stat stat;
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        if (mz_zip_reader_file_stat(&archive, i, &stat))
        {
            if (boost::iends_with(stat.m_filename, ".amf"))
            {
                try
                {
                    if (!extract_model_from_archive(archive, stat, config, config_substitutions, model, use_inches))
                    {
                        close_zip_reader(&archive);
                        BOOST_LOG_TRIVIAL(error) << "Archive does not contain a valid model";
                        return false;
                    }
                }
                catch (const std::exception& e)
                {
                    close_zip_reader(&archive);
                    throw Slic3r::FileIOError(e.what());
                }

                break;
            }
        }
    }

#if 0
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        if (mz_zip_reader_file_stat(&archive, i, &stat))
        {
        }
    }
#endif

    close_zip_reader(&archive);

    for (ModelObject *o : model->objects)
        for (ModelVolume *v : o->volumes)
            if (v->source.input_file.empty() && (v->type() == ModelVolumeType::MODEL_PART))
                v->source.input_file = path;

    return true;
}

bool load_amf(const char *path, DynamicPrintConfig *config, ConfigSubstitutionContext *config_substitutions, Model *model, bool* use_inches)
{
    CNumericLocalesSetter locales_setter;

    if (boost::iends_with(path, ".amf"))
    {
        boost::nowide::ifstream file(path, boost::nowide::ifstream::binary);
        if (!file.good())
            return false;

        std::string zip_mask(2, '\0');
        file.read(zip_mask.data(), 2);
        file.close();

        return (zip_mask == "PK") ? load_amf_archive(path, config, config_substitutions, model, use_inches) : load_amf_file(path, config, config_substitutions, model, use_inches);
    }
    else
        return false;
}

};
