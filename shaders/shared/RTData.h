#ifndef RTDATA_H
#define RTDATA_H

#define RT_HIT_MASK_OPAQUE 0x01
#define RT_HIT_MASK_MASKED 0x02
#define RT_HIT_MASK_BLEND  0x04

#define RT_MAX_TEXTURES 256

struct RTMesh {
    int objectId;
    int baseColor;

    //int pad0, pad1;
};

struct RTTriangleMesh {
    int firstVertex;
    int firstIndex;
    int materialIndex;
};

struct RTAABB {
	vec3 min;
	vec3 max;
};

struct RTAABB_packable {
	float minX, minY, minZ;
	float maxX, maxY, maxZ;
};

struct RTSphere {
	vec3 center;
	float radius;
};

struct RTVoxelContour {
    vec4 plane;
};

struct RTVertex {
    // TODO: we could fit the tex coord data in .w of position & normal!
    vec4 position;
    vec4 normal;
    vec4 texCoord;
};

#endif // RTDATA_H
