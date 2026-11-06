#include "../Model.hpp"
#include "../TriangleMesh.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Thread.hpp"

#include "STEP.hpp"

#include <boost/filesystem/path.hpp>
#include <ios>
#include <ostream>
#include <cstddef>
#include <Standard_Handle.hxx>
#include <Standard_TypeDef.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <atomic>
#include <IFSelect_ReturnStatus.hxx>
#include <boost/chrono/duration.hpp>
#include <cstring>
#include <Poly_Triangulation.hxx>
#include <cstdint>
#include <gp_Trsf.hxx>
#include <gp_Pnt.hxx>
#include <TopAbs_Orientation.hxx>
#include <Poly_Triangle.hxx>
#include <string>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/nowide/fstream.hpp>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <vector>
#include <utility>

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

#include "STEPCAFControl_Reader.hxx"
#include "BRepMesh_IncrementalMesh.hxx"
#include "Interface_Static.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XCAFApp_Application.hxx"
#include "NCollection_Sequence.hxx"
#include "TDF_Label.hxx"
#include "TopoDS_Solid.hxx"
#include "TopoDS_Compound.hxx"
#include "TopoDS_Builder.hxx"
#include "TopoDS.hxx"
#include "TDataStd_Name.hxx"
#include "BRepBuilderAPI_Transform.hxx"
#include "TopExp_Explorer.hxx"
#include "BRep_Tool.hxx"
#include "BRepTools.hxx"
#include <IMeshTools_Parameters.hxx>

namespace fs = boost::filesystem;


