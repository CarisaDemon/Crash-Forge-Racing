/*
 * Native glTF/GLB skeletal racer loader.
 *
 * Asset data is immutable and shared. Animation pose + skinned triangle data
 * is per Instance so split-screen players can use the same character without
 * overwriting each other's pose.
 */
#include <common.h>
#include <platform.h>
#include <platform/native_assets.h>
#include <platform/native_gltf.h>
#include <platform/native_obj.h>
#include <platform/native_renderer.h>
#include <CharacterRegistry.h>

#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "../externals/cgltf/cgltf.h"

#define NATIVE_GLTF_MAX_TRIANGLES 100000
#define NATIVE_GLTF_MAX_JOINTS 256
#define NATIVE_GLTF_MAX_MATERIALS 256
#define NATIVE_GLTF_MAX_CLIPS 256
#define NATIVE_GLTF_MAX_CHANNELS_PER_CLIP 1024
#define NATIVE_GLTF_MAX_TEXTURE_DIM 8192
#define NATIVE_GLTF_MAX_TEXTURE_BYTES (64u * 1024u * 1024u)
#define NATIVE_GLTF_MAX_FILE_BYTES (256u * 1024u * 1024u)
#define NATIVE_GLTF_MAX_STATES 32
#define NATIVE_GLTF_EPSILON 0.000001f

enum NativeGltfChannelPath
{
    NATIVE_GLTF_CHANNEL_TRANSLATION = 0,
    NATIVE_GLTF_CHANNEL_ROTATION,
    NATIVE_GLTF_CHANNEL_SCALE,
};

struct NativeGltfSourceVertex
{
    float position[3];
    float uv[2];
    float color[3];
    u16 joints[4];
    float weights[4];
};

struct NativeGltfSourceTriangle
{
    struct NativeGltfSourceVertex v[3];
    int material;
};

struct NativeGltfJoint
{
    int parent;
    float restTranslation[3];
    float restRotation[4];
    float restScale[3];
    float prefix[16];
    float inverseBind[16];
};

struct NativeGltfChannel
{
    int joint;
    int path;
    int interpolation;
    int keyCount;
    int valueSize;
    float *times;
    float *values;
};

struct NativeGltfClip
{
    char name[64];
    float duration;
    int channelCount;
    struct NativeGltfChannel *channels;
};

struct NativeGltfAsset;

struct NativeGltfDescriptor
{
    struct Model model;
    struct ModelHeader header;
    struct ModelFrame frame;
    struct NativeGltfAsset *asset;
};

struct NativeGltfAsset
{
    char key[256];
    char slug[64];
    char relativePath[320];
    int characterID;
    int failed;

    struct NativeGltfSourceTriangle *triangles;
    int triangleCount;

    struct NativeObjMaterial *materials;
    int materialCount;

    struct NativeGltfJoint *joints;
    int jointCount;

    struct NativeGltfClip *clips;
    int clipCount;

    float extent[3];
    float normPerAuth[3];
    float authPerNorm[3];
    s16 frameOrigin[3];
    float inverseMeshWorld[16];

    float userScale;
    float userOffset[3];

    struct NativeGltfDescriptor *descriptor;
    struct NativeGltfAsset *next;
};

struct NativeGltfState
{
    const struct Instance *instance;
    struct NativeGltfAsset *asset;
    u64 lastPresentSerial;
    u64 lastTimeNS;
    u64 lastSeenSerial;
    u32 rng;

    int currentClip;
    int oneShot;
    float clipTime;
    float idleCountdown;
    float fadeElapsed;
    float fadeDuration;
    int poseValid;

    float *translation;
    float *rotation;
    float *scale;
    float *fromTranslation;
    float *fromRotation;
    float *fromScale;
    float *localMatrices;
    float *worldMatrices;
    float *skinMatrices;
    u8 *worldReady;

    struct NativeObjMesh renderMesh;
};

static struct NativeGltfAsset *s_gltfAssets;
static struct NativeGltfState s_gltfStates[NATIVE_GLTF_MAX_STATES];

static void NativeGltf_MatIdentity(float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void NativeGltf_MatCopy(float dst[16], const float src[16])
{
    memcpy(dst, src, sizeof(float) * 16);
}

static void NativeGltf_MatMul(float out[16], const float a[16], const float b[16])
{
    float temp[16];

    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            temp[column * 4 + row] =
                a[0 * 4 + row] * b[column * 4 + 0] +
                a[1 * 4 + row] * b[column * 4 + 1] +
                a[2 * 4 + row] * b[column * 4 + 2] +
                a[3 * 4 + row] * b[column * 4 + 3];
        }
    }

    memcpy(out, temp, sizeof(temp));
}

static int NativeGltf_MatInverse(float out[16], const float input[16])
{
    float a[4][8];

    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
            a[row][column] = input[column * 4 + row];

        for (int column = 0; column < 4; ++column)
            a[row][4 + column] = row == column ? 1.0f : 0.0f;
    }

    for (int pivot = 0; pivot < 4; ++pivot)
    {
        int best = pivot;
        float bestAbs = fabsf(a[pivot][pivot]);

        for (int row = pivot + 1; row < 4; ++row)
        {
            float value = fabsf(a[row][pivot]);
            if (value > bestAbs)
            {
                bestAbs = value;
                best = row;
            }
        }

        if (bestAbs < NATIVE_GLTF_EPSILON)
            return 0;

        if (best != pivot)
        {
            for (int column = 0; column < 8; ++column)
            {
                float swap = a[pivot][column];
                a[pivot][column] = a[best][column];
                a[best][column] = swap;
            }
        }

        {
            float inv = 1.0f / a[pivot][pivot];
            for (int column = 0; column < 8; ++column)
                a[pivot][column] *= inv;
        }

        for (int row = 0; row < 4; ++row)
        {
            float factor;
            if (row == pivot)
                continue;

            factor = a[row][pivot];
            if (fabsf(factor) < NATIVE_GLTF_EPSILON)
                continue;

            for (int column = 0; column < 8; ++column)
                a[row][column] -= factor * a[pivot][column];
        }
    }

    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            out[column * 4 + row] = a[row][4 + column];

    return 1;
}

static void NativeGltf_QuatNormalize(float q[4])
{
    float length =
        sqrtf(q[0] * q[0] + q[1] * q[1] +
              q[2] * q[2] + q[3] * q[3]);

    if (length < NATIVE_GLTF_EPSILON)
    {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }

    for (int i = 0; i < 4; ++i)
        q[i] /= length;
}

static void NativeGltf_QuatSlerp(float out[4], const float a[4], const float bIn[4], float t)
{
    float b[4] = { bIn[0], bIn[1], bIn[2], bIn[3] };
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];

    if (dot < 0.0f)
    {
        dot = -dot;
        for (int i = 0; i < 4; ++i)
            b[i] = -b[i];
    }

    if (dot > 0.9995f)
    {
        for (int i = 0; i < 4; ++i)
            out[i] = a[i] + (b[i] - a[i]) * t;
        NativeGltf_QuatNormalize(out);
        return;
    }

    dot = fminf(1.0f, fmaxf(-1.0f, dot));

    {
        float theta = acosf(dot);
        float sinTheta = sinf(theta);
        float wa = sinf((1.0f - t) * theta) / sinTheta;
        float wb = sinf(t * theta) / sinTheta;

        for (int i = 0; i < 4; ++i)
            out[i] = a[i] * wa + b[i] * wb;
    }
}

static void NativeGltf_MatFromTRS(
    float out[16],
    const float translation[3],
    const float rotationIn[4],
    const float scale[3])
{
    float q[4] = {
        rotationIn[0], rotationIn[1], rotationIn[2], rotationIn[3]
    };
    float x, y, z, w;
    float xx, yy, zz, xy, xz, yz, wx, wy, wz;

    NativeGltf_QuatNormalize(q);
    x = q[0]; y = q[1]; z = q[2]; w = q[3];
    xx = x * x; yy = y * y; zz = z * z;
    xy = x * y; xz = x * z; yz = y * z;
    wx = w * x; wy = w * y; wz = w * z;

    NativeGltf_MatIdentity(out);

    out[0] = (1.0f - 2.0f * (yy + zz)) * scale[0];
    out[1] = (2.0f * (xy + wz)) * scale[0];
    out[2] = (2.0f * (xz - wy)) * scale[0];

    out[4] = (2.0f * (xy - wz)) * scale[1];
    out[5] = (1.0f - 2.0f * (xx + zz)) * scale[1];
    out[6] = (2.0f * (yz + wx)) * scale[1];

    out[8] = (2.0f * (xz + wy)) * scale[2];
    out[9] = (2.0f * (yz - wx)) * scale[2];
    out[10] = (1.0f - 2.0f * (xx + yy)) * scale[2];

    out[12] = translation[0];
    out[13] = translation[1];
    out[14] = translation[2];
}

