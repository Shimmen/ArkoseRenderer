#ifndef RTDATA_H
#define RTDATA_H

#define RT_HIT_MASK_OPAQUE 0x01
#define RT_HIT_MASK_MASKED 0x02
#define RT_HIT_MASK_BLEND  0x04

struct RTTriangleMesh {
    int firstVertex;
    int firstIndex;
    int materialIndex;
};

#endif // RTDATA_H