namespace Slic3r {

bool StepPreProcessor::preprocess(const char* path, std::string &output_path)
{
    boost::nowide::ifstream infile(path);
    if (!infile.good()) {
        throw Slic3r::RuntimeError(std::string("Load step file failed.\nCannot open file for reading.\n"));
        return false;
    }

    boost::filesystem::path temp_path(temporary_dir());
    std::string temp_step_path = temp_path.string() + "/temp.step";
    boost::nowide::remove(temp_step_path.c_str());
    boost::nowide::ofstream temp_file(temp_step_path, std::ios::app);
    std::string temp_line;
    while (std::getline(infile, temp_line)) {
        if (m_encode_type == EncodedType::UTF8) {

            if (isUtf8(temp_line)) {

            }

            else if (isGBK(temp_line)) {
                m_encode_type = EncodedType::GBK;
            }


            else {
                m_encode_type = EncodedType::OTHER;
            }
        }
        if (m_encode_type == EncodedType::GBK)


            temp_file << decode_path(temp_line.c_str()) << std::endl;
        else
            temp_file << temp_line.c_str() << std::endl;
    }
    temp_file.close();
    infile.close();
    if (m_encode_type == EncodedType::GBK) {
        output_path = temp_step_path;
    } else {
        boost::nowide::remove(temp_step_path.c_str());
        output_path = std::string(path);
    }

    return true;
}

bool StepPreProcessor::isUtf8File(const char* path)
{
    boost::nowide::ifstream infile(path);
    if (!infile.good()) {
        throw Slic3r::RuntimeError(std::string("Load step file failed.\nCannot open file for reading.\n"));
        return false;
    }

    std::string temp_line;
    while (std::getline(infile, temp_line)) {
        if (!isUtf8(temp_line)) {
            infile.close();
            return false;
        }
    }

    infile.close();
    return true;
}

bool StepPreProcessor::isUtf8(const std::string str)
{
    size_t num = 0;
    size_t i = 0;
    while (i < str.length()) {
        const unsigned char lead = static_cast<unsigned char>(str[i]);
        if ((lead & 0x80) == 0x00) {
            i++;


        } else if ((num = preNum(lead)) >= 2 && num <= 4) {
            if (i + num > str.length())
                return false;
            i++;
            for (size_t j = 0; j < num - 1; j++) {
                if ((static_cast<unsigned char>(str[i]) & 0xc0) != 0x80)
                    return false;
                i++;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool StepPreProcessor::isGBK(const std::string str) {
    size_t i = 0;
    while (i < str.length()) {

        const unsigned char lead = static_cast<unsigned char>(str[i]);
        if (lead <= 0x7f) {
            i++;
            continue;
        } else {
            if (i + 1 >= str.length())
                return false;
            const unsigned char trail = static_cast<unsigned char>(str[i + 1]);
            if (lead >= 0x81 &&
                lead <= 0xfe &&
                trail >= 0x40 &&
                trail <= 0xfe &&
                trail != 0xf7) {
                i += 2;
                continue;
            }
            else {
                return false;
            }
        }
    }
    return true;
}

int StepPreProcessor::preNum(const unsigned char byte) {
    unsigned char mask = 0x80;
    int num = 0;
    for (int i = 0; i < 8; i++) {
        if ((byte & mask) == mask) {
            mask = mask >> 1;
            num++;
        } else {
            break;
        }
    }
    return num;
}

static void getNamedSolids(const TopLoc_Location& location,
                           const std::string& prefix,
                           unsigned int& id,
                           const Handle(XCAFDoc_ShapeTool) shapeTool,
                           const TDF_Label label,
                           std::vector<NamedSolid>& namedSolids,
                           bool isSplitCompound = false) {
    TDF_Label referredLabel{label};
    if (shapeTool->IsReference(label))
        shapeTool->GetReferredShape(label, referredLabel);

    std::string name;
    Handle(TDataStd_Name) shapeName;
    if (referredLabel.FindAttribute(TDataStd_Name::GetID(), shapeName) ||
        label.FindAttribute(TDataStd_Name::GetID(), shapeName))
        name = TCollection_AsciiString(shapeName->Get()).ToCString();

    if (name == "" || !StepPreProcessor::isUtf8(name))
        name = std::to_string(id++);
    std::string fullName{name};

    TopLoc_Location localLocation = location * shapeTool->GetLocation(label);
    NCollection_Sequence<TDF_Label> components;
    if (shapeTool->GetComponents(referredLabel, components)) {
        for (int compIndex = 1; compIndex <= components.Length(); ++compIndex) {
            getNamedSolids(localLocation, fullName, id, shapeTool, components.Value(compIndex), namedSolids, isSplitCompound);
        }
    } else {
        TopoDS_Shape shape;
        TopExp_Explorer explorer;
        shapeTool->GetShape(referredLabel, shape);
        TopAbs_ShapeEnum shape_type = shape.ShapeType();
        BRepBuilderAPI_Transform transform(shape, localLocation, true);
        int                      i = 0;
        switch (shape_type) {
        case TopAbs_COMPOUND:
            if (!isSplitCompound) {
                namedSolids.emplace_back(TopoDS::Compound(transform.Shape()), fullName);
                break;
            }
        case TopAbs_COMPSOLID:
            if (!isSplitCompound) {
                namedSolids.emplace_back(TopoDS::CompSolid(transform.Shape()), fullName);
            } else {
                for (explorer.Init(transform.Shape(), TopAbs_SOLID); explorer.More(); explorer.Next()) {
                    i++;
                    const TopoDS_Shape& currentShape = explorer.Current();
                    namedSolids.emplace_back(TopoDS::Solid(currentShape), fullName + "-SOLID-" + std::to_string(i));
                }
            }
            break;
        case TopAbs_SOLID:
            namedSolids.emplace_back(TopoDS::Solid(transform.Shape()), fullName);
            break;
        case TopAbs_SHELL:
            namedSolids.emplace_back(TopoDS::Shell(transform.Shape()), fullName);
            break;
        default:
            break;
        }
    }
}

























































































































































































Step::Step(fs::path path, ImportStepProgressFn stepFn, StepIsUtf8Fn isUtf8Fn):
    m_stepFn(stepFn),
    m_utf8Fn(isUtf8Fn)
{
    m_path = path.string();
    m_app->NewDocument(TCollection_ExtendedString("BinXCAF"), m_doc);
}

Step::Step(std::string path, ImportStepProgressFn stepFn, StepIsUtf8Fn isUtf8Fn) :
    m_path(path),
    m_stepFn(stepFn),
    m_utf8Fn(isUtf8Fn)
{
    m_app->NewDocument(TCollection_ExtendedString("BinXCAF"), m_doc);
}

Step::~Step()
{
    m_app->Close(m_doc);
}

void Step::update_process(int load_stage, int current, int total, bool& cancel)
{
    if (m_stepFn) {
        m_stepFn(load_stage, current, total, cancel);
    }
}

Step::Step_Status Step::load()
{
    if (!StepPreProcessor::isUtf8File(m_path.c_str()) && m_utf8Fn) {
        m_utf8Fn(false);
        return Step_Status::LOAD_ERROR;
    }
    std::atomic<bool> stop_load_flag = false;
    Handle(StepProgressIncdicator) incdicator = new StepProgressIncdicator(stop_load_flag);
    bool task_result = false;
    bool cb_cancel = false;
    int progress = 0;
    bool load_result = false;
    auto task = new boost::thread(Slic3r::create_thread([&]() -> void {

        STEPCAFControl_Reader reader;
        reader.SetNameMode(true);
        IFSelect_ReturnStatus stat = reader.ReadFile(m_path.c_str());
        if (cb_cancel) return;
        progress = 3;
        if (stat != IFSelect_RetDone || !reader.Transfer(m_doc, incdicator->Start())) {
            load_result = false;
            task_result = true;
            return;
        }
        if (cb_cancel) return;
        progress = 6;
        m_shape_tool = XCAFDoc_DocumentTool::ShapeTool(m_doc->Main());
        NCollection_Sequence<TDF_Label> topLevelShapes;
        m_shape_tool->GetFreeShapes(topLevelShapes);
        unsigned int id{ 1 };
        int topShapeLength = topLevelShapes.Length() + 1;
        for (int iLabel = 1; iLabel < topShapeLength; ++iLabel) {
            if (cb_cancel) return;
            getNamedSolids(TopLoc_Location{}, "", id, m_shape_tool, topLevelShapes.Value(iLabel), m_name_solids);
        }
        progress = 10;
        load_result = true;
        task_result = true;
    }));
    while (!task_result) {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(200));
        update_process(LOAD_STEP_STAGE_READ_FILE, progress, 10, cb_cancel);
        if (cb_cancel) {
            stop_load_flag.store(true);
            if (task) {
                if (task->joinable()) {
                    task->join();
                    delete task;
                }
            }
            return Step_Status::CANCEL;
        }
    }
    if (task){
        if (task->joinable()) {
            task->join();
            delete task;
        }
    }
    if (load_result) {
        return Step_Status::LOAD_SUCCESS;
    }else {
        return Step_Status::LOAD_ERROR;
    }

}

Step::Step_Status Step::mesh(Model* model,
                             bool& is_cancel,
                             bool isSplitCompound,
                             double linear_deflection ,
                             double angle_deflection )

{
    bool task_result = false;
    bool cb_cancel = false;
    float progress = .0;
    std::atomic<int> meshed_solid_num = 0;
    std::vector<NamedSolid> namedSolids;
    float progress_2 = .0;
    ModelObject* new_object = model->add_object();
    const char* last_slash = strrchr(m_path.c_str(), DIR_SEPARATOR);
    new_object->name.assign((last_slash == nullptr) ? m_path.c_str() : last_slash + 1);
    new_object->input_file = m_path.c_str();

    auto task = new boost::thread(Slic3r::create_thread([&]() -> void {
        NCollection_Sequence<TDF_Label> topLevelShapes;
        m_shape_tool->GetFreeShapes(topLevelShapes);
        unsigned int id{ 1 };
        int topShapeLength = topLevelShapes.Length() + 1;

        for (int iLabel = 1; iLabel < topShapeLength; ++iLabel) {
            progress = static_cast<double>(iLabel) / (topShapeLength-1);
            if (cb_cancel) {
                return;
            }
            getNamedSolids(TopLoc_Location{}, "", id, m_shape_tool, topLevelShapes.Value(iLabel), namedSolids, isSplitCompound);
        }

        std::vector<stl_file> stl;
        stl.resize(namedSolids.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, namedSolids.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); i++) {
                BRepMesh_IncrementalMesh mesh(namedSolids[i].solid, linear_deflection, false, angle_deflection, true);

                int aNbNodes = 0;
                int aNbTriangles = 0;
                for (TopExp_Explorer anExpSF(namedSolids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
                    TopLoc_Location aLoc;
                    Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(anExpSF.Current()), aLoc);
                    if (!aTriangulation.IsNull()) {
                        aNbNodes += aTriangulation->NbNodes();
                        aNbTriangles += aTriangulation->NbTriangles();
                    }
                }

                if (aNbTriangles == 0 || aNbNodes == 0)

                    continue;

                stl[i].stats.type = inmemory;
                stl[i].stats.number_of_facets = (uint32_t)aNbTriangles;
                stl[i].stats.original_num_facets = stl[i].stats.number_of_facets;
                stl_allocate(&stl[i]);

                std::vector<Vec3f> points;
                points.reserve(aNbNodes);

                int aNbFacesNoTri = 0;

                int aNodeOffset = 0;
                int aTriangleOffet = 0;
                for (TopExp_Explorer anExpSF(namedSolids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
                    const TopoDS_Shape& aFace = anExpSF.Current();
                    TopLoc_Location     aLoc;
                    Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(aFace), aLoc);
                    if (aTriangulation.IsNull()) {
                        ++aNbFacesNoTri;
                        continue;
                    }

                    gp_Trsf aTrsf = aLoc.Transformation();
                    for (int aNodeIter = 1; aNodeIter <= aTriangulation->NbNodes(); ++aNodeIter) {
                        gp_Pnt aPnt = aTriangulation->Node(aNodeIter);
                        aPnt.Transform(aTrsf);
                        points.emplace_back(Vec3f(aPnt.X(), aPnt.Y(), aPnt.Z()));
                    }

                    const TopAbs_Orientation anOrientation = anExpSF.Current().Orientation();
                    int anId[3] = {};
                    for (int aTriIter = 1; aTriIter <= aTriangulation->NbTriangles(); ++aTriIter) {
                        Poly_Triangle aTri = aTriangulation->Triangle(aTriIter);

                        aTri.Get(anId[0], anId[1], anId[2]);
                        if (anOrientation == TopAbs_REVERSED)
                            std::swap(anId[1], anId[2]);

                        stl_facet facet;
                        facet.vertex[0] = points[anId[0] + aNodeOffset - 1].cast<float>();
                        facet.vertex[1] = points[anId[1] + aNodeOffset - 1].cast<float>();
                        facet.vertex[2] = points[anId[2] + aNodeOffset - 1].cast<float>();
                        facet.extra[0] = 0;
                        facet.extra[1] = 0;
                        stl_normal normal;
                        stl_calculate_normal(normal, &facet);
                        stl_normalize_vector(normal);
                        facet.normal = normal;
                        stl[i].facet_start[aTriangleOffet + aTriIter - 1] = facet;
                    }

                    aNodeOffset += aTriangulation->NbNodes();
                    aTriangleOffet += aTriangulation->NbTriangles();
                }
                meshed_solid_num.fetch_add(1, std::memory_order_relaxed);
            }
        });


        for (size_t i = 0; i < stl.size(); i++) {
            progress_2 = static_cast<float>(i) / stl.size();
            if (cb_cancel)
                return;


            if (stl[i].stats.number_of_facets > 0) {
                TriangleMesh triangle_mesh;
                triangle_mesh.from_stl(stl[i]);
                ModelVolume* new_volume = new_object->add_volume(std::move(triangle_mesh));
                new_volume->name = namedSolids[i].name;
                new_volume->source.input_file = m_path.c_str();
                new_volume->source.object_idx = (int)model->objects.size() - 1;
                new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
            }
        }
        task_result = true;
    }));

    while (!task_result) {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(300));
        if (progress_2 > 0) {

            update_process(LOAD_STEP_STAGE_GET_MESH, static_cast<int>(progress_2 * 100), 100, cb_cancel);
        }else {
            if (meshed_solid_num.load()) {

                int meshed_solid = meshed_solid_num.load();
                update_process(LOAD_STEP_STAGE_GET_SOLID, static_cast<int>((float)meshed_solid / namedSolids.size() * 10) + 10, 20, cb_cancel);
            } else {
                if (progress > 0) {

                    update_process(LOAD_STEP_STAGE_GET_SOLID, static_cast<int>(progress * 10), 20, cb_cancel);
                }
            }
        }


        if (cb_cancel) {
            if (task) {
                if (task->joinable()) {
                    task->join();
                    delete task;
                }
            }
            return Step_Status::CANCEL;
        }
    }
    if (task) {
        if (task->joinable()) {
            task->join();
            delete task;
        }
    }


