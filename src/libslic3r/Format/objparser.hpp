#ifndef slic3r_Format_objparser_hpp_
#define slic3r_Format_objparser_hpp_

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <istream>
#include <memory>

namespace ObjParser {

struct ObjVertex
{
	int coordIdx;
	int textureCoordIdx;
	int normalIdx;
};

inline bool operator==(const ObjVertex &v1, const ObjVertex &v2)
{
	return  v1.coordIdx		== v2.coordIdx			&&
		v1.textureCoordIdx	== v2.textureCoordIdx	&&
		v1.normalIdx		== v2.normalIdx;
}

struct ObjUseMtl
{
	int			vertexIdxFirst;
    int         vertexIdxEnd{-1};
    int         face_start;
    int         face_end{-1};
	std::string name;
};

struct ObjNewMtl
{
    std::string name;
    float       Ns;
    float       Ni;
    float       d;
    float       illum;
    float       Tr{1.0f};
    std::array<float, 3>  Tf;
    std::array<float, 3>  Ka;
    std::array<float, 3>  Kd;
    std::array<float, 3>  Ks;
    std::array<float, 3>  Ke;
    std::string           map_Kd;
};

inline bool operator==(const ObjUseMtl &v1, const ObjUseMtl &v2)
{
	return v1.vertexIdxFirst	== v2.vertexIdxFirst	&&
		v1.name.compare(v2.name) == 0;
}

struct ObjObject
{
	int			vertexIdxFirst;
	std::string name;
};

inline bool operator==(const ObjObject &v1, const ObjObject &v2)
{
	return
		v1.vertexIdxFirst	== v2.vertexIdxFirst	&&
		v1.name.compare(v2.name) == 0;
}

struct ObjGroup
{
	int			vertexIdxFirst;
	std::string name;
};

inline bool operator==(const ObjGroup &v1, const ObjGroup &v2)
{
	return v1.vertexIdxFirst	== v2.vertexIdxFirst &&
		v1.name.compare(v2.name) == 0;
}

struct ObjSmoothingGroup
{
	int			vertexIdxFirst;
	int			smoothingGroupID;
};

inline bool operator==(const ObjSmoothingGroup &v1, const ObjSmoothingGroup &v2)
{
	return v1.vertexIdxFirst	== v2.vertexIdxFirst	&&
		v1.smoothingGroupID == v2.smoothingGroupID;
}
#define OBJ_VERTEX_COLOR_ALPHA 6
#define OBJ_VERTEX_LENGTH   7
#define OBJ_TEXCOORD_LENGTH 2
#define ONE_FACE_SIZE 4
struct ObjData {

	int								version;


	std::vector<float>				coordinates;
    bool                            has_vertex_color{false};

	std::vector<float>				textureCoordinates;

	std::vector<float>				normals;

	std::vector<float>				parameters;

	std::vector<std::string>		mtllibs;
	std::vector<ObjUseMtl>			usemtls;
	std::vector<ObjObject>			objects;
	std::vector<ObjGroup>			groups;
	std::vector<ObjSmoothingGroup>	smoothingGroups;


	std::vector<ObjVertex>			vertices;
};

struct MtlData
{

    int version;
    std::unordered_map<std::string, std::shared_ptr<ObjNewMtl>> new_mtl_unmap;


    std::vector<std::string>                                    mtl_orders;
};
extern bool objparse(const char *path, ObjData &data);
extern bool mtlparse(const char *path, MtlData &data);
extern bool objparse(std::istream &stream, ObjData &data);

extern bool objbinsave(const char *path, const ObjData &data);

extern bool objbinload(const char *path, ObjData &data);

extern bool objequal(const ObjData &data1, const ObjData &data2);

}

#endif
