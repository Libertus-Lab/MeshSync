#include "pch.h"
#include "Test.h"
#include "Common.h"
#include "Utility/TestUtility.h"
#include "Utility/MeshGenerator.h"

#include "MeshSync/SceneGraph/msAnimation.h"
#include "MeshSync/SceneGraph/msMaterial.h"
#include "MeshSync/SceneGraph/msMesh.h"
#include "MeshSync/SceneGraph/msCurve.h"
#include "MeshSync/SceneGraph/msPoints.h"
#include "MeshSync/SceneGraph/msScene.h"

#include "MeshSync/SceneCache/msSceneCacheInputSettings.h"
#include "MeshSync/SceneCache/msSceneCacheOutputSettings.h"
#include "MeshSync/SceneCache/msSceneCacheEncoding.h"
#include "MeshSync/SceneCache/msSceneCacheWriter.h" //SceneCacheWriter

#include "MeshSync/Utility/msMaterialExt.h"     //standardMaterial
#include "MeshSync/SceneCache/msSceneCacheInputFile.h"

#include "MeshSync/AsyncSceneSender.h" //ms::AsyncSceneSender
#include "MeshSync/msServer.h"

using namespace mu;

//#define SKIP_CLIENT_TEST

#ifndef SKIP_CLIENT_TEST

// We don't have tests for clients yet, using this to test some code:

class ID
{
public:
    std::string name;
    unsigned int session_uuid;
};

class Object
{
public:
    ID* data;
};

std::map<std::string, unsigned int> mappedNames;

std::string msblenContextIntermediatePathProvider_append_id(std::string path, const Object* obj) {
    auto data = (ID*)obj->data;

    path += "_" + std::string(data->name);

    // If we already have an object with this name but a different session_uuid, append the session_uuid as well
    auto it = mappedNames.find(data->name);

    if (it == mappedNames.end()) {
        mappedNames.insert(std::make_pair(data->name, data->session_uuid));
    }
    else if (it->second != data->session_uuid)
    {
        path += "_" + std::to_string(data->session_uuid);
    }

    return path;
}

TestCase(Test_IntermediatePathProvider) {
    Object cube1;
    cube1.data = new ID();
    cube1.data->name = "id";
    cube1.data->session_uuid = 1;

    Object cube2;
    cube2.data = new ID();
    cube2.data->name = "id";
    cube2.data->session_uuid = 2;

    // Ensure the returned path is consistent and the first object with the name has no uuid suffix:
    for (int i = 0; i < 10; i++) {
        assert(msblenContextIntermediatePathProvider_append_id("/cube", &cube1) == "/cube_id");
        assert(msblenContextIntermediatePathProvider_append_id("/cube", &cube2) == "/cube_id_2");
    }
}

TestCase(Test_SendProperties) {
    std::shared_ptr<ms::Scene> scene = ms::Scene::create();
    {
        std::shared_ptr<ms::Mesh> node = ms::Mesh::create();
        scene->entities.push_back(node);

        node->path = "/Test/PropertiesHolder";
        node->position = { 0.0f, 0.0f, 0.0f };
        node->rotation = quatf::identity();
        node->scale = { 1.0f, 1.0f, 1.0f };
        node->visibility = { false, true, true };
        MeshGenerator::GenerateIcoSphereMesh(node->counts, node->indices, node->points, node->m_uv[0], 0.1f, 1);
        node->refine_settings.flags.Set(ms::MESH_REFINE_FLAG_GEN_NORMALS, true);
        node->refine_settings.flags.Set(ms::MESH_REFINE_FLAG_GEN_TANGENTS, true);

        scene->propertyInfos.clear();
        {
            auto prop = ms::PropertyInfo::create();
            prop->path = node->path;
            prop->name = "Test int";
            prop->set(10, 0, 100);
            assert(prop->type == ms::PropertyInfo::Type::Int && "type is not correct");
            scene->propertyInfos.push_back(prop);
        }
        {
            auto prop = ms::PropertyInfo::create();
            prop->path = node->path;
            prop->name = "Test float";
            prop->set(10.0f, 0, 100);
            assert(prop->type == ms::PropertyInfo::Type::Float && "type is not correct");
            scene->propertyInfos.push_back(prop);
        }
        {
            auto prop = ms::PropertyInfo::create();
            prop->path = node->path;
            prop->name = "Test string";
            const char* testString = "Test123";
            prop->set(testString, strlen(testString));
            assert(prop->type == ms::PropertyInfo::Type::String && "type is not correct");
            scene->propertyInfos.push_back(prop);
        }
        {
            auto prop = ms::PropertyInfo::create();
            prop->path = node->path;
            prop->name = "Test int array";
            int testArray[] = {1, 2, 3};
            prop->set(testArray, -100, 200, 3);
            assert(prop->type == ms::PropertyInfo::Type::IntArray && "type is not correct");
            assert(prop->min == -100 && "min is not correct");
            assert(prop->max == 200 && "max is not correct");
            scene->propertyInfos.push_back(prop);
        }
        {
            auto prop = ms::PropertyInfo::create();
            prop->name = "Test float array";
            float testArray[] = { 1, 2, 3 };
            prop->set(testArray, -100, 200, 3);
            assert(prop->type == ms::PropertyInfo::Type::FloatArray && "type is not correct");
            assert(prop->min == -100 && "min is not correct");
            assert(prop->max == 200 && "max is not correct");
            scene->propertyInfos.push_back(prop);
        }
    }

    TestUtility::Send(scene);
}