    if (new_object->volumes.size() == 0) {
        model->delete_object(new_object);
        return Step_Status::MESH_ERROR;
    }
    return Step_Status::MESH_SUCCESS;
}

void Step::clean_mesh_data()
{
    for (const auto& name_solid : m_name_solids) {
        BRepTools::Clean(name_solid.solid);
    }
}

unsigned int Step::get_triangle_num(double linear_deflection, double angle_deflection)
{
    unsigned int tri_num = 0;
    try {
        Handle(StepProgressIncdicator) progress = new StepProgressIncdicator(m_stop_mesh);
        clean_mesh_data();
        IMeshTools_Parameters param;
        param.Deflection = linear_deflection;
        param.Angle = angle_deflection;
        param.InParallel = true;
        for (int i = 0; i < m_name_solids.size(); ++i) {
            BRepMesh_IncrementalMesh mesh(m_name_solids[i].solid, param, progress->Start());
            for (TopExp_Explorer anExpSF(m_name_solids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
                TopLoc_Location aLoc;
                Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(anExpSF.Current()), aLoc);
                if (!aTriangulation.IsNull()) {
                    tri_num += aTriangulation->NbTriangles();
                }
            }
            if (m_stop_mesh.load()) {
                return 0;
            }
        }
    } catch(const Exception &) {
        return 0;
    }

    return tri_num;
}

unsigned int Step::get_triangle_num_tbb(double linear_deflection, double angle_deflection)
{
    unsigned int tri_num = 0;
    clean_mesh_data();
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_name_solids.size()),
    [&](const tbb::blocked_range<size_t>& range) {
        for (size_t i = range.begin(); i < range.end(); i++) {
            unsigned int solids_tri_num = 0;
            BRepMesh_IncrementalMesh mesh(m_name_solids[i].solid, linear_deflection, false, angle_deflection, true);
            for (TopExp_Explorer anExpSF(m_name_solids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
                TopLoc_Location aLoc;
                Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(anExpSF.Current()), aLoc);
                if (!aTriangulation.IsNull()) {
                    solids_tri_num += aTriangulation->NbTriangles();
                }
            }
            m_name_solids[i].tri_face_cout = solids_tri_num;
        }

    });
    for (int i = 0; i < m_name_solids.size(); ++i) {
        tri_num += m_name_solids[i].tri_face_cout;
    }
    return tri_num;
}

};