static void NativeGltf_QuatFromBasis(float out[4], const float m[16])
{
    float trace = m[0] + m[5] + m[10];

    if (trace > 0.0f)
    {
        float s = sqrtf(trace + 1.0f) * 2.0f;
        out[3] = 0.25f * s;
        out[0] = (m[6] - m[9]) / s;
        out[1] = (m[8] - m[2]) / s;
        out[2] = (m[1] - m[4]) / s;
    }
    else if (m[0] > m[5] && m[0] > m[10])
    {
        float s = sqrtf(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        out[3] = (m[6] - m[9]) / s;
        out[0] = 0.25f * s;
        out[1] = (m[4] + m[1]) / s;
        out[2] = (m[8] + m[2]) / s;
    }
    else if (m[5] > m[10])
    {
        float s = sqrtf(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        out[3] = (m[8] - m[2]) / s;
        out[0] = (m[4] + m[1]) / s;
        out[1] = 0.25f * s;
        out[2] = (m[9] + m[6]) / s;
    }
    else
    {
        float s = sqrtf(1.0f + m[10] - m[0] - m[5]) * 2.0f;
        out[3] = (m[1] - m[4]) / s;
        out[0] = (m[8] + m[2]) / s;
        out[1] = (m[9] + m[6]) / s;
        out[2] = 0.25f * s;
    }

    NativeGltf_QuatNormalize(out);
}

static void NativeGltf_DecomposeMatrix(
    const float m[16],
    float translation[3],
    float rotation[4],
    float scale[3])
{
    float basis[16];

    translation[0] = m[12];
    translation[1] = m[13];
    translation[2] = m[14];

    scale[0] = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    scale[1] = sqrtf(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    scale[2] = sqrtf(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);

    for (int i = 0; i < 3; ++i)
        if (scale[i] < NATIVE_GLTF_EPSILON)
            scale[i] = 1.0f;

    NativeGltf_MatIdentity(basis);
    basis[0] = m[0] / scale[0];
    basis[1] = m[1] / scale[0];
    basis[2] = m[2] / scale[0];
    basis[4] = m[4] / scale[1];
    basis[5] = m[5] / scale[1];
    basis[6] = m[6] / scale[1];
    basis[8] = m[8] / scale[2];
    basis[9] = m[9] / scale[2];
    basis[10] = m[10] / scale[2];

    NativeGltf_QuatFromBasis(rotation, basis);
}

static void NativeGltf_TransformPoint(
    const float m[16],
    const float p[3],
    float out[3])
{
    out[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    out[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
}

static unsigned char NativeGltf_SaturateU8(float value)
{
    int ivalue = (int)value;
    if (ivalue < 0) return 0;
    if (ivalue > 255) return 255;
    return (unsigned char)ivalue;
}

static u32 NativeGltf_RandomNext(u32 *state)
{
    u32 x = *state ? *state : 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float NativeGltf_Random01(u32 *state)
{
    return (float)(NativeGltf_RandomNext(state) & 0x00ffffffu) /
           16777216.0f;
}

static unsigned int NativeGltf_LoadEmbeddedTexture(const cgltf_image *image)
{
    const u8 *bytes;
    cgltf_size byteCount;
    SDL_IOStream *stream;
    SDL_Surface *source;
    SDL_Surface *rgba;
    unsigned int texture = 0;

    if (image == NULL || image->buffer_view == NULL)
        return 0;

    bytes = cgltf_buffer_view_data(image->buffer_view);
    byteCount = image->buffer_view->size;
    if (bytes == NULL || byteCount == 0)
        return 0;

    if (image->mime_type != NULL &&
        SDL_strcasecmp(image->mime_type, "image/png") != 0)
    {
        SDL_Log("GLB image %s: only embedded PNG is supported for now",
                image->name ? image->name : "(unnamed)");
        return 0;
    }

    stream = SDL_IOFromConstMem(bytes, (size_t)byteCount);
    if (stream == NULL)
        return 0;

    source = SDL_LoadPNG_IO(stream, true);
    if (source == NULL)
        return 0;

    if (source->w <= 0 ||
        source->h <= 0 ||
        source->w > NATIVE_GLTF_MAX_TEXTURE_DIM ||
        source->h > NATIVE_GLTF_MAX_TEXTURE_DIM ||
        (size_t)source->w * (size_t)source->h * 4u >
            (size_t)NATIVE_GLTF_MAX_TEXTURE_BYTES)
    {
        SDL_Log(
            "GLB image %s: texture dimensions exceed safety limit",
            image->name ? image->name : "(unnamed)");
        SDL_DestroySurface(source);
        return 0;
    }

    rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(source);
    if (rgba == NULL)
        return 0;

    {
        size_t rowBytes = (size_t)rgba->w * 4u;
        size_t totalBytes = rowBytes * (size_t)rgba->h;
        u8 *pixels = (u8 *)malloc(totalBytes);

        if (pixels != NULL)
        {
            for (int y = 0; y < rgba->h; ++y)
            {
                memcpy(
                    pixels + (size_t)y * rowBytes,
                    (const u8 *)rgba->pixels + (size_t)y * (size_t)rgba->pitch,
                    rowBytes);
            }

            texture = NativeRenderer_CreateImageTexture(
                pixels, rgba->w, rgba->h);
            free(pixels);
        }
    }

    SDL_DestroySurface(rgba);
    return texture;
}

static int NativeGltf_LoadMaterials(
    struct NativeGltfAsset *asset,
    const cgltf_data *data)
{
    if (data->materials_count >= NATIVE_GLTF_MAX_MATERIALS)
    {
        SDL_Log("GLB %s: too many materials (%d)",
                asset->slug, (int)data->materials_count);
        return 0;
    }

    asset->materialCount = (int)data->materials_count + 1;
    asset->materials = (struct NativeObjMaterial *)calloc(
        (size_t)asset->materialCount, sizeof(*asset->materials));

    if (asset->materials == NULL)
        return 0;

    snprintf(asset->materials[0].name,
             sizeof(asset->materials[0].name), "%s", "default");
    asset->materials[0].color[0] = 1.0f;
    asset->materials[0].color[1] = 1.0f;
    asset->materials[0].color[2] = 1.0f;
    asset->materials[0].opacity = 1.0f;

    for (cgltf_size i = 0; i < data->materials_count; ++i)
    {
        const cgltf_material *source = &data->materials[i];
        struct NativeObjMaterial *material = &asset->materials[i + 1];
        const cgltf_texture_view *baseTexture = NULL;

        snprintf(
            material->name,
            sizeof(material->name),
            "%s",
            source->name ? source->name : "material");

        material->color[0] = 1.0f;
        material->color[1] = 1.0f;
        material->color[2] = 1.0f;
        material->opacity = 1.0f;

        if (source->has_pbr_metallic_roughness)
        {
            const cgltf_pbr_metallic_roughness *pbr =
                &source->pbr_metallic_roughness;

            material->color[0] = pbr->base_color_factor[0];
            material->color[1] = pbr->base_color_factor[1];
            material->color[2] = pbr->base_color_factor[2];
            material->opacity = pbr->base_color_factor[3];
            baseTexture = &pbr->base_color_texture;
        }

        for (int component = 0; component < 3; ++component)
        {
            if (!isfinite(material->color[component]))
                material->color[component] = 1.0f;
            material->color[component] =
                fminf(1.0f, fmaxf(0.0f, material->color[component]));
        }

        if (!isfinite(material->opacity))
            material->opacity = 1.0f;
        material->opacity =
            fminf(1.0f, fmaxf(0.0f, material->opacity));

        if (baseTexture != NULL &&
            baseTexture->texture != NULL &&
            baseTexture->texture->image != NULL)
        {
            material->texture =
                NativeGltf_LoadEmbeddedTexture(
                    baseTexture->texture->image);

            if (material->texture == 0)
            {
                SDL_Log(
                    "GLB %s: failed to load base-color texture for material %s",
                    asset->slug,
                    material->name);
                return 0;
            }
        }
    }

    return 1;
}

static int NativeGltf_FindJointIndex(
    const cgltf_skin *skin,
    const cgltf_node *node)
{
    if (skin == NULL || node == NULL)
        return -1;

    for (cgltf_size i = 0; i < skin->joints_count; ++i)
        if (skin->joints[i] == node)
            return (int)i;

    return -1;
}

static int NativeGltf_LoadJoints(
    struct NativeGltfAsset *asset,
    const cgltf_node *meshNode)
{
    const cgltf_skin *skin = meshNode ? meshNode->skin : NULL;
    float meshWorld[16];

    NativeGltf_MatIdentity(asset->inverseMeshWorld);

    if (meshNode == NULL)
        return 0;

    cgltf_node_transform_world(meshNode, meshWorld);
    if (!NativeGltf_MatInverse(asset->inverseMeshWorld, meshWorld))
    {
        SDL_Log("GLB %s: mesh transform is not invertible", asset->slug);
        return 0;
    }

    if (skin == NULL || skin->joints_count == 0)
    {
        asset->jointCount = 0;
        return 1;
    }

    if (skin->joints_count > NATIVE_GLTF_MAX_JOINTS)
    {
        SDL_Log("GLB %s: %d joints exceeds safety limit %d",
                asset->slug, (int)skin->joints_count,
                NATIVE_GLTF_MAX_JOINTS);
        return 0;
    }

    asset->jointCount = (int)skin->joints_count;
    asset->joints = (struct NativeGltfJoint *)calloc(
        (size_t)asset->jointCount, sizeof(*asset->joints));

    if (asset->joints == NULL)
        return 0;

    for (int jointIndex = 0;
         jointIndex < asset->jointCount;
         ++jointIndex)
    {
        const cgltf_node *node = skin->joints[jointIndex];
        struct NativeGltfJoint *joint = &asset->joints[jointIndex];
        const cgltf_node *parentNode = node ? node->parent : NULL;
        float prefix[16];

        joint->parent = -1;
        NativeGltf_MatIdentity(prefix);

        while (parentNode != NULL)
        {
            int parentJoint =
                NativeGltf_FindJointIndex(skin, parentNode);

            if (parentJoint >= 0)
            {
                joint->parent = parentJoint;
                break;
            }

            {
                float local[16];
                float combined[16];
                cgltf_node_transform_local(parentNode, local);
                NativeGltf_MatMul(combined, local, prefix);
                NativeGltf_MatCopy(prefix, combined);
            }

            parentNode = parentNode->parent;
        }

        NativeGltf_MatCopy(joint->prefix, prefix);

        if (node != NULL && node->has_matrix)
        {
            NativeGltf_DecomposeMatrix(
                node->matrix,
                joint->restTranslation,
                joint->restRotation,
                joint->restScale);
        }
        else
        {
            joint->restTranslation[0] =
                (node && node->has_translation) ?
                node->translation[0] : 0.0f;
            joint->restTranslation[1] =
                (node && node->has_translation) ?
                node->translation[1] : 0.0f;
            joint->restTranslation[2] =
                (node && node->has_translation) ?
                node->translation[2] : 0.0f;

            joint->restRotation[0] =
                (node && node->has_rotation) ?
                node->rotation[0] : 0.0f;
            joint->restRotation[1] =
                (node && node->has_rotation) ?
                node->rotation[1] : 0.0f;
            joint->restRotation[2] =
                (node && node->has_rotation) ?
                node->rotation[2] : 0.0f;
            joint->restRotation[3] =
                (node && node->has_rotation) ?
                node->rotation[3] : 1.0f;

            joint->restScale[0] =
                (node && node->has_scale) ?
                node->scale[0] : 1.0f;
            joint->restScale[1] =
                (node && node->has_scale) ?
                node->scale[1] : 1.0f;
            joint->restScale[2] =
                (node && node->has_scale) ?
                node->scale[2] : 1.0f;
        }

        NativeGltf_QuatNormalize(joint->restRotation);
        NativeGltf_MatIdentity(joint->inverseBind);

        if (skin->inverse_bind_matrices != NULL &&
            (cgltf_size)jointIndex <
                skin->inverse_bind_matrices->count)
        {
            if (!cgltf_accessor_read_float(
                    skin->inverse_bind_matrices,
                    (cgltf_size)jointIndex,
                    joint->inverseBind,
                    16))
            {
                SDL_Log("GLB %s: invalid inverse bind matrix %d",
                        asset->slug, jointIndex);
                return 0;
            }
        }
    }

    return 1;
}

static int NativeGltf_CopyAnimationChannel(
    struct NativeGltfChannel *dst,
    const cgltf_animation_channel *source,
    const cgltf_skin *skin)
{
    const cgltf_animation_sampler *sampler;
    int joint;
    int valueSize;
    int path;

    memset(dst, 0, sizeof(*dst));

    if (source == NULL ||
        source->target_node == NULL ||
        source->sampler == NULL)
        return 0;

    joint = NativeGltf_FindJointIndex(
        skin, source->target_node);
    if (joint < 0)
        return 0;

    if (source->target_path ==
        cgltf_animation_path_type_translation)
    {
        path = NATIVE_GLTF_CHANNEL_TRANSLATION;
        valueSize = 3;
    }
    else if (source->target_path ==
             cgltf_animation_path_type_rotation)
    {
        path = NATIVE_GLTF_CHANNEL_ROTATION;
        valueSize = 4;
    }
    else if (source->target_path ==
             cgltf_animation_path_type_scale)
    {
        path = NATIVE_GLTF_CHANNEL_SCALE;
        valueSize = 3;
    }
    else
    {
        return 0;
    }

    sampler = source->sampler;
    if (sampler->input == NULL ||
        sampler->output == NULL ||
        sampler->input->count == 0 ||
        sampler->input->count > 100000)
        return 0;

    dst->joint = joint;
    dst->path = path;
    dst->valueSize = valueSize;
    dst->keyCount = (int)sampler->input->count;
    dst->interpolation =
        sampler->interpolation ==
            cgltf_interpolation_type_step ? 1 : 0;

    dst->times = (float *)malloc(
        (size_t)dst->keyCount * sizeof(float));
    dst->values = (float *)malloc(
        (size_t)dst->keyCount *
        (size_t)valueSize * sizeof(float));

    if (dst->times == NULL || dst->values == NULL)
        return 0;

    for (int key = 0; key < dst->keyCount; ++key)
    {
        cgltf_float time = 0.0f;
        cgltf_size outputIndex = (cgltf_size)key;
        cgltf_float value[4] = { 0, 0, 0, 1 };

        if (!cgltf_accessor_read_float(
                sampler->input,
                (cgltf_size)key,
                &time,
                1))
            return 0;

        if (sampler->interpolation ==
            cgltf_interpolation_type_cubic_spline)
        {
            outputIndex = (cgltf_size)key * 3u + 1u;
        }

        if (outputIndex >= sampler->output->count ||
            !cgltf_accessor_read_float(
                sampler->output,
                outputIndex,
                value,
                (cgltf_size)valueSize))
            return 0;

        if (!isfinite(time) ||
            time < 0.0f ||
            time > 3600.0f ||
            (key > 0 && time < dst->times[key - 1]))
            return 0;

        for (int component = 0;
             component < valueSize;
             ++component)
        {
            if (!isfinite(value[component]))
                return 0;

            if (path == NATIVE_GLTF_CHANNEL_TRANSLATION &&
                fabsf(value[component]) > 1000000.0f)
                return 0;

            if (path == NATIVE_GLTF_CHANNEL_SCALE &&
                (value[component] <= 0.0001f ||
                 value[component] > 1000.0f))
                return 0;
        }

        dst->times[key] = time;
        memcpy(
            &dst->values[(size_t)key * (size_t)valueSize],
            value,
            (size_t)valueSize * sizeof(float));
    }

    return 1;
}

static void NativeGltf_FreeChannel(
    struct NativeGltfChannel *channel)
{
    if (channel == NULL)
        return;

    free(channel->times);
    free(channel->values);
    memset(channel, 0, sizeof(*channel));
}

static int NativeGltf_LoadClips(
    struct NativeGltfAsset *asset,
    const cgltf_data *data,
    const cgltf_skin *skin)
{
    if (skin == NULL || data->animations_count == 0)
        return 1;

    if (data->animations_count > NATIVE_GLTF_MAX_CLIPS)
    {
        SDL_Log("GLB %s: too many animation clips (%d)",
                asset->slug, (int)data->animations_count);
        return 0;
    }

    asset->clips = (struct NativeGltfClip *)calloc(
        data->animations_count, sizeof(*asset->clips));
    if (asset->clips == NULL)
        return 0;

    for (cgltf_size animationIndex = 0;
         animationIndex < data->animations_count;
         ++animationIndex)
    {
        const cgltf_animation *animation =
            &data->animations[animationIndex];
        struct NativeGltfClip clip;
        int validChannels = 0;

        memset(&clip, 0, sizeof(clip));
        snprintf(
            clip.name,
            sizeof(clip.name),
            "%s",
            animation->name ?
                animation->name : "animation");

        if (animation->channels_count >
            NATIVE_GLTF_MAX_CHANNELS_PER_CLIP)
        {
            SDL_Log(
                "GLB %s: clip %s has too many channels (%d)",
                asset->slug,
                clip.name,
                (int)animation->channels_count);
            return 0;
        }

        if (animation->channels_count > 0)
        {
            clip.channels = (struct NativeGltfChannel *)calloc(
                animation->channels_count,
                sizeof(*clip.channels));
            if (clip.channels == NULL)
                return 0;
        }

        for (cgltf_size channelIndex = 0;
             channelIndex < animation->channels_count;
             ++channelIndex)
        {
            struct NativeGltfChannel channel;

            if (!NativeGltf_CopyAnimationChannel(
                    &channel,
                    &animation->channels[channelIndex],
                    skin))
            {
                NativeGltf_FreeChannel(&channel);
                continue;
            }

            clip.channels[validChannels++] = channel;

            if (channel.keyCount > 0)
            {
                float endTime =
                    channel.times[channel.keyCount - 1];
                if (endTime > clip.duration)
                    clip.duration = endTime;
            }
        }

        clip.channelCount = validChannels;

        if (validChannels == 0)
        {
            free(clip.channels);
            continue;
        }

        asset->clips[asset->clipCount++] = clip;
    }

    return 1;
}

static int NativeGltf_ReadSourceVertex(
    struct NativeGltfSourceVertex *dst,
    cgltf_size index,
    const cgltf_accessor *position,
    const cgltf_accessor *texcoord,
    const cgltf_accessor *color,
    const cgltf_accessor *joints,
    const cgltf_accessor *weights,
    int jointCount)
{
    cgltf_float value[4] = { 0, 0, 0, 1 };

    memset(dst, 0, sizeof(*dst));
    dst->color[0] = 1.0f;
    dst->color[1] = 1.0f;
    dst->color[2] = 1.0f;

    if (position == NULL ||
        index >= position->count ||
        !cgltf_accessor_read_float(
            position, index, value, 3))
        return 0;

    for (int component = 0; component < 3; ++component)
    {
        if (!isfinite(value[component]) ||
            fabsf(value[component]) > 1000000.0f)
            return 0;

        dst->position[component] = value[component];
    }

    if (texcoord != NULL && index < texcoord->count)
    {
        value[0] = value[1] = 0.0f;
        if (!cgltf_accessor_read_float(
                texcoord, index, value, 2))
            return 0;

        dst->uv[0] = isfinite(value[0]) ? value[0] : 0.0f;
        dst->uv[1] = isfinite(value[1]) ? value[1] : 0.0f;
    }

    if (color != NULL && index < color->count)
    {
        value[0] = value[1] = value[2] = value[3] = 1.0f;
        if (!cgltf_accessor_read_float(
                color, index, value, 4))
            return 0;

        for (int component = 0; component < 3; ++component)
        {
            float channel = isfinite(value[component]) ?
                value[component] : 1.0f;
            dst->color[component] =
                fminf(1.0f, fmaxf(0.0f, channel));
        }
    }

    if (jointCount > 0)
    {
        cgltf_uint jointValues[4] = { 0, 0, 0, 0 };
        cgltf_float weightValues[4] = { 0, 0, 0, 0 };
        float totalWeight = 0.0f;

        if (joints == NULL || weights == NULL ||
            index >= joints->count ||
            index >= weights->count)
            return 0;

        if (!cgltf_accessor_read_uint(
                joints, index, jointValues, 4) ||
            !cgltf_accessor_read_float(
                weights, index, weightValues, 4))
            return 0;

        for (int influence = 0; influence < 4; ++influence)
        {
            float weight = weightValues[influence];

            if (!isfinite(weight) || weight < 0.0f)
                weight = 0.0f;

            if ((int)jointValues[influence] >= jointCount)
                weight = 0.0f;

            dst->joints[influence] =
                (u16)jointValues[influence];
            dst->weights[influence] = weight;
            totalWeight += weight;
        }

        if (totalWeight > NATIVE_GLTF_EPSILON)
        {
            float invTotal = 1.0f / totalWeight;
            for (int influence = 0; influence < 4; ++influence)
                dst->weights[influence] *= invTotal;
        }
    }

    return 1;
}

static int NativeGltf_LoadMesh(
    struct NativeGltfAsset *asset,
    const cgltf_data *data,
    const cgltf_node *meshNode)
{
    const cgltf_mesh *mesh;
    int totalTriangles = 0;
    int outputTriangle = 0;
    float lo[3] = { 1e30f, 1e30f, 1e30f };
    float hi[3] = { -1e30f, -1e30f, -1e30f };

    if (meshNode == NULL || meshNode->mesh == NULL)
        return 0;

    mesh = meshNode->mesh;

    for (cgltf_size primitiveIndex = 0;
         primitiveIndex < mesh->primitives_count;
         ++primitiveIndex)
    {
        const cgltf_primitive *primitive =
            &mesh->primitives[primitiveIndex];
        const cgltf_accessor *position =
            cgltf_find_accessor(
                primitive,
                cgltf_attribute_type_position,
                0);
        cgltf_size elementCount;

        if (primitive->type != cgltf_primitive_type_triangles ||
            position == NULL)
            continue;

        elementCount = primitive->indices ?
            primitive->indices->count : position->count;

        if (elementCount < 3 ||
            (elementCount % 3u) != 0)
            continue;

        if (elementCount / 3u >
            (cgltf_size)(NATIVE_GLTF_MAX_TRIANGLES -
                         totalTriangles))
        {
            SDL_Log("GLB %s: triangle safety limit exceeded",
                    asset->slug);
            return 0;
        }

        totalTriangles += (int)(elementCount / 3u);
    }

    if (totalTriangles <= 0)
    {
        SDL_Log("GLB %s: no triangle primitives found",
                asset->slug);
        return 0;
    }

    asset->triangles =
        (struct NativeGltfSourceTriangle *)calloc(
            (size_t)totalTriangles,
            sizeof(*asset->triangles));
    if (asset->triangles == NULL)
        return 0;

    for (cgltf_size primitiveIndex = 0;
         primitiveIndex < mesh->primitives_count;
         ++primitiveIndex)
    {
        const cgltf_primitive *primitive =
            &mesh->primitives[primitiveIndex];
        const cgltf_accessor *position =
            cgltf_find_accessor(
                primitive,
                cgltf_attribute_type_position,
                0);
        const cgltf_accessor *texcoord =
            cgltf_find_accessor(
                primitive,
                cgltf_attribute_type_texcoord,
                0);
        const cgltf_accessor *color =
            cgltf_find_accessor(
                primitive,
                cgltf_attribute_type_color,
                0);
        const cgltf_accessor *joints =
            cgltf_find_accessor(
                primitive,
                cgltf_attribute_type_joints,
                0);
        const cgltf_accessor *weights =
            cgltf_find_accessor(
                primitive,
                cgltf_attribute_type_weights,
                0);
        cgltf_size elementCount;
        int materialIndex = 0;

        if (primitive->type != cgltf_primitive_type_triangles ||
            position == NULL)
            continue;

        elementCount = primitive->indices ?
            primitive->indices->count : position->count;

        if (elementCount < 3 ||
            (elementCount % 3u) != 0)
            continue;

        if (primitive->material != NULL)
        {
            cgltf_size gltfMaterial =
                cgltf_material_index(
                    data, primitive->material);

            if (gltfMaterial < data->materials_count)
                materialIndex = (int)gltfMaterial + 1;
        }

        for (cgltf_size element = 0;
             element < elementCount;
             element += 3u)
        {
            struct NativeGltfSourceTriangle *triangle =
                &asset->triangles[outputTriangle];

            triangle->material = materialIndex;

            for (int corner = 0; corner < 3; ++corner)
            {
                cgltf_size vertexIndex =
                    primitive->indices ?
                    cgltf_accessor_read_index(
                        primitive->indices,
                        element + (cgltf_size)corner) :
                    element + (cgltf_size)corner;

                if (vertexIndex >= position->count ||
                    !NativeGltf_ReadSourceVertex(
                        &triangle->v[corner],
                        vertexIndex,
                        position,
                        texcoord,
                        color,
                        joints,
                        weights,
                        asset->jointCount))
                {
                    SDL_Log(
                        "GLB %s: invalid vertex data",
                        asset->slug);
                    return 0;
                }

                for (int axis = 0; axis < 3; ++axis)
                {
                    float value =
                        triangle->v[corner].position[axis];
                    lo[axis] = fminf(lo[axis], value);
                    hi[axis] = fmaxf(hi[axis], value);
                }
            }

            outputTriangle++;
        }
    }

    asset->triangleCount = outputTriangle;
    if (asset->triangleCount <= 0)
        return 0;

    for (int axis = 0; axis < 3; ++axis)
    {
        float extent = fmaxf(
            hi[axis] - lo[axis], 0.001f);
        float origin;

        /*
         * The native ModelHeader stores scale in signed 16-bit units.
         * Character GLBs authored in Blender are normally only a few units
         * tall, so this is a format guard rather than a PS1 memory cap.
         */
        if (!isfinite(extent) || extent > 32.0f)
        {
            SDL_Log(
                "GLB %s: authored extent %.3f on axis %d is too large; apply Blender scale before export",
                asset->slug, extent, axis);
            return 0;
        }

        asset->extent[axis] = extent;
        asset->normPerAuth[axis] = 1016.0f / extent;
        asset->authPerNorm[axis] = extent / 1016.0f;

        origin = floorf(
            lo[axis] / extent * 254.0f);

        if (origin < -32767.0f ||
            origin > 32767.0f)
            return 0;

        asset->frameOrigin[axis] =
            (s16)origin;
    }

    return 1;
}

static void NativeGltf_FreeAsset(
    struct NativeGltfAsset *asset)
{
    if (asset == NULL)
        return;

    if (asset->materials != NULL)
    {
        for (int i = 0; i < asset->materialCount; ++i)
            NativeRenderer_ReleaseImageTexture(
                asset->materials[i].texture);
    }

    if (asset->clips != NULL)
    {
        for (int clipIndex = 0;
             clipIndex < asset->clipCount;
             ++clipIndex)
        {
            struct NativeGltfClip *clip =
                &asset->clips[clipIndex];

            for (int channelIndex = 0;
                 channelIndex < clip->channelCount;
                 ++channelIndex)
            {
                NativeGltf_FreeChannel(
                    &clip->channels[channelIndex]);
            }

            free(clip->channels);
        }
    }

    free(asset->clips);
    free(asset->joints);
    free(asset->materials);
    free(asset->triangles);
    free(asset->descriptor);
    free(asset);
}

static struct NativeGltfAsset *NativeGltf_FindAsset(
    const char *key)
{
    for (struct NativeGltfAsset *asset = s_gltfAssets;
         asset != NULL;
         asset = asset->next)
    {
        if (!strcmp(asset->key, key))
            return asset;
    }

    return NULL;
}

static int NativeGltf_FileSizeIsSafe(const char *hostPath)
{
    FILE *file;
    long size;

    if (hostPath == NULL || !*hostPath)
        return 0;

    file = fopen(hostPath, "rb");
    if (file == NULL)
        return 0;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 0;
    }

    size = ftell(file);
    fclose(file);

    return size > 0 &&
           (unsigned long)size <=
               (unsigned long)NATIVE_GLTF_MAX_FILE_BYTES;
}

static const cgltf_node *NativeGltf_FindMeshNode(
    const cgltf_data *data)
{
    const cgltf_node *fallback = NULL;

    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        const cgltf_node *node = &data->nodes[i];

        if (node->mesh == NULL)
            continue;

        if (fallback == NULL)
            fallback = node;

        if (node->skin != NULL)
            return node;
    }

    return fallback;
}

static int NativeGltf_CreateDescriptor(
    struct NativeGltfAsset *asset)
{
    struct NativeGltfDescriptor *descriptor;

    descriptor =
        (struct NativeGltfDescriptor *)calloc(
            1, sizeof(*descriptor));
    if (descriptor == NULL)
        return 0;

    snprintf(
        descriptor->model.name,
        sizeof(descriptor->model.name),
        "%s",
        asset->slug);
    descriptor->model.id = -1;
    descriptor->model.numHeaders = 1;
    descriptor->model.headers =
        &descriptor->header;

    snprintf(
        descriptor->header.name,
        sizeof(descriptor->header.name),
        "%s",
        asset->slug);
    descriptor->header.unk1 =
        NATIVE_GLTF_MODEL_MAGIC;
    descriptor->header.maxDistanceLOD = -1;

    descriptor->header.scale.x =
        (s16)fmaxf(
            1.0f,
            asset->extent[0] * 1000.0f);
    descriptor->header.scale.y =
        (s16)fmaxf(
            1.0f,
            asset->extent[1] * 1000.0f);
    descriptor->header.scale.z =
        (s16)fmaxf(
            1.0f,
            asset->extent[2] * 1000.0f);

    descriptor->header.ptrFrameData =
        &descriptor->frame;
    descriptor->frame.pos.x =
        asset->frameOrigin[0];
    descriptor->frame.pos.y =
        asset->frameOrigin[1];
    descriptor->frame.pos.z =
        asset->frameOrigin[2];
    descriptor->frame.vertexOffset =
        sizeof(descriptor->frame);

    descriptor->asset = asset;
    asset->descriptor = descriptor;
    return 1;
}

static struct NativeGltfAsset *NativeGltf_LoadAsset(
    int characterID)
{
    const struct CharacterDef *character =
        CharacterRegistry_GetByID(characterID);
    const char *configuredFile;
    const char *fileName;
    int format;
    char relativePath[320];
    char hostPath[1024];
    char key[256];
    struct NativeGltfAsset *asset;
    cgltf_options options;
    cgltf_data *data = NULL;
    cgltf_result result;
    const cgltf_node *meshNode;
    const cgltf_skin *skin;

    if (character == NULL)
        return NULL;

    format =
        CharacterRegistry_GetModelFormat(characterID);
    if (format == CHARACTER_MODEL_OBJ)
        return NULL;

    configuredFile =
        CharacterRegistry_GetModelFile(characterID);
    fileName = configuredFile ?
        configuredFile : "character.glb";

    if (configuredFile != NULL)
    {
        const char *extension =
            strrchr(configuredFile, '.');

        if (extension != NULL &&
            SDL_strcasecmp(extension, ".glb") != 0 &&
            SDL_strcasecmp(extension, ".gltf") != 0)
        {
            if (format == CHARACTER_MODEL_GLB)
                SDL_Log(
                    "GLB %s: configured model file is not glTF/GLB",
                    character->slug);
            return NULL;
        }
    }

    snprintf(
        relativePath,
        sizeof(relativePath),
        "mods/racers/%s/%s",
        character->slug,
        fileName);
    snprintf(
        key,
        sizeof(key),
        "%s|%s",
        character->slug,
        fileName);

    asset = NativeGltf_FindAsset(key);
    if (asset != NULL)
        return asset->failed ? NULL : asset;

    asset =
        (struct NativeGltfAsset *)calloc(
            1, sizeof(*asset));
    if (asset == NULL)
        return NULL;

    snprintf(asset->key, sizeof(asset->key), "%s", key);
    snprintf(
        asset->slug,
        sizeof(asset->slug),
        "%s",
        character->slug);
    snprintf(
        asset->relativePath,
        sizeof(asset->relativePath),
        "%s",
        relativePath);
    asset->characterID = characterID;
    asset->userScale = character->modelScale;
    asset->userOffset[0] = character->modelOffset[0];
    asset->userOffset[1] = character->modelOffset[1];
    asset->userOffset[2] = character->modelOffset[2];

    asset->next = s_gltfAssets;
    s_gltfAssets = asset;

    if (!NativeAssets_ResolvePath(
            relativePath,
            hostPath,
            sizeof(hostPath)))
    {
        asset->failed = 1;
        if (format == CHARACTER_MODEL_GLB)
            SDL_Log(
                "GLB %s: missing %s",
                character->slug,
                relativePath);
        return NULL;
    }

    if (!NativeGltf_FileSizeIsSafe(hostPath))
    {
        SDL_Log(
            "GLB %s: file is empty, unreadable, or exceeds %u MB safety limit",
            character->slug,
            (unsigned int)(NATIVE_GLTF_MAX_FILE_BYTES / (1024u * 1024u)));
        asset->failed = 1;
        return NULL;
    }

    memset(&options, 0, sizeof(options));
    result = cgltf_parse_file(
        &options, hostPath, &data);
    if (result != cgltf_result_success ||
        data == NULL)
    {
        SDL_Log("GLB %s: parse failed (%d)",
                character->slug, (int)result);
        asset->failed = 1;
        return NULL;
    }

    result = cgltf_load_buffers(
        &options, data, hostPath);
    if (result != cgltf_result_success)
    {
        SDL_Log("GLB %s: buffer load failed (%d)",
                character->slug, (int)result);
        cgltf_free(data);
        asset->failed = 1;
        return NULL;
    }

    result = cgltf_validate(data);
    if (result != cgltf_result_success)
    {
        SDL_Log("GLB %s: validation failed (%d)",
                character->slug, (int)result);
        cgltf_free(data);
        asset->failed = 1;
        return NULL;
    }

    meshNode = NativeGltf_FindMeshNode(data);
    if (meshNode == NULL)
    {
        SDL_Log("GLB %s: no mesh node", character->slug);
        cgltf_free(data);
        asset->failed = 1;
        return NULL;
    }

    skin = meshNode->skin;

    if (!NativeGltf_LoadMaterials(asset, data) ||
        !NativeGltf_LoadJoints(asset, meshNode) ||
        !NativeGltf_LoadMesh(asset, data, meshNode) ||
        !NativeGltf_LoadClips(asset, data, skin) ||
        !NativeGltf_CreateDescriptor(asset))
    {
        SDL_Log("GLB %s: unsupported or invalid character asset",
                character->slug);
        cgltf_free(data);
        asset->failed = 1;
        return NULL;
    }

    cgltf_free(data);

    SDL_Log(
        "GLB racer %s: loaded %d triangles, %d joints, %d clips",
        character->slug,
        asset->triangleCount,
        asset->jointCount,
        asset->clipCount);

    return asset;
}

static struct NativeGltfAsset *NativeGltf_GetAssetFromModel(
    const struct Model *model)
{
    const struct NativeGltfDescriptor *descriptor;

    if (!NativeGltf_IsModel(model))
        return NULL;

    descriptor =
        (const struct NativeGltfDescriptor *)model;
    return descriptor->asset;
}

static void NativeGltf_StateRelease(
    struct NativeGltfState *state)
{
    if (state == NULL)
        return;

    free(state->translation);
    free(state->rotation);
    free(state->scale);
    free(state->fromTranslation);
    free(state->fromRotation);
    free(state->fromScale);
    free(state->localMatrices);
    free(state->worldMatrices);
    free(state->skinMatrices);
    free(state->worldReady);
    free(state->renderMesh.triangles);

    memset(state, 0, sizeof(*state));
}

static void NativeGltf_PrepareRenderVertex(
    struct NativeObjVertex *dst,
    const struct NativeGltfSourceVertex *source,
    const struct NativeObjMaterial *material,
    int objIndex)
{
    const float colorScale =
        material->texture ? 128.0f : 255.0f;
    float u;
    float v;

    memset(dst, 0, sizeof(*dst));
    dst->objIndex = objIndex;
    dst->projectionIndex = objIndex;
    dst->uv[0] = source->uv[0];
    dst->uv[1] = source->uv[1];
    dst->color[0] = source->color[0];
    dst->color[1] = source->color[1];
    dst->color[2] = source->color[2];

    for (int component = 0; component < 3; ++component)
    {
        dst->renderColor[component] =
            NativeGltf_SaturateU8(
                source->color[component] *
                material->color[component] *
                colorScale);
    }

    u = source->uv[0];
    v = 1.0f - source->uv[1];

    u = fminf(1.0f, fmaxf(0.0f, u));
    v = fminf(1.0f, fmaxf(0.0f, v));

    dst->renderUV[0] =
        (unsigned char)(u * 255.0f);
    dst->renderUV[1] =
        (unsigned char)(v * 255.0f);
}

static void NativeGltf_SetRestPose(
    struct NativeGltfState *state)
{
    struct NativeGltfAsset *asset = state->asset;

    for (int joint = 0;
         joint < asset->jointCount;
         ++joint)
    {
        memcpy(
            &state->translation[joint * 3],
            asset->joints[joint].restTranslation,
            sizeof(float) * 3);
        memcpy(
            &state->rotation[joint * 4],
            asset->joints[joint].restRotation,
            sizeof(float) * 4);
        memcpy(
            &state->scale[joint * 3],
            asset->joints[joint].restScale,
            sizeof(float) * 3);
    }
}

static int NativeGltf_StateAllocateArrays(
    struct NativeGltfState *state)
{
    struct NativeGltfAsset *asset = state->asset;
    size_t joints = (size_t)asset->jointCount;

    if (asset->jointCount > 0)
    {
        state->translation =
            (float *)malloc(joints * 3u * sizeof(float));
        state->rotation =
            (float *)malloc(joints * 4u * sizeof(float));
        state->scale =
            (float *)malloc(joints * 3u * sizeof(float));
        state->fromTranslation =
            (float *)malloc(joints * 3u * sizeof(float));
        state->fromRotation =
            (float *)malloc(joints * 4u * sizeof(float));
        state->fromScale =
            (float *)malloc(joints * 3u * sizeof(float));
        state->localMatrices =
            (float *)malloc(joints * 16u * sizeof(float));
        state->worldMatrices =
            (float *)malloc(joints * 16u * sizeof(float));
        state->skinMatrices =
            (float *)malloc(joints * 16u * sizeof(float));
        state->worldReady =
            (u8 *)calloc(joints, sizeof(u8));

        if (state->translation == NULL ||
            state->rotation == NULL ||
            state->scale == NULL ||
            state->fromTranslation == NULL ||
            state->fromRotation == NULL ||
            state->fromScale == NULL ||
            state->localMatrices == NULL ||
            state->worldMatrices == NULL ||
            state->skinMatrices == NULL ||
            state->worldReady == NULL)
            return 0;

        NativeGltf_SetRestPose(state);
        memcpy(
            state->fromTranslation,
            state->translation,
            joints * 3u * sizeof(float));
        memcpy(
            state->fromRotation,
            state->rotation,
            joints * 4u * sizeof(float));
        memcpy(
            state->fromScale,
            state->scale,
            joints * 3u * sizeof(float));
    }

    state->renderMesh.triangles =
        (struct NativeObjTriangle *)calloc(
            (size_t)asset->triangleCount,
            sizeof(*state->renderMesh.triangles));

    if (state->renderMesh.triangles == NULL)
        return 0;

    state->renderMesh.materials =
        asset->materials;
    state->renderMesh.triangleCount =
        asset->triangleCount;
    state->renderMesh.materialCount =
        asset->materialCount;
    state->renderMesh.userScale =
        asset->userScale;
    state->renderMesh.userOffset[0] =
        asset->userOffset[0];
    state->renderMesh.userOffset[1] =
        asset->userOffset[1];
    state->renderMesh.userOffset[2] =
        asset->userOffset[2];
    state->renderMesh.wheelMask = 0;

    for (int axis = 0; axis < 3; ++axis)
    {
        state->renderMesh.extent[axis] =
            asset->extent[axis];
        state->renderMesh.authPerNorm[axis] =
            asset->authPerNorm[axis];
        state->renderMesh.normPerAuth[axis] =
            asset->normPerAuth[axis];
        state->renderMesh.frameOrigin[axis] =
            asset->frameOrigin[axis];
        state->renderMesh.renderOffsetScaled[axis] =
            asset->userOffset[axis] *
            asset->normPerAuth[axis] *
            asset->userScale;
    }

    for (int triangleIndex = 0;
         triangleIndex < asset->triangleCount;
         ++triangleIndex)
    {
        const struct NativeGltfSourceTriangle *source =
            &asset->triangles[triangleIndex];
        struct NativeObjTriangle *triangle =
            &state->renderMesh.triangles[triangleIndex];
        int materialIndex = source->material;

        if (materialIndex < 0 ||
            materialIndex >= asset->materialCount)
            materialIndex = 0;

        triangle->material = materialIndex;
        triangle->wheel = -1;
        triangle->renderTexture =
            asset->materials[materialIndex].texture;
        triangle->renderTextured =
            triangle->renderTexture != 0;

        for (int corner = 0;
             corner < 3;
             ++corner)
        {
            NativeGltf_PrepareRenderVertex(
                &triangle->v[corner],
                &source->v[corner],
                &asset->materials[materialIndex],
                triangleIndex * 3 + corner);
        }
    }

    return 1;
}

static void NativeGltf_ResetIdleCountdown(
    struct NativeGltfState *state)
{
    float minDelay =
        CharacterRegistry_GetIdleMinDelay(
            state->asset->characterID);
    float maxDelay =
        CharacterRegistry_GetIdleMaxDelay(
            state->asset->characterID);

    if (maxDelay < minDelay)
    {
        float swap = minDelay;
        minDelay = maxDelay;
        maxDelay = swap;
    }

    state->idleCountdown =
        minDelay +
        (maxDelay - minDelay) *
        NativeGltf_Random01(&state->rng);
}

static struct NativeGltfState *NativeGltf_GetState(
    const struct Instance *inst,
    struct NativeGltfAsset *asset)
{
    struct NativeGltfState *slot = NULL;
    struct NativeGltfState *oldest = NULL;

    for (int i = 0; i < NATIVE_GLTF_MAX_STATES; ++i)
    {
        struct NativeGltfState *state =
            &s_gltfStates[i];

        if (state->instance == inst &&
            state->asset == asset)
        {
            state->lastSeenSerial =
                Platform_GetHostPresentCount();
            return state;
        }

        if (state->instance == NULL &&
            slot == NULL)
            slot = state;

        if (oldest == NULL ||
            state->lastSeenSerial <
                oldest->lastSeenSerial)
            oldest = state;
    }

    if (slot == NULL)
        slot = oldest;

    if (slot == NULL)
        return NULL;

    if (slot->instance != NULL)
        NativeGltf_StateRelease(slot);

    slot->instance = inst;
    slot->asset = asset;
    slot->lastPresentSerial = ~(u64)0;
    slot->lastSeenSerial =
        Platform_GetHostPresentCount();
    slot->rng =
        (u32)(uintptr_t)inst ^
        ((u32)asset->characterID * 0x9e3779b9u) ^
        0xa341316cu;
    slot->currentClip = -1;

    if (!NativeGltf_StateAllocateArrays(slot))
    {
        NativeGltf_StateRelease(slot);
        return NULL;
    }

    NativeGltf_ResetIdleCountdown(slot);
    return slot;
}

static int NativeGltf_FindClip(
    const struct NativeGltfAsset *asset,
    const char *name)
{
    if (asset == NULL || name == NULL)
        return -1;

    for (int clip = 0;
         clip < asset->clipCount;
         ++clip)
    {
        if (!SDL_strcasecmp(
                asset->clips[clip].name,
                name))
            return clip;
    }

    return -1;
}

static int NativeGltf_FindClip2(
    const struct NativeGltfAsset *asset,
    const char *a,
    const char *b)
{
    int clip = NativeGltf_FindClip(asset, a);
    return clip >= 0 ?
        clip : NativeGltf_FindClip(asset, b);
}

static int NativeGltf_IsIdleExtra(
    const char *name)
{
    if (name == NULL)
        return 0;

    if (SDL_strcasecmp(name, "idle_base") == 0 ||
        SDL_strcasecmp(name, "idle") == 0)
        return 0;

    return SDL_strncasecmp(
        name, "idle_", 5) == 0;
}

static int NativeGltf_ChooseIdleExtra(
    struct NativeGltfState *state)
{
    int count = 0;
    int selected;

    for (int clip = 0;
         clip < state->asset->clipCount;
         ++clip)
    {
        if (NativeGltf_IsIdleExtra(
                state->asset->clips[clip].name))
            count++;
    }

    if (count <= 0)
        return -1;

    selected =
        (int)(NativeGltf_RandomNext(
            &state->rng) % (u32)count);

    for (int clip = 0;
         clip < state->asset->clipCount;
         ++clip)
    {
        if (!NativeGltf_IsIdleExtra(
                state->asset->clips[clip].name))
            continue;

        if (selected-- == 0)
            return clip;
    }

    return -1;
}

static void NativeGltf_StateSetClip(
    struct NativeGltfState *state,
    int clip,
    int oneShot)
{
    float crossfade;

    if (state->currentClip == clip &&
        state->oneShot == oneShot)
        return;

    if (state->asset->jointCount > 0)
    {
        size_t joints =
            (size_t)state->asset->jointCount;

        if (state->poseValid)
        {
            memcpy(
                state->fromTranslation,
                state->translation,
                joints * 3u * sizeof(float));
            memcpy(
                state->fromRotation,
                state->rotation,
                joints * 4u * sizeof(float));
            memcpy(
                state->fromScale,
                state->scale,
                joints * 3u * sizeof(float));
        }
        else
        {
            NativeGltf_SetRestPose(state);
            memcpy(
                state->fromTranslation,
                state->translation,
                joints * 3u * sizeof(float));
            memcpy(
                state->fromRotation,
                state->rotation,
                joints * 4u * sizeof(float));
            memcpy(
                state->fromScale,
                state->scale,
                joints * 3u * sizeof(float));
        }
    }

    state->currentClip = clip;
    state->oneShot = oneShot;
    state->clipTime = 0.0f;
    state->fadeElapsed = 0.0f;

    crossfade =
        CharacterRegistry_GetAnimationCrossfade(
            state->asset->characterID);
    state->fadeDuration =
        fmaxf(0.0f, crossfade);
}

static void NativeGltf_SampleChannel(
    const struct NativeGltfChannel *channel,
    float time,
    float *out)
{
    int left = 0;
    int right = 0;

    if (channel->keyCount <= 0)
        return;

    if (channel->keyCount == 1 ||
        time <= channel->times[0])
    {
        memcpy(
            out,
            &channel->values[0],
            (size_t)channel->valueSize *
                sizeof(float));
        return;
    }

    if (time >=
        channel->times[channel->keyCount - 1])
    {
        memcpy(
            out,
            &channel->values[
                (size_t)(channel->keyCount - 1) *
                (size_t)channel->valueSize],
            (size_t)channel->valueSize *
                sizeof(float));
        return;
    }

    left = 0;
    right = channel->keyCount - 1;

    while (right - left > 1)
    {
        int mid = (left + right) >> 1;
        if (channel->times[mid] <= time)
            left = mid;
        else
            right = mid;
    }

    if (channel->interpolation == 1)
    {
        memcpy(
            out,
            &channel->values[
                (size_t)left *
                (size_t)channel->valueSize],
            (size_t)channel->valueSize *
                sizeof(float));
        return;
    }

    {
        float start = channel->times[left];
        float end = channel->times[right];
        float alpha =
            (end > start) ?
            (time - start) / (end - start) :
            0.0f;
        const float *a =
            &channel->values[
                (size_t)left *
                (size_t)channel->valueSize];
        const float *b =
            &channel->values[
                (size_t)right *
                (size_t)channel->valueSize];

        alpha =
            fminf(1.0f, fmaxf(0.0f, alpha));

        if (channel->path ==
            NATIVE_GLTF_CHANNEL_ROTATION)
        {
            NativeGltf_QuatSlerp(
                out, a, b, alpha);
        }
        else
        {
            for (int component = 0;
                 component < channel->valueSize;
                 ++component)
            {
                out[component] =
                    a[component] +
                    (b[component] - a[component]) *
                    alpha;
            }
        }
    }
}

static void NativeGltf_SamplePose(
    struct NativeGltfState *state)
{
    struct NativeGltfAsset *asset = state->asset;
    float fadeAlpha = 1.0f;

    if (asset->jointCount <= 0)
        return;

    NativeGltf_SetRestPose(state);

    if (state->currentClip >= 0 &&
        state->currentClip < asset->clipCount)
    {
        const struct NativeGltfClip *clip =
            &asset->clips[state->currentClip];

        for (int channelIndex = 0;
             channelIndex < clip->channelCount;
             ++channelIndex)
        {
            const struct NativeGltfChannel *channel =
                &clip->channels[channelIndex];
            float value[4] = { 0, 0, 0, 1 };

            NativeGltf_SampleChannel(
                channel,
                state->clipTime,
                value);

            if (channel->path ==
                NATIVE_GLTF_CHANNEL_TRANSLATION)
            {
                memcpy(
                    &state->translation[
                        channel->joint * 3],
                    value,
                    sizeof(float) * 3);
            }
            else if (channel->path ==
                     NATIVE_GLTF_CHANNEL_ROTATION)
            {
                memcpy(
                    &state->rotation[
                        channel->joint * 4],
                    value,
                    sizeof(float) * 4);
                NativeGltf_QuatNormalize(
                    &state->rotation[
                        channel->joint * 4]);
            }
            else if (channel->path ==
                     NATIVE_GLTF_CHANNEL_SCALE)
            {
                memcpy(
                    &state->scale[
                        channel->joint * 3],
                    value,
                    sizeof(float) * 3);
            }
        }
    }

    if (state->fadeDuration > 0.0f &&
        state->fadeElapsed <
            state->fadeDuration)
    {
        fadeAlpha =
            state->fadeElapsed /
            state->fadeDuration;
        fadeAlpha =
            fminf(1.0f, fmaxf(0.0f, fadeAlpha));
    }

    if (fadeAlpha < 1.0f)
    {
        for (int joint = 0;
             joint < asset->jointCount;
             ++joint)
        {
            for (int component = 0;
                 component < 3;
                 ++component)
            {
                int index = joint * 3 + component;

                state->translation[index] =
                    state->fromTranslation[index] +
                    (state->translation[index] -
                     state->fromTranslation[index]) *
                    fadeAlpha;

                state->scale[index] =
                    state->fromScale[index] +
                    (state->scale[index] -
                     state->fromScale[index]) *
                    fadeAlpha;
            }

            NativeGltf_QuatSlerp(
                &state->rotation[joint * 4],
                &state->fromRotation[joint * 4],
                &state->rotation[joint * 4],
                fadeAlpha);
        }
    }

    state->poseValid = 1;
}

static int NativeGltf_BuildJointWorld(
    struct NativeGltfState *state,
    int jointIndex,
    int depth)
{
    struct NativeGltfAsset *asset = state->asset;
    const struct NativeGltfJoint *joint;
    float prefixedLocal[16];

    if (jointIndex < 0 ||
        jointIndex >= asset->jointCount)
        return 0;

    if (state->worldReady[jointIndex])
        return 1;

    if (depth > asset->jointCount)
        return 0;

    joint = &asset->joints[jointIndex];

    NativeGltf_MatFromTRS(
        &state->localMatrices[jointIndex * 16],
        &state->translation[jointIndex * 3],
        &state->rotation[jointIndex * 4],
        &state->scale[jointIndex * 3]);

    NativeGltf_MatMul(
        prefixedLocal,
        joint->prefix,
        &state->localMatrices[jointIndex * 16]);

    if (joint->parent >= 0)
    {
        if (!NativeGltf_BuildJointWorld(
                state,
                joint->parent,
                depth + 1))
            return 0;

        NativeGltf_MatMul(
            &state->worldMatrices[jointIndex * 16],
            &state->worldMatrices[
                joint->parent * 16],
            prefixedLocal);
    }
    else
    {
        NativeGltf_MatCopy(
            &state->worldMatrices[jointIndex * 16],
            prefixedLocal);
    }

    {
        float jointInMesh[16];

        NativeGltf_MatMul(
            jointInMesh,
            asset->inverseMeshWorld,
            &state->worldMatrices[
                jointIndex * 16]);

        NativeGltf_MatMul(
            &state->skinMatrices[
                jointIndex * 16],
            jointInMesh,
            joint->inverseBind);
    }

    state->worldReady[jointIndex] = 1;
    return 1;
}

static int NativeGltf_BuildSkinMatrices(
    struct NativeGltfState *state)
{
    if (state->asset->jointCount <= 0)
        return 1;

    memset(
        state->worldReady,
        0,
        (size_t)state->asset->jointCount *
            sizeof(*state->worldReady));

    for (int joint = 0;
         joint < state->asset->jointCount;
         ++joint)
    {
        if (!NativeGltf_BuildJointWorld(
                state, joint, 0))
            return 0;
    }

    return 1;
}

static void NativeGltf_SkinSourceVertex(
    const struct NativeGltfState *state,
    const struct NativeGltfSourceVertex *source,
    float out[3])
{
    const struct NativeGltfAsset *asset =
        state->asset;
    float totalWeight = 0.0f;

    out[0] = out[1] = out[2] = 0.0f;

    if (asset->jointCount > 0)
    {
        for (int influence = 0;
             influence < 4;
             ++influence)
        {
            int joint =
                (int)source->joints[influence];
            float weight =
                source->weights[influence];
            float transformed[3];

            if (weight <= 0.0f ||
                joint < 0 ||
                joint >= asset->jointCount)
                continue;

            NativeGltf_TransformPoint(
                &state->skinMatrices[joint * 16],
                source->position,
                transformed);

            out[0] += transformed[0] * weight;
            out[1] += transformed[1] * weight;
            out[2] += transformed[2] * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight <= NATIVE_GLTF_EPSILON)
    {
        out[0] = source->position[0];
        out[1] = source->position[1];
        out[2] = source->position[2];
    }
    else if (fabsf(totalWeight - 1.0f) >
             0.0001f)
    {
        float inv = 1.0f / totalWeight;
        out[0] *= inv;
        out[1] *= inv;
        out[2] *= inv;
    }
}

static int NativeGltf_UpdateSkinnedMesh(
    struct NativeGltfState *state)
{
    struct NativeGltfAsset *asset = state->asset;
    float seatOffset[3] = { 0, 0, 0 };
    b32 hasSeatOffset =
        NativeObj_GetSeatOffsetForInstance(
            state->instance, seatOffset);

    if (!NativeGltf_BuildSkinMatrices(state))
        return 0;

    for (int axis = 0; axis < 3; ++axis)
    {
        state->renderMesh.renderOffsetScaled[axis] =
            asset->userOffset[axis] *
            asset->normPerAuth[axis] *
            asset->userScale;

        if (hasSeatOffset)
            state->renderMesh.renderOffsetScaled[axis] +=
                seatOffset[axis];
    }

    for (int triangleIndex = 0;
         triangleIndex < asset->triangleCount;
         ++triangleIndex)
    {
        const struct NativeGltfSourceTriangle *source =
            &asset->triangles[triangleIndex];
        struct NativeObjTriangle *triangle =
            &state->renderMesh.triangles[
                triangleIndex];

        for (int corner = 0;
             corner < 3;
             ++corner)
        {
            float skinned[3];

            NativeGltf_SkinSourceVertex(
                state,
                &source->v[corner],
                skinned);

            for (int axis = 0;
                 axis < 3;
                 ++axis)
            {
                float normalized =
                    skinned[axis] *
                    asset->normPerAuth[axis];

                triangle->v[corner].p[axis] =
                    normalized;
                triangle->v[corner].renderP[axis] =
                    normalized *
                    state->renderMesh.userScale;
            }
        }
    }

    return 1;
}

static const struct Driver *NativeGltf_GetDriver(
    const struct Instance *inst)
{
    struct GameTracker *gGT = sdata->gGT;

    if (inst == NULL || gGT == NULL)
        return NULL;

    for (int driverIndex = 0;
         driverIndex < 8;
         ++driverIndex)
    {
        const struct Driver *driver =
            gGT->drivers[driverIndex];

        if (driver != NULL &&
            driver->instSelf == inst)
            return driver;
    }

    /*
     * Time Trial ghosts are Driver objects too, but they live in the GHOST
     * thread bucket instead of gGT->drivers[]. Validate through instSelf before
     * treating the thread object as a Driver so skeletal ghost clips can use
     * the same semantic animation bridge as normal racers.
     */
    if (inst->thread != NULL &&
        inst->thread->object != NULL)
    {
        const struct Driver *threadDriver =
            (const struct Driver *)inst->thread->object;

        if (threadDriver->instSelf == inst)
            return threadDriver;
    }

    return NULL;
}

static int NativeGltf_GetIdleBaseClip(
    const struct NativeGltfAsset *asset)
{
    return NativeGltf_FindClip2(
        asset, "idle_base", "idle");
}

static int NativeGltf_GetMovingClip(
    const struct NativeGltfAsset *asset,
    const struct Driver *driver)
{
    int drive =
        NativeGltf_FindClip2(
            asset, "drive", "driving");

    if (driver != NULL)
    {
        int turnState =
            (driver->actionsFlagSet &
             ACTION_ACCEL_PREVENTION) != 0 ?
            (int)driver->ampTurnState :
            (int)driver->simpTurnState;

        if (turnState > 8)
        {
            int right =
                NativeGltf_FindClip(
                    asset, "turn_right");
            if (right >= 0)
                return right;
        }
        else if (turnState < -8)
        {
            int left =
                NativeGltf_FindClip(
                    asset, "turn_left");
            if (left >= 0)
                return left;
        }
    }

    return drive;
}

static int NativeGltf_GetAirborneClip(
    const struct NativeGltfAsset *asset)
{
    return NativeGltf_FindClip2(
        asset, "airborne", "jump");
}

static int NativeGltf_GetImpactClip(
    const struct NativeGltfAsset *asset,
    const struct Driver *driver)
{
    int clip;

    if (driver == NULL)
        return -1;

    if (driver->kartState == KS_SPINNING)
    {
        clip = NativeGltf_FindClip(asset, "spin");
        if (clip >= 0)
            return clip;
    }

    if (driver->kartState == KS_CRASHING ||
        driver->kartState == KS_BLASTED)
    {
        clip = NativeGltf_FindClip2(
            asset, "bump", "crash");
        if (clip >= 0)
            return clip;
    }

    return -1;
}

static int NativeGltf_GetGroundDrivingClip(
    const struct NativeGltfAsset *asset,
    const struct Driver *driver)
{
    int clip;

    if (driver == NULL)
        return -1;

    if (driver->speedApprox < -1 ||
        (driver->actionsFlagSet &
         ACTION_REVERSING_ENGINE) != 0)
    {
        clip = NativeGltf_FindClip(
            asset, "reverse");
        if (clip >= 0)
            return clip;
    }

    if ((driver->actionsFlagSet &
         ACTION_BRAKE_WITH_ACCEL) != 0)
    {
        clip = NativeGltf_FindClip(
            asset, "brake");
        if (clip >= 0)
            return clip;
    }

    return -1;
}

static void NativeGltf_UpdateClipSelection(
    struct NativeGltfState *state,
    const struct Driver *driver,
    float dt)
{
    const struct Instance *inst =
        state->instance;
    int idleBase =
        NativeGltf_GetIdleBaseClip(
            state->asset);
    int airborne = 0;
    int stationary =
        driver == NULL ||
        abs((int)driver->speedApprox) < 1;

    if (driver != NULL)
    {
        airborne =
            inst->animIndex == 3 ||
            driver->jumpHeightCurr > 0x600;
    }

    if (driver != NULL)
    {
        int impact =
            NativeGltf_GetImpactClip(
                state->asset, driver);

        if (impact >= 0)
        {
            state->oneShot = 0;
            NativeGltf_StateSetClip(
                state, impact, 0);
            return;
        }
    }

    if (airborne)
    {
        int clip =
            NativeGltf_GetAirborneClip(
                state->asset);

        state->oneShot = 0;
        if (clip < 0)
            clip = NativeGltf_GetMovingClip(
                state->asset, driver);

        NativeGltf_StateSetClip(
            state, clip, 0);
        return;
    }

    if (driver != NULL)
    {
        int groundSpecial =
            NativeGltf_GetGroundDrivingClip(
                state->asset, driver);

        if (groundSpecial >= 0)
        {
            state->oneShot = 0;
            NativeGltf_StateSetClip(
                state, groundSpecial, 0);
            return;
        }
    }

    if (!stationary)
    {
        int clip =
            NativeGltf_GetMovingClip(
                state->asset, driver);

        if (state->oneShot ||
            state->currentClip == idleBase ||
            state->currentClip < 0)
        {
            NativeGltf_ResetIdleCountdown(state);
        }

        state->oneShot = 0;
        NativeGltf_StateSetClip(
            state, clip, 0);
        return;
    }

    if (state->oneShot)
    {
        if (state->currentClip < 0 ||
            state->currentClip >=
                state->asset->clipCount ||
            state->asset->clips[
                state->currentClip].duration <= 0.0f ||
            state->clipTime >=
                state->asset->clips[
                    state->currentClip].duration)
        {
            state->oneShot = 0;
            NativeGltf_StateSetClip(
                state, idleBase, 0);
            NativeGltf_ResetIdleCountdown(state);
        }

        return;
    }

    NativeGltf_StateSetClip(
        state, idleBase, 0);

    state->idleCountdown -= dt;
    if (state->idleCountdown <= 0.0f)
    {
        int extra =
            NativeGltf_ChooseIdleExtra(state);

        if (extra >= 0)
        {
            NativeGltf_StateSetClip(
                state, extra, 1);
        }
        else
        {
            NativeGltf_ResetIdleCountdown(state);
        }
    }
}

static int NativeGltf_UpdateState(
    struct NativeGltfState *state)
{
    u64 serial =
        Platform_GetHostPresentCount();
    u64 now;
    float dt = 0.0f;

    if (state->lastPresentSerial == serial)
        return 1;

    state->lastPresentSerial = serial;
    state->lastSeenSerial = serial;

    now = SDL_GetTicksNS();

    if (state->lastTimeNS != 0 &&
        now > state->lastTimeNS)
    {
        dt = (float)(now - state->lastTimeNS) /
             1000000000.0f;

        if (dt > 0.10f)
            dt = 0.10f;
    }

    state->lastTimeNS = now;

    if (state->currentClip >= 0 &&
        state->currentClip <
            state->asset->clipCount)
    {
        state->clipTime += dt;
    }

    NativeGltf_UpdateClipSelection(
        state,
        NativeGltf_GetDriver(
            state->instance),
        dt);

    if (!state->oneShot &&
        state->currentClip >= 0 &&
        state->currentClip <
            state->asset->clipCount)
    {
        float duration =
            state->asset->clips[
                state->currentClip].duration;

        if (duration > 0.0f &&
            state->clipTime >= duration)
        {
            state->clipTime =
                fmodf(
                    state->clipTime,
                    duration);
        }
    }

    if (state->fadeDuration > 0.0f &&
        state->fadeElapsed <
            state->fadeDuration)
    {
        state->fadeElapsed += dt;
        if (state->fadeElapsed >
            state->fadeDuration)
        {
            state->fadeElapsed =
                state->fadeDuration;
        }
    }

    NativeGltf_SamplePose(state);
    return NativeGltf_UpdateSkinnedMesh(state);
}

struct Model *NativeGltf_LoadRacer(
    int characterID)
{
    struct NativeGltfAsset *asset =
        NativeGltf_LoadAsset(characterID);

    if (asset == NULL ||
        asset->descriptor == NULL)
        return NULL;

    return &asset->descriptor->model;
}

b32 NativeGltf_IsModel(
    const struct Model *model)
{
    return model != NULL &&
           model->headers != NULL &&
           model->numHeaders > 0 &&
           model->headers[0].unk1 ==
               NATIVE_GLTF_MODEL_MAGIC;
}

const struct NativeObjMesh *NativeGltf_GetRenderMesh(
    const struct Instance *inst)
{
    struct NativeGltfAsset *asset;
    struct NativeGltfState *state;

    if (inst == NULL ||
        !NativeGltf_IsModel(inst->model))
        return NULL;

    asset =
        NativeGltf_GetAssetFromModel(
            inst->model);
    if (asset == NULL || asset->failed)
        return NULL;

    state = NativeGltf_GetState(
        inst, asset);
    if (state == NULL)
        return NULL;

    if (!NativeGltf_UpdateState(state))
        return NULL;

    return &state->renderMesh;
}

int NativeGltf_GetCompatFrameCount(
    const struct Instance *inst,
    int animIndex)
{
    if (inst == NULL ||
        !NativeGltf_IsModel(inst->model))
        return 0;

    switch (animIndex)
    {
    case 0:
        return 21;
    case 1:
        return 1;
    case 2:
    case 3:
    case 4:
        return 12;
    default:
        return 12;
    }
}

void NativeGltf_Shutdown(void)
{
    struct NativeGltfAsset *asset;

    for (int i = 0;
         i < NATIVE_GLTF_MAX_STATES;
         ++i)
    {
        NativeGltf_StateRelease(
            &s_gltfStates[i]);
    }

    asset = s_gltfAssets;
    while (asset != NULL)
    {
        struct NativeGltfAsset *next =
            asset->next;
        NativeGltf_FreeAsset(asset);
        asset = next;
    }

    s_gltfAssets = NULL;
}

void NativeGltf_PrepareRegistryRefresh(void)
{
    struct GameTracker *gGT = sdata->gGT;
    int allDetached = 1;

    /*
     * The Mods menu can rebuild CharacterRegistry while main-menu Driver
     * instances still exist. Detach any live GLB model back to its retail
     * fallback before freeing GLB descriptors/textures, otherwise those hidden
     * instances would retain dangling model pointers until Character Select
     * assigns them again.
     *
     * If even one live instance cannot resolve a retail fallback, keep the GLB
     * cache alive for this refresh instead of freeing memory still referenced
     * by that instance. The registry can still reload safely; the old visual
     * asset will be released on a later successful refresh or platform shutdown.
     */
    if (gGT != NULL)
    {
        for (int driverIndex = 0; driverIndex < 8; ++driverIndex)
        {
            struct Driver *driver = gGT->drivers[driverIndex];
            struct Instance *inst;
            struct NativeGltfAsset *asset;
            struct Model *fallbackModel;
            int fallbackID;

            if (driver == NULL ||
                driver->instSelf == NULL)
                continue;

            inst = driver->instSelf;
            if (!NativeGltf_IsModel(inst->model))
                continue;

            asset = NativeGltf_GetAssetFromModel(inst->model);
            fallbackID = CharacterRegistry_GetDriverPackID(
                asset ? asset->characterID :
                data.characterIDs[driverIndex]);

            fallbackModel = VehBirth_GetModelByName(
                data.MetaDataCharacters[fallbackID].name_Debug);

            if (fallbackModel != NULL)
            {
                inst->model = fallbackModel;
            }
            else
            {
                allDetached = 0;
                SDL_Log(
                    "GLB refresh: keeping cache because driver %d could not detach to retail fallback %d",
                    driverIndex,
                    fallbackID);
            }
        }
    }

    if (allDetached)
        NativeGltf_Shutdown();
}