TestCase(Test_ServerInitiatedMessage) {
    ms::AsyncSceneSender sender;
    sender.requestLiveEditMessage();

    // AsyncSceneSender should be blocking now until the client responds. Otherwise the destructor of AsyncSceneSender aborts the call.
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
}

TestCase(Test_ServerPropertyHandling) {
    auto server = ms::Server(ms::ServerSettings());

    auto prop1 = ms::PropertyInfo::create();
    prop1->path = "TestPath";
    prop1->name = "Test string";
    const char* testString1 = "Test123";
    prop1->set(testString1, strlen(testString1));
    server.receivedProperty(prop1);

    auto prop2 = ms::PropertyInfo::create();
    prop2->path = "TestPath";
    prop2->name = "Test string";
    const char* testString2 = "Test456";
    prop2->set(testString2, strlen(testString2));
    server.receivedProperty(prop2);

    // 2 properties with the same path should be merged into 1:
    assert(server.m_pending_properties.size() == 1);
    assert(server.m_pending_properties[prop2->hash()] == prop2);
}

TestCase(Test_ServerEntityHandling) {
    auto server = ms::Server(ms::ServerSettings());

    auto testMesh = server.getOrCreatePendingEntity<ms::Mesh>("TestMesh");
    assert(testMesh != nullptr);
    assert(testMesh->path == "TestMesh" && "mesh was not created.");

    auto testCurve = server.getOrCreatePendingEntity<ms::Curve>("TestCurve");
    assert(testCurve != nullptr);
    assert(testCurve->path == "TestCurve" && "curve was not created.");

    assert(server.m_pending_entities.size() == 2);
}

