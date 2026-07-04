#include "pch.h"
#include "SerializeScene.h"

#include "CanvasMath.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace Canvas::Cpkg
{

namespace
{
    PackageFloat4 ToFloat4(const Math::FloatVector4& v)
    {
        return { { v.V[0], v.V[1], v.V[2], v.V[3] } };
    }

    PackageFloat2 ToFloat2(const Math::FloatVector2& v)
    {
        return { { v.V[0], v.V[1] } };
    }

    PackageQuat ToQuat(const Math::FloatQuaternion& q)
    {
        PackageQuat out;
        out.V[0] = q.V[0];
        out.V[1] = q.V[1];
        out.V[2] = q.V[2];
        out.V[3] = q.V[3];
        return out;
    }

    PackageAABB ToAABB(const Math::AABB& box)
    {
        PackageAABB out;
        for (int i = 0; i < 3; ++i)
        {
            out.Min.V[i] = box.Min.V[i];
            out.Max.V[i] = box.Max.V[i];
        }
        return out;
    }

    PackageMatrix4x4 ToMatrix(const Math::FloatMatrix4x4& m)
    {
        PackageMatrix4x4 out; // row-major, matching Math::FloatMatrix4x4's row storage
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                out.M[row * 4 + col] = m[row][col];
        return out;
    }

    // Remap an ImportedMaterial texture index (into scene.Textures) through the resolved TXTR
    // indices. Out-of-range or unresolved references become -1 (unbound).
    int32_t Remap(int32_t sceneIndex, const std::vector<int32_t>& remap)
    {
        if (sceneIndex < 0 || sceneIndex >= static_cast<int32_t>(remap.size()))
            return -1;
        return remap[sceneIndex];
    }
}

void SerializeScene(const Canvas::Fbx::ImportedScene& scene, const TextureTable& table,
                    PackageData* out)
{
    if (!out)
        return;

    *out = PackageData{};

    // ImportedScene texture index -> resolved TXTR index, keyed by each texture's absolute path.
    std::vector<int32_t> textureRemap;
    textureRemap.reserve(scene.Textures.size());
    for (const Canvas::Fbx::ImportedTextureRef& ref : scene.Textures)
        textureRemap.push_back(table.FindByAbsPath(ref.AbsoluteFilePath));

    // Meshes
    out->Meshes.reserve(scene.Meshes.size());
    for (const Canvas::Fbx::ImportedMesh& srcMesh : scene.Meshes)
    {
        PackageMesh mesh;
        mesh.Name   = srcMesh.Name;
        mesh.Bounds = ToAABB(srcMesh.Bounds);

        mesh.Skin.HasSkin         = srcMesh.Skin.HasSkin;
        mesh.Skin.BoneNodeIndices = srcMesh.Skin.BoneNodeIndices;
        mesh.Skin.InvBindPoses.reserve(srcMesh.Skin.InvBindPoses.size());
        for (const Math::FloatMatrix4x4& pose : srcMesh.Skin.InvBindPoses)
            mesh.Skin.InvBindPoses.push_back(ToMatrix(pose));

        mesh.Parts.reserve(srcMesh.Parts.size());
        for (const Canvas::Fbx::ImportedMeshPart& srcPart : srcMesh.Parts)
        {
            PackageMeshPart part;
            part.MaterialIndex = srcPart.MaterialIndex;

            part.Positions.reserve(srcPart.Positions.size());
            for (const Math::FloatVector4& p : srcPart.Positions)
                part.Positions.push_back(ToFloat4(p));

            part.Normals.reserve(srcPart.Normals.size());
            for (const Math::FloatVector4& n : srcPart.Normals)
                part.Normals.push_back(ToFloat4(n));

            part.UV0.reserve(srcPart.UV0.size());
            for (const Math::FloatVector2& uv : srcPart.UV0)
                part.UV0.push_back(ToFloat2(uv));

            part.Tangents.reserve(srcPart.Tangents.size());
            for (const Math::FloatVector4& t : srcPart.Tangents)
                part.Tangents.push_back(ToFloat4(t));

            part.SkinVertices.reserve(srcPart.SkinVertices.size());
            for (const Canvas::Fbx::ImportedSkinVertex& sv : srcPart.SkinVertices)
            {
                PackageSkinVertex dst;
                for (int b = 0; b < 4; ++b)
                {
                    dst.BoneIndices[b] = sv.BoneIndices[b];
                    dst.BoneWeights[b] = sv.Weights[b];
                }
                part.SkinVertices.push_back(dst);
            }

            mesh.Parts.push_back(std::move(part));
        }

        out->Meshes.push_back(std::move(mesh));
    }

    // Lights
    out->Lights.reserve(scene.Lights.size());
    for (const Canvas::Fbx::ImportedLight& srcLight : scene.Lights)
    {
        PackageLight light;
        light.Name              = srcLight.Name;
        light.Type              = srcLight.Type;
        light.Color             = ToFloat4(srcLight.Color);
        light.Intensity         = srcLight.Intensity;
        light.Range             = srcLight.Range;
        light.AttenuationConst  = srcLight.AttenuationConst;
        light.AttenuationLinear = srcLight.AttenuationLinear;
        light.AttenuationQuad   = srcLight.AttenuationQuad;
        light.SpotInnerAngle    = srcLight.SpotInnerAngle;
        light.SpotOuterAngle    = srcLight.SpotOuterAngle;
        out->Lights.push_back(std::move(light));
    }

    // Cameras
    out->Cameras.reserve(scene.Cameras.size());
    for (const Canvas::Fbx::ImportedCamera& srcCamera : scene.Cameras)
    {
        PackageCamera camera;
        camera.Name        = srcCamera.Name;
        camera.NearZ       = srcCamera.NearClip;
        camera.FarZ        = srcCamera.FarClip;
        camera.FovY        = srcCamera.FovAngle;
        camera.AspectRatio = srcCamera.AspectRatio;
        out->Cameras.push_back(std::move(camera));
    }

    // Materials (texture indices remapped through the TextureTable)
    out->Materials.reserve(scene.Materials.size());
    for (const Canvas::Fbx::ImportedMaterial& srcMat : scene.Materials)
    {
        PackageMaterial mat;
        mat.Name               = srcMat.Name;
        mat.BaseColorFactor    = ToFloat4(srcMat.BaseColorFactor);
        mat.EmissiveFactor     = ToFloat4(srcMat.EmissiveFactor);
        mat.RoughMetalAOFactor = ToFloat4(srcMat.RoughMetalAOFactor);

        mat.AlbedoTextureIndex           = Remap(srcMat.AlbedoTextureIndex, textureRemap);
        mat.NormalTextureIndex           = Remap(srcMat.NormalTextureIndex, textureRemap);
        mat.EmissiveTextureIndex         = Remap(srcMat.EmissiveTextureIndex, textureRemap);
        mat.RoughnessTextureIndex        = Remap(srcMat.RoughnessTextureIndex, textureRemap);
        mat.MetallicTextureIndex         = Remap(srcMat.MetallicTextureIndex, textureRemap);
        mat.AmbientOcclusionTextureIndex = Remap(srcMat.AmbientOcclusionTextureIndex, textureRemap);
        out->Materials.push_back(std::move(mat));
    }

    // Textures come from the deduplicated table, not the raw ImportedScene list.
    out->Textures = table.Finalize();

    // Nodes
    out->Nodes.reserve(scene.Nodes.size());
    for (const Canvas::Fbx::ImportedNode& srcNode : scene.Nodes)
    {
        PackageNode node;
        node.Name        = srcNode.Name;
        node.ParentIndex = srcNode.ParentIndex;
        node.Translation = ToFloat4(srcNode.Translation);
        node.Scale       = ToFloat4(srcNode.Scale);
        node.Rotation    = ToQuat(srcNode.Rotation);
        node.MeshIndex   = srcNode.MeshIndex;
        node.LightIndex  = srcNode.LightIndex;
        node.CameraIndex = srcNode.CameraIndex;
        out->Nodes.push_back(std::move(node));
    }

    // Animation clips
    out->AnimClips.reserve(scene.AnimationClips.size());
    for (const Canvas::Fbx::ImportedAnimationClip& srcClip : scene.AnimationClips)
    {
        PackageAnimClip clip;
        clip.Name            = srcClip.Name;
        clip.DurationSeconds = srcClip.DurationSeconds;
        clip.Tracks.reserve(srcClip.Tracks.size());
        for (const Canvas::Fbx::ImportedAnimationTrack& srcTrack : srcClip.Tracks)
        {
            PackageAnimTrack track;
            track.NodeIndex = srcTrack.NodeIndex;
            track.Keyframes.reserve(srcTrack.Keyframes.size());
            for (const Canvas::Fbx::ImportedAnimationKeyframe& key : srcTrack.Keyframes)
            {
                PackageAnimKeyframe frame;
                frame.Time        = key.Time;
                frame.Translation = ToFloat4(key.Translation);
                frame.Rotation    = ToQuat(key.Rotation);
                frame.Scale       = ToFloat4(key.Scale);
                track.Keyframes.push_back(frame);
            }
            clip.Tracks.push_back(std::move(track));
        }
        out->AnimClips.push_back(std::move(clip));
    }

    out->Bounds                = ToAABB(scene.SceneBounds);
    out->ActiveCameraNodeIndex = scene.ActiveCameraNodeIndex;
}

} // namespace Canvas::Cpkg
