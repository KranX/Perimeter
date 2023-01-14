#ifndef PERIMETER_SOKOLRENDERPIPELINE_H
#define PERIMETER_SOKOLRENDERPIPELINE_H

#include "VertexFormat.h"

enum PIPELINE_TYPE {
    PIPELINE_TYPE_TRIANGLE,
    PIPELINE_TYPE_TRIANGLESTRIP,
    PIPELINE_TYPE_TERRAIN,
#ifdef PERIMETER_DEBUG
    PIPELINE_TYPE_LINE_STRIP,
#endif
    PIPELINE_TYPE_MAX,
};
const PIPELINE_TYPE PIPELINE_TYPE_DEFAULT = PIPELINE_TYPE_TRIANGLE;

static PIPELINE_TYPE getPipelineType(const ePrimitiveType type) {
    switch (type) {
        default:
            xassert(0);
        case PT_TRIANGLESTRIP:
            return PIPELINE_TYPE_TRIANGLESTRIP;
        case PT_TRIANGLES:
            return PIPELINE_TYPE_TRIANGLE;
    }
}

using pipeline_mode_value_t = uint16_t;
struct PIPELINE_MODE {
    eBlendMode blend = ALPHA_NONE;
    eCullMode cull = CULL_NONE;
    eCMPFUNC depth_cmp = CMP_GREATER;
    bool depth_write = true;
    
    pipeline_mode_value_t GetValue() const;
    void FromValue(pipeline_mode_value_t value);
};

const uint32_t PIPELINE_ID_MODE_MASK = 0xFFFF;
const uint32_t PIPELINE_ID_MODE_BITS = 8;
static_assert((1 << PIPELINE_ID_MODE_BITS) - 1 < PIPELINE_ID_MODE_MASK);
const uint32_t PIPELINE_ID_VERTEX_FMT_BITS = (VERTEX_FMT_BITS - 1); //Remove 1 dedicated for Dot3 flag in D3D
const uint32_t PIPELINE_ID_VERTEX_FMT_MASK = (1 << PIPELINE_ID_VERTEX_FMT_BITS) - 1;
const uint32_t PIPELINE_ID_TYPE_MASK = 0xF;

const uint32_t PIPELINE_ID_MODE_SHIFT = 0;
const uint32_t PIPELINE_ID_VERTEX_FMT_SHIFT = 16;
const uint32_t PIPELINE_ID_TYPE_SHIFT = PIPELINE_ID_VERTEX_FMT_SHIFT + PIPELINE_ID_VERTEX_FMT_BITS;

const uint32_t PERIMETER_SOKOL_PIPELINES_MAX = 128;

struct SokolPipeline {
    //Pipeline ID
    pipeline_id_t id;
    //Pipeline type
    PIPELINE_TYPE type;
    //VERTEX_FMT_* flags used as index for this pipeline
    vertex_fmt_t vertex_fmt;
    //Amount of bytes for this vertex fmt
    size_t vertex_size;
    //Created pipeline for this
    sg_pipeline pipeline;
    //Shader functions to retrieve info
    struct shader_funcs* shader_funcs;
    
    SokolPipeline() = default;
    ~SokolPipeline();
};

#endif //PERIMETER_SOKOLRENDERPIPELINE_H