TestCase(Test_SendMesh) {

    const float FRAME_RATE = 2.0f;
    const float TIME_PER_FRAME = 1.0f / FRAME_RATE;

    ms::SceneCacheOutputSettings c0;
    c0.exportSettings.stripUnchanged = 0;
    c0.exportSettings.flattenHierarchy = 0;
    c0.exportSettings.encoding = ms::SceneCacheEncoding::Plain;
    c0.exportSettings.sampleRate = FRAME_RATE;

    ms::SceneCacheOutputSettings c1;
    c1.exportSettings.flattenHierarchy = 0;
    c1.exportSettings.sampleRate = FRAME_RATE;

    ms::SceneCacheOutputSettings c2;
    c2.exportSettings.flattenHierarchy = 0;
    c2.exportSettings.encoderSettings.zstd.compressionLevel = 100;
    c2.exportSettings.sampleRate = FRAME_RATE;

    ms::SceneCacheWriter writer0, writer1, writer2;
    writer0.Open("wave_c0.sc", c0);
    writer1.Open("wave_c1.sc", c1);
    writer2.Open("wave_c2.sc", c2);

    for (int i = 0; i < 8; ++i) {
        std::shared_ptr<ms::Scene> scene = ms::Scene::create();

        std::shared_ptr<ms::Mesh> mesh = ms::Mesh::create();
        scene->entities.push_back(mesh);

        mesh->path = "/Test/Wave";
        mesh->refine_settings.flags.Set(ms::MESH_REFINE_FLAG_GEN_NORMALS, true);
        mesh->refine_settings.flags.Set(ms::MESH_REFINE_FLAG_GEN_TANGENTS, true);


        SharedVector<float3>& points = mesh->points;
        SharedVector<tvec2<float>>* uv = mesh->m_uv;
        SharedVector<int>& counts = mesh->counts;
        SharedVector<int>& indices = mesh->indices;
        SharedVector<int>& materialIDs = mesh->material_ids;

        MeshGenerator::GenerateWaveMesh(counts, indices, points, uv, 2.0f, 1.0f, 32, 30.0f * mu::DegToRad * i);
        materialIDs.resize(counts.size(), 0);
        mesh->setupDataFlags();


        const float writerTime = TIME_PER_FRAME * i;
        writer0.SetTime(writerTime);
        writer1.SetTime(writerTime);
        writer2.SetTime(writerTime);

        writer0.geometries.emplace_back(std::static_pointer_cast<ms::Transform>(mesh->clone(true)));
        writer0.kick();

        writer1.geometries.emplace_back(std::static_pointer_cast<ms::Transform>(mesh->clone(true)));
        writer1.kick();

        writer2.geometries.emplace_back(std::static_pointer_cast<ms::Transform>(mesh->clone(true)));
        writer2.kick();

        TestUtility::Send(scene);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

TestCase(Test_SceneCacheRead)
{
    ms::SceneCacheInputSettings iscs;
    iscs.enableDiff = false;
    ms::SceneCacheInputFilePtr isc = ms::SceneCacheInputFile::Open("wave_c2.sc", iscs);
    Expect(isc);
    if (!isc)
        return;

    const ms::TimeRange range = isc->GetTimeRangeV();
    const float step = 0.1f;
    for (float t = range.start; t < range.end; t += step) {
        const ms::ScenePtr scene = isc->LoadByTimeV(t, true);
        if (!scene)
            break;
        TestUtility::Send(scene);

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

TestCase(Test_Animation)
{
    std::shared_ptr<ms::Scene> scene = ms::Scene::create();
    {
        std::shared_ptr<ms::Mesh> node = ms::Mesh::create();
        scene->entities.push_back(node);

        node->path = "/Test/Animation";
        node->position = { 0.0f, 0.0f, 0.0f };
        node->rotation = quatf::identity();
        node->scale = { 1.0f, 1.0f, 1.0f };
        MeshGenerator::GenerateIcoSphereMesh(node->counts, node->indices, node->points, node->m_uv[0], 0.5f, 1);

        node->refine_settings.flags.Set(ms::MESH_REFINE_FLAG_GEN_NORMALS, true);
        node->refine_settings.flags.Set(ms::MESH_REFINE_FLAG_GEN_TANGENTS, true);
    }
    {
        std::shared_ptr<ms::AnimationClip> clip = ms::AnimationClip::create();
        scene->assets.push_back(clip);

        std::shared_ptr<ms::TransformAnimation> anim = ms::TransformAnimation::create();
        clip->addAnimation(anim);

        anim->path = "/Test/Animation";
        anim->translation.push_back({ 0.0f, {0.0f, 0.0f, 0.0f} });
        anim->translation.push_back({ 1.0f, {1.0f, 0.0f, 0.0f} });
        anim->translation.push_back({ 2.0f, {1.0f, 1.0f, 0.0f} });
        anim->translation.push_back({ 3.0f, {1.0f, 1.0f, 1.0f} });

        anim->rotation.push_back({ 0.0f, mu::rotate_x(0.0f * mu::DegToRad) });
        anim->rotation.push_back({ 1.0f, mu::rotate_x(90.0f * mu::DegToRad) });
        anim->rotation.push_back({ 2.0f, mu::rotate_x(180.0f * mu::DegToRad) });
        anim->rotation.push_back({ 3.0f, mu::rotate_x(270.0f * mu::DegToRad) });

        anim->scale.push_back({ 0.0f, {1.0f, 1.0f, 1.0f} });
        anim->scale.push_back({ 1.0f, {2.0f, 2.0f, 2.0f} });
        anim->scale.push_back({ 2.0f, {1.0f, 1.0f, 1.0f} });
        anim->scale.push_back({ 3.0f, {2.0f, 2.0f, 2.0f} });
    }
    TestUtility::Send(scene);
}


TestCase(Test_Points)
{
    Random rand;

    std::shared_ptr<ms::Scene> scene = ms::Scene::create();
    {
        std::shared_ptr<ms::Mesh> node = ms::Mesh::create();
        scene->entities.push_back(node);

        node->path = "/Test/PointMesh";
        node->position = { 0.0f, 0.0f, 0.0f };
        node->rotation = quatf::identity();
        node->scale = { 1.0f, 1.0f, 1.0f };
        node->visibility = { false, true, true };
        MeshGenerator::GenerateIcoSphereMesh(node->counts, node->indices, node->points, node->m_uv[0], 0.1f, 1);
        node->refine_settings.flags.Set(ms::MESH_REFINE_FLAG_GEN_NORMALS, true);
        node->refine_settings.flags.Set(ms::MESH_REFINE_FLAG_GEN_TANGENTS, true);
        {
            ms::Variant test("test", mu::float4::one());
            node->addUserProperty(std::move(test));
        }
    }
    {
        std::shared_ptr<ms::Points> node = ms::Points::create();
        scene->entities.push_back(node);

        node->path = "/Test/PointsT";
        node->reference = "/Test/PointMesh";
        node->position = { -2.5f, 0.0f, 0.0f };

        int N = 100;
        node->points.resize_discard(N);
        for (int i = 0; i < N;++i) {
            node->points[i] = { rand.f11(), rand.f11(), rand.f11() };
        }
        node->setupPointsDataFlags();
    }
    {
        std::shared_ptr<ms::Points> node = ms::Points::create();
        scene->entities.push_back(node);

        node->path = "/Test/PointsTR";
        node->reference = "/Test/PointMesh";
        node->position = { 0.0f, 0.0f, 0.0f };

        const int N = 100;
        node->points.resize_discard(N);
        node->rotations.resize_discard(N);
        for (int i = 0; i < N; ++i) {
            node->points[i] = { rand.f11(), rand.f11(), rand.f11() };
            node->rotations[i] = rotate(rand.v3n(), rand.f11() * mu::PI);
        }
        node->setupPointsDataFlags();
    }
    {
        std::shared_ptr<ms::Points> node = ms::Points::create();
        scene->entities.push_back(node);

        node->path = "/Test/PointsTRS";
        node->reference = "/Test/PointMesh";
        node->position = { 2.5f, 0.0f, 0.0f };

        int N = 100;
        node->points.resize_discard(N);
        node->rotations.resize_discard(N);
        node->scales.resize_discard(N);
        node->colors.resize_discard(N);
        node->velocities.resize_discard(N);
        for (int i = 0; i < N; ++i) {
            node->points[i] = { rand.f11(), rand.f11(), rand.f11() };
            node->rotations[i] = rotate(rand.v3n(), rand.f11() * mu::PI);
            node->scales[i] = { rand.f01(), rand.f01(), rand.f01() };
            node->colors[i] = { rand.f01(), rand.f01(), rand.f01() };
            node->velocities[i] = float3{ rand.f11(), rand.f11(), rand.f11() } *0.1f;
        }
        node->setupPointsDataFlags();

    }
    TestUtility::Send(scene);

    // animation
    {
        const int F = 20;
        std::shared_ptr<ms::Points> node = std::static_pointer_cast<ms::Points>(scene->entities.back());

        for (int fi = 0; fi < F; ++fi) {
            for (int i = 0; i < node->points.size(); ++i)
                node->points[i] += node->velocities[i];

            TestUtility::Send(scene);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

TestCase(Test_SendTexture) {
    auto gen_id = []() {
        static int id_seed = 0;
        return ++id_seed;
    };

    // raw file textures
    const char* testTextures[] = {
        "Texture_RGBA_u8.png",
        "Texture_RGBA_f16.exr",
    };

    const uint32_t numTestTextures = sizeof(testTextures) / sizeof(testTextures[0]);
    int testTexturesID[numTestTextures];

    std::shared_ptr<ms::Scene> scene = ms::Scene::create();
    for (uint32_t i =0;i<numTestTextures;++i) {

        std::shared_ptr<ms::Texture> tex = ms::Texture::create();
        const bool canReadTex = tex->readFromFile(testTextures[i]);
        assert( canReadTex && "Test_SendTexture() failed in loading textures");
        scene->assets.push_back(tex);
        testTexturesID[i] = gen_id();
        tex->id = testTexturesID[i];
    }

    if (!scene->assets.empty())
        TestUtility::Send(scene);


    {
        std::shared_ptr<ms::Scene> scene = ms::Scene::create();

        const int width = 512;
        const int height = 512;
        {
            // Ru8
            const unorm8 black{ 0.0f };
            const unorm8 white{ 1.0f };
            scene->assets.push_back(TestUtility::CreateCheckerImageTexture<unorm8>(black, white, width, height, gen_id(), "Ru8"));
        }
        const int emissionTexID = gen_id();
        {
            // RGu8
            const unorm8x2 black{ 0.0f, 0.0f };
            const unorm8x2 white{ 1.0f, 1.0f };
            scene->assets.push_back(TestUtility::CreateCheckerImageTexture<unorm8x2>(black, white, width, height, emissionTexID, "RGu8"));
        }
        const int metallicTexID = gen_id();
        {
            // RGBu8
            const unorm8x3 black{ 0.0f, 0.0f, 0.0f };
            const unorm8x3 white{ 1.0f, 1.0f, 1.0f };
            scene->assets.push_back(TestUtility::CreateCheckerImageTexture<unorm8x3>(black, white, width, height, metallicTexID, "RGBu8"));
        }
        {
            // RGBAu8
            const unorm8x4 black{ 0.0f, 0.0f, 0.0f, 1.0f };
            const unorm8x4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
            scene->assets.push_back(TestUtility::CreateCheckerImageTexture<unorm8x4>(black, white, width, height, gen_id(), "RGBAu8"));
        }
        {
            // RGBAf16
            const half4 black{ 0.0f, 0.0f, 0.0f, 1.0f };
            const half4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
            scene->assets.push_back(TestUtility::CreateCheckerImageTexture<half4>(black, white, width, height, gen_id(), "RGBAf16"));
        }
        {
            // RGBAf32
            const float4 black{ 0.0f, 0.0f, 0.0f, 1.0f };
            const float4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
            scene->assets.push_back(TestUtility::CreateCheckerImageTexture<float4>(black, white, width, height, gen_id(), "RGBAf32"));
        }

        // material
        {
            std::shared_ptr<ms::Material> mat = ms::Material::create();
            scene->assets.push_back(mat);
            mat->name = "MeshSyncTest Material";
            mat->id = 0;
            ms::StandardMaterial& standardMaterial = ms::AsStandardMaterial(*mat);
            standardMaterial.setColor({ 0.3f, 0.3f, 0.5f, 1.0f });
            standardMaterial.setEmissionColor({ 0.7f, 0.1f, 0.2f, 1.0f });
            standardMaterial.setMetallic(0.2f);
            standardMaterial.setSmoothness(0.8f);
            standardMaterial.setColorMap(testTexturesID[0]);
            standardMaterial.setMetallicMap(metallicTexID);
            standardMaterial.setEmissionMap(emissionTexID);

            standardMaterial.SetDetailAlbedoMap(testTexturesID[1]);
            standardMaterial.SetUVForSecondaryMap(1);

            standardMaterial.addKeyword({ "_EMISSION", true });
            standardMaterial.addKeyword({ "_INVALIDKEYWORD", true });
        }
        TestUtility::Send(scene);
    }
}

TestCase(Test_FileAsset)
{
    std::shared_ptr<ms::Scene> scene = ms::Scene::create();

    // file asset
    {
        std::shared_ptr<ms::FileAsset> as = ms::FileAsset::create();
        if (as->readFromFile("pch.h"))
            scene->assets.push_back(as);
    }
    TestUtility::Send(scene);
}


TestCase(Test_Query)
{
    ms::Client client(TestUtility::GetClientSettings());
    if (!client.isServerAvailable()) {
        const std::string& log = client.getErrorMessage();
        Print("Server not available. error log: %s\n", log.c_str());
        return;
    }

    auto send_query_impl = [&](ms::QueryMessage::QueryType qt, const char *query_name) {
        ms::QueryMessage query;
        query.query_type = qt;
        ms::ResponseMessagePtr response = client.send(query);

        Print("query: %s\n", query_name);
        Print("response:\n");
        if (response) {
            for (auto& t : response->text)
                Print("  %s\n", t.c_str());
        }
        else {
            Print("  no response. error log: %s\n", client.getErrorMessage().c_str());
        }
    };

#define SendQuery(Q) send_query_impl(ms::QueryMessage::QueryType::Q, #Q)
    SendQuery(PluginVersion);
    SendQuery(ProtocolVersion);
    SendQuery(HostName);
    SendQuery(RootNodes);
    SendQuery(AllNodes);
#undef SendQuery
}

#endif