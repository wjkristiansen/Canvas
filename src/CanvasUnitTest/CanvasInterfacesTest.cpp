//================================================================================================
// CanvasInterfacesTest
//================================================================================================

#include "pch.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Canvas;

namespace CanvasUnitTest
{
    TEST_CLASS(CanvasInterfacesTest)
    {
    public:

        TEST_METHOD(SimpleInterfaces)
        {
            // Create XCanvas object
            Gem::TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(Canvas::CreateCanvas(&pCanvas)));

            // Create XScene object
            Gem::TGemPtr<Canvas::XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(&pScene)));

            // Create an empty XSceneGraphNode
            Gem::TGemPtr<Canvas::XSceneGraphNode> pSceneGraphNode;
            const char szNodeName[] = "NullNode";
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pSceneGraphNode)));
            pSceneGraphNode->SetName(szNodeName);

            // Verify QI for XGeneric
            Gem::TGemPtr<XGeneric> pGeneric;
            Assert::IsTrue(Succeeded(pSceneGraphNode->QueryInterface(&pGeneric)));

            // Validate the name
            Assert::IsTrue(0 == strncmp(pSceneGraphNode->GetName(), szNodeName, _countof(szNodeName)));
        }

        TEST_METHOD(SceneGraphNodesTest)
        {
            // Create XCanvas object
            Gem::TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(Canvas::CreateCanvas(&pCanvas)));

            // Create XScene object
            Gem::TGemPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(&pScene)));

            // Create nodes
            Gem::TGemPtr<XSceneGraphNode> pNodes[6];
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[0])));
            pNodes[0]->SetName("Node0");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[1])));
            pNodes[1]->SetName("Node1");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[2])));
            pNodes[2]->SetName("Node2");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[3])));
            pNodes[3]->SetName("Node3");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[4])));
            pNodes[4]->SetName("Node4");
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pNodes[5])));
            pNodes[5]->SetName("Node5");

            // Build the following tree
            // Root
            //  - Node0
            //      - Node2
            //      - Node3
            //  - Node1
            //      - Node4
            //          - Node5

            Gem::TGemPtr<XSceneGraphNode> pRoot = pScene->GetRootSceneGraphNode();
            Assert::IsNotNull(pRoot.Get());
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[0])));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pNodes[1])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[2])));
            Assert::IsTrue(Succeeded(pNodes[0]->AddChild(pNodes[3])));
            Assert::IsTrue(Succeeded(pNodes[1]->AddChild(pNodes[4])));
            Assert::IsTrue(Succeeded(pNodes[4]->AddChild(pNodes[5])));
        }

        // Helpers for equality checks
        static bool AlmostZero(float n)
        {
            return std::abs(n) < FLT_EPSILON;
        }
        static bool AlmostEqual(const Canvas::Math::FloatVector4 &a, const Canvas::Math::FloatVector4 &b)
        {
            auto d = b - a;
            float lensq = Canvas::Math::DotProduct(d, d);
            return AlmostZero(lensq);
        }
        static bool AlmostEqual(const Canvas::Math::FloatQuaternion &a, const Canvas::Math::FloatQuaternion &b)
        {
            auto d = b - a;
            float lensq = Canvas::Math::DotProduct(d, d);
            return AlmostZero(lensq);
        }
        static bool AlmostEqual(const Canvas::Math::FloatMatrix4x4 &m0, const Canvas::Math::FloatMatrix4x4 &m1)
        {
            for (unsigned int row = 0; row < Canvas::Math::FloatMatrix4x4::Rows; ++row)
            {
                auto d = m1[row] - m0[row];
                float lensq = Canvas::Math::DotProduct(d, d);
                if (!AlmostZero(lensq)) return false;
            }
            return true;
        }

        TEST_METHOD(SceneGraphTransforms)
        {
            using namespace Canvas::Math;

            // Create Canvas and Scene
            Gem::TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(Canvas::CreateCanvas(&pCanvas)));
            Gem::TGemPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(&pScene)));

            // Create a small hierarchy: root -> A -> B
            Gem::TGemPtr<XSceneGraphNode> pA, pB;
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pA)));
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pB)));

            Gem::TGemPtr<XSceneGraphNode> pRoot = pScene->GetRootSceneGraphNode();
            Assert::IsNotNull(pRoot.Get());
            Assert::IsTrue(Succeeded(pRoot->AddChild(pA)));
            Assert::IsTrue(Succeeded(pA->AddChild(pB)));

            // Set locals
            // A: rotate 90 deg around Z, translate (1,2,0)
            const float ninety = float(3.14159265358979323846 / 2.0);
            auto qA = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety);
            pA->SetLocalRotation(qA);
            pA->SetLocalTranslation(FloatVector4(1.0f, 2.0f, 0.0f, 1.0f));

            // B: rotate 90 deg around X, translate (1,0,0)
            auto qB = FloatQuaternion::FromEulerXYZ(ninety, 0.0f, 0.0f);
            pB->SetLocalRotation(qB);
            pB->SetLocalTranslation(FloatVector4(1.0f, 0.0f, 0.0f, 1.0f));

            // Verify A local matrix matches quaternion + translation
            auto mA_local = pA->GetLocalMatrix();
            auto mA_expected = QuaternionToRotationMatrix(qA);
            mA_expected[3][0] = 1.0f; mA_expected[3][1] = 2.0f; mA_expected[3][2] = 0.0f; mA_expected[3][3] = 1.0f;
            Assert::IsTrue(AlmostEqual(mA_local, mA_expected));

            // Verify B local matrix
            auto mB_local = pB->GetLocalMatrix();
            auto mB_expected = QuaternionToRotationMatrix(qB);
            mB_expected[3][0] = 1.0f; mB_expected[3][1] = 0.0f; mB_expected[3][2] = 0.0f; mB_expected[3][3] = 1.0f;
            Assert::IsTrue(AlmostEqual(mB_local, mB_expected));

            // Global rotations: Rg(A) = qA, Rg(B) = qA * qB
            auto qA_g = pA->GetGlobalRotation();
            auto qB_g = pB->GetGlobalRotation();
            Assert::IsTrue(AlmostEqual(qA_g, qA.Normalize()));
            Assert::IsTrue(AlmostEqual(qB_g, (qA * qB).Normalize()));

            // Global translations:
            // Tg(A) = rotate(qRoot, (1,2,0)) + Troot = (1,2,0)
            // Tg(B) = Tg(A) + rotate(Rg(A), (1,0,0)) = (1,2,0) + (0,1,0) = (1,3,0)
            auto tA_g = pA->GetGlobalTranslation();
            auto tB_g = pB->GetGlobalTranslation();
            Assert::IsTrue(AlmostEqual(tA_g, FloatVector4(1.0f, 2.0f, 0.0f, 1.0f)));
            Assert::IsTrue(AlmostEqual(tB_g, FloatVector4(1.0f, 3.0f, 0.0f, 1.0f)));

            // Global matrices: Mg = Mparent * Mlocal (row-vector convention)
            auto mA_global = pA->GetGlobalMatrix();
            auto mB_global = pB->GetGlobalMatrix();
            auto mRoot = FloatMatrix4x4::Identity();
            auto mA_global_expected = mRoot * mA_local; // root is identity
            auto mB_global_expected = mA_global_expected * mB_local;
            Assert::IsTrue(AlmostEqual(mA_global, mA_global_expected));
            Assert::IsTrue(AlmostEqual(mB_global, mB_global_expected));

            // Test scenario: Move B from A to Root after transforms are resolved
            // This tests that dirty flags are properly set when parent changes
            
            // First, create a new node C under Root for verification
            Gem::TGemPtr<XSceneGraphNode> pC;
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pC)));
            auto qC = FloatQuaternion::FromEulerXYZ(0.0f, ninety, 0.0f);  // 90° around Y
            pC->SetLocalRotation(qC);
            pC->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 1.0f, 1.0f));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pC)));

            // Verify C's initial global transforms
            auto qC_g_initial = pC->GetGlobalRotation();
            auto tC_g_initial = pC->GetGlobalTranslation();
            Assert::IsTrue(AlmostEqual(qC_g_initial, qC.Normalize()));
            Assert::IsTrue(AlmostEqual(tC_g_initial, FloatVector4(0.0f, 0.0f, 1.0f, 1.0f)));

            // Now move B from A to C (B was previously under A)
            // B's local is: rotation 90° around X, translation (1,0,0)
            // When B is under C (which has 90° Y rotation and translation (0,0,1)):
            //   - B's global rotation should be: qC * qB
            //   - B's global translation should be: tC + rotate(qC, (1,0,0)) = (0,0,1) + (0,0,-1) = (0,0,0)

            Assert::IsTrue(Succeeded(pC->AddChild(pB)));

            // After moving, B's global transforms should reflect new parent C
            auto qB_g_moved = pB->GetGlobalRotation();
            auto tB_g_moved = pB->GetGlobalTranslation();
            auto mB_global_moved = pB->GetGlobalMatrix();

            // Expected: qC * qB
            auto qB_g_expected_moved = (qC * qB).Normalize();
            Assert::IsTrue(AlmostEqual(qB_g_moved, qB_g_expected_moved));

            // Expected translation: C's translation (0,0,1) + rotate(qC, B's local translation (1,0,0))
            // qC rotates (1,0,0) by 90° around Y: (1,0,0) -> (0,0,-1)
            const FloatQuaternion vB_local(1.0f, 0.0f, 0.0f, 0.0f);
            const auto qC_conj = Conjugate(qC.Normalize());
            const auto vB_rotated = qC.Normalize() * vB_local * qC_conj;
            FloatVector4 tB_expected_moved = tC_g_initial + FloatVector4(vB_rotated.X, vB_rotated.Y, vB_rotated.Z, 0.0f);
            tB_expected_moved.W = 1.0f;
            Assert::IsTrue(AlmostEqual(tB_g_moved, tB_expected_moved));

            // Expected matrix: C_global * B_local
            auto mC_global = pC->GetGlobalMatrix();
            auto mB_global_expected_moved = mC_global * mB_local;
            Assert::IsTrue(AlmostEqual(mB_global_moved, mB_global_expected_moved));

            // Verify that A's child is no longer B (B was moved to C)
            auto pA_firstChild = pA->GetFirstChild();
            Assert::IsTrue(pA_firstChild == nullptr || pA_firstChild != pB.Get());

            // Test that changing a parent's transform propagates to descendants
            // B is currently a child of C. If we change C's rotation/translation,
            // B's global transforms should update accordingly.
            
            // Get B's current global matrix and local matrix (local shouldn't change)
            auto mB_before_parent_change = pB->GetGlobalMatrix();
            auto mB_local_saved = pB->GetLocalMatrix();
            
            // Change C's local translation
            pC->SetLocalTranslation(FloatVector4(5.0f, 5.0f, 5.0f, 1.0f));
            
            // B's local matrix should remain unchanged
            auto mB_local_check = pB->GetLocalMatrix();
            Assert::IsTrue(AlmostEqual(mB_local_saved, mB_local_check));
            
            // B's global matrix should now be different (dirty flags propagated)
            auto mB_after_translation = pB->GetGlobalMatrix();
            Assert::IsFalse(AlmostEqual(mB_before_parent_change, mB_after_translation));
            
            // Verify the new value is correct: C_new_global * B_local
            auto mC_global_new = pC->GetGlobalMatrix();
            auto mB_expected_new = mC_global_new * mB_local_saved;
            Assert::IsTrue(AlmostEqual(mB_after_translation, mB_expected_new));
            
            // Change C's rotation
            auto qC_new = FloatQuaternion::FromEulerXYZ(ninety, 0.0f, 0.0f);  // 90° around X instead of Y
            pC->SetLocalRotation(qC_new);
            
            // B's global transforms should update again
            auto mB_after_rotation = pB->GetGlobalMatrix();
            Assert::IsFalse(AlmostEqual(mB_after_translation, mB_after_rotation));
            
            // Verify correctness after rotation change
            auto mC_global_final = pC->GetGlobalMatrix();
            auto mB_expected_final = mC_global_final * mB_local_saved;
            Assert::IsTrue(AlmostEqual(mB_after_rotation, mB_expected_final));
        }

        TEST_METHOD(CameraTest)
        {
            using namespace Canvas::Math;

            // Create Canvas and Scene
            Gem::TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(Canvas::CreateCanvas(&pCanvas)));
            Gem::TGemPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(&pScene)));

            // Create a hierarchy: root -> A -> B -> cameraNode
            Gem::TGemPtr<XSceneGraphNode> pA, pB, pCameraNode;
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pA)));
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pB)));
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pCameraNode)));

            Gem::TGemPtr<XSceneGraphNode> pRoot = pScene->GetRootSceneGraphNode();
            Assert::IsNotNull(pRoot.Get());
            Assert::IsTrue(Succeeded(pRoot->AddChild(pA)));
            Assert::IsTrue(Succeeded(pA->AddChild(pB)));
            Assert::IsTrue(Succeeded(pB->AddChild(pCameraNode)));

            // Create camera and attach to cameraNode
            Gem::TGemPtr<XCamera> pCamera;
            Assert::IsTrue(Succeeded(pCanvas->CreateCamera(&pCamera)));
            Assert::IsTrue(Succeeded(pCamera->AttachTo(pCameraNode)));

            // Set initial camera parameters
            const float fov = float(3.14159265358979323846 / 4.0); // 45 degrees
            const float aspect = 16.0f / 9.0f;
            const float nearClip = 0.1f;
            const float farClip = 1000.0f;
            pCamera->SetFovAngle(fov);
            pCamera->SetAspectRatio(aspect);
            pCamera->SetNearClip(nearClip);
            pCamera->SetFarClip(farClip);

            // Set transforms for the hierarchy
            const float ninety = float(3.14159265358979323846 / 2.0);
            auto qA = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety);
            pA->SetLocalRotation(qA);
            pA->SetLocalTranslation(FloatVector4(1.0f, 0.0f, 0.0f, 1.0f));

            auto qB = FloatQuaternion::FromEulerXYZ(ninety, 0.0f, 0.0f);
            pB->SetLocalRotation(qB);
            pB->SetLocalTranslation(FloatVector4(0.0f, 1.0f, 0.0f, 1.0f));

            pCameraNode->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 5.0f, 1.0f));

            // Update scene graph and camera (simulates frame update)
            Assert::IsTrue(Succeeded(pScene->Update(0.0f)));

            // Get initial matrices - this will compute and cache them
            auto viewMatrix1 = pCamera->GetViewMatrix();
            auto projMatrix1 = pCamera->GetProjectionMatrix();
            auto viewProjMatrix1 = pCamera->GetViewProjectionMatrix();

            // Verify matrices are not identity
            auto identity = FloatMatrix4x4::Identity();
            Assert::IsFalse(AlmostEqual(viewMatrix1, identity));
            Assert::IsFalse(AlmostEqual(projMatrix1, identity));

            // Verify view-projection is the product of view and projection
            auto expectedViewProj = viewMatrix1 * projMatrix1;
            Assert::IsTrue(AlmostEqual(viewProjMatrix1, expectedViewProj));

            // Test 1: Modify camera node's local transform
            // This should invalidate view matrix after Update()
            pCameraNode->SetLocalTranslation(FloatVector4(0.0f, 0.0f, 10.0f, 1.0f));
            Assert::IsTrue(Succeeded(pScene->Update(0.0f)));

            auto viewMatrix2 = pCamera->GetViewMatrix();
            Assert::IsFalse(AlmostEqual(viewMatrix1, viewMatrix2)); // Should be different

            // Projection should remain the same (no parameters changed)
            auto projMatrix2 = pCamera->GetProjectionMatrix();
            Assert::IsTrue(AlmostEqual(projMatrix1, projMatrix2));

            // View-projection should be different
            auto viewProjMatrix2 = pCamera->GetViewProjectionMatrix();
            Assert::IsFalse(AlmostEqual(viewProjMatrix1, viewProjMatrix2));

            // Test 2: Modify parent node B's transform
            // This should invalidate camera's view matrix (ancestor changed)
            pB->SetLocalTranslation(FloatVector4(0.0f, 2.0f, 0.0f, 1.0f));
            Assert::IsTrue(Succeeded(pScene->Update(0.0f)));

            auto viewMatrix3 = pCamera->GetViewMatrix();
            Assert::IsFalse(AlmostEqual(viewMatrix2, viewMatrix3)); // Should be different from previous

            // Test 3: Modify grandparent node A's transform
            // This should also invalidate camera's view matrix
            pA->SetLocalTranslation(FloatVector4(2.0f, 0.0f, 0.0f, 1.0f));
            Assert::IsTrue(Succeeded(pScene->Update(0.0f)));

            auto viewMatrix4 = pCamera->GetViewMatrix();
            Assert::IsFalse(AlmostEqual(viewMatrix3, viewMatrix4)); // Should be different

            // Test 4: Modify projection parameters
            // This should invalidate projection and view-projection matrices
            pCamera->SetFovAngle(float(3.14159265358979323846 / 3.0)); // 60 degrees
            auto projMatrix3 = pCamera->GetProjectionMatrix();
            Assert::IsFalse(AlmostEqual(projMatrix2, projMatrix3)); // Should be different

            auto viewProjMatrix3 = pCamera->GetViewProjectionMatrix();
            Assert::IsFalse(AlmostEqual(viewProjMatrix2, viewProjMatrix3));

            // Verify new view-projection is correct product
            auto viewMatrixCurrent = pCamera->GetViewMatrix();
            auto projMatrixCurrent = pCamera->GetProjectionMatrix();
            auto expectedViewProjCurrent = viewMatrixCurrent * projMatrixCurrent;
            Assert::IsTrue(AlmostEqual(viewProjMatrix3, expectedViewProjCurrent));

            // Test 5: Modify aspect ratio
            pCamera->SetAspectRatio(4.0f / 3.0f);
            auto projMatrix4 = pCamera->GetProjectionMatrix();
            Assert::IsFalse(AlmostEqual(projMatrix3, projMatrix4));

            // Test 6: Modify near/far clip planes
            pCamera->SetNearClip(0.5f);
            auto projMatrix5 = pCamera->GetProjectionMatrix();
            Assert::IsFalse(AlmostEqual(projMatrix4, projMatrix5));

            pCamera->SetFarClip(500.0f);
            auto projMatrix6 = pCamera->GetProjectionMatrix();
            Assert::IsFalse(AlmostEqual(projMatrix5, projMatrix6));
        }

        TEST_METHOD(CameraNodeReparenting)
        {
            using namespace Canvas::Math;

            // Create Canvas and Scene
            Gem::TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(Canvas::CreateCanvas(&pCanvas)));
            Gem::TGemPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(&pScene)));

            // Create two separate branches: root -> A and root -> B
            Gem::TGemPtr<XSceneGraphNode> pA, pB, pCameraNode;
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pA)));
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pB)));
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pCameraNode)));

            Gem::TGemPtr<XSceneGraphNode> pRoot = pScene->GetRootSceneGraphNode();
            Assert::IsNotNull(pRoot.Get());
            
            // Initial setup: cameraNode is under A
            Assert::IsTrue(Succeeded(pRoot->AddChild(pA)));
            Assert::IsTrue(Succeeded(pRoot->AddChild(pB)));
            Assert::IsTrue(Succeeded(pA->AddChild(pCameraNode)));

            // Create camera and attach
            Gem::TGemPtr<XCamera> pCamera;
            Assert::IsTrue(Succeeded(pCanvas->CreateCamera(&pCamera)));
            Assert::IsTrue(Succeeded(pCamera->AttachTo(pCameraNode)));

            // Set different transforms for A and B
            const float ninety = float(3.14159265358979323846 / 2.0);
            auto qA = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, ninety); // Rotate 90° around Z
            pA->SetLocalRotation(qA);
            pA->SetLocalTranslation(FloatVector4(5.0f, 0.0f, 0.0f, 1.0f));

            auto qB = FloatQuaternion::FromEulerXYZ(0.0f, ninety, 0.0f); // Rotate 90° around Y
            pB->SetLocalRotation(qB);
            pB->SetLocalTranslation(FloatVector4(0.0f, 5.0f, 0.0f, 1.0f));

            pCameraNode->SetLocalTranslation(FloatVector4(1.0f, 0.0f, 0.0f, 1.0f));

            // Update and get initial view matrix (camera is under A)
            Assert::IsTrue(Succeeded(pScene->Update(0.0f)));
            auto viewMatrix1 = pCamera->GetViewMatrix();
            auto viewProjMatrix1 = pCamera->GetViewProjectionMatrix();

            // Move camera node from A to B
            // This changes the camera's parent, so its world transform will change
            Assert::IsTrue(Succeeded(pB->AddChild(pCameraNode)));

            // Update scene to mark view matrix dirty
            Assert::IsTrue(Succeeded(pScene->Update(0.0f)));

            // Get new matrices - should be different due to different parent transform
            auto viewMatrix2 = pCamera->GetViewMatrix();
            auto viewProjMatrix2 = pCamera->GetViewProjectionMatrix();

            // Matrices should be different (different parent transforms)
            Assert::IsFalse(AlmostEqual(viewMatrix1, viewMatrix2));
            Assert::IsFalse(AlmostEqual(viewProjMatrix1, viewProjMatrix2));

            // Verify the view matrix is the inverse of the camera's world transform
            auto cameraWorldMatrix = pCameraNode->GetGlobalMatrix();
            
            // For a proper inverse check, multiply view * world should give identity
            // But since we're using row vectors, it's world * view = identity
            auto shouldBeIdentity = cameraWorldMatrix * viewMatrix2;
            
            // Check if result is close to identity (accounting for floating point error)
            auto identity = FloatMatrix4x4::Identity();
            Assert::IsTrue(AlmostEqual(shouldBeIdentity, identity));

            // Test moving to root (no parent transform)
            Assert::IsTrue(Succeeded(pRoot->AddChild(pCameraNode)));
            Assert::IsTrue(Succeeded(pScene->Update(0.0f)));

            auto viewMatrix3 = pCamera->GetViewMatrix();
            Assert::IsFalse(AlmostEqual(viewMatrix2, viewMatrix3));

            // With only local transform, world matrix should equal local matrix
            auto cameraLocalMatrix = pCameraNode->GetLocalMatrix();
            auto cameraWorldMatrix2 = pCameraNode->GetGlobalMatrix();
            Assert::IsTrue(AlmostEqual(cameraLocalMatrix, cameraWorldMatrix2));
        }

        TEST_METHOD(CameraMatrixConsistency)
        {
            using namespace Canvas::Math;

            // Create Canvas and Scene
            Gem::TGemPtr<XCanvas> pCanvas;
            Assert::IsTrue(Succeeded(Canvas::CreateCanvas(&pCanvas)));
            Gem::TGemPtr<XScene> pScene;
            Assert::IsTrue(Succeeded(pCanvas->CreateScene(&pScene)));

            // Create camera node
            Gem::TGemPtr<XSceneGraphNode> pCameraNode;
            Assert::IsTrue(Succeeded(pCanvas->CreateSceneGraphNode(&pCameraNode)));
            
            Gem::TGemPtr<XSceneGraphNode> pRoot = pScene->GetRootSceneGraphNode();
            Assert::IsTrue(Succeeded(pRoot->AddChild(pCameraNode)));

            // Create and attach camera
            Gem::TGemPtr<XCamera> pCamera;
            Assert::IsTrue(Succeeded(pCanvas->CreateCamera(&pCamera)));
            Assert::IsTrue(Succeeded(pCamera->AttachTo(pCameraNode)));

            // Set camera parameters
            pCamera->SetFovAngle(float(3.14159265358979323846 / 4.0));
            pCamera->SetAspectRatio(16.0f / 9.0f);
            pCamera->SetNearClip(0.1f);
            pCamera->SetFarClip(1000.0f);

            // Position camera
            const float ninety = float(3.14159265358979323846 / 2.0);
            auto qCamera = FloatQuaternion::FromEulerXYZ(0.0f, ninety, 0.0f);
            pCameraNode->SetLocalRotation(qCamera);
            pCameraNode->SetLocalTranslation(FloatVector4(10.0f, 5.0f, 0.0f, 1.0f));

            Assert::IsTrue(Succeeded(pScene->Update(0.0f)));

            // Test: Calling GetViewProjectionMatrix should compute all three matrices
            auto viewProjMatrix = pCamera->GetViewProjectionMatrix();
            
            // Now calling individual getters should return cached values (not recompute)
            auto viewMatrix = pCamera->GetViewMatrix();
            auto projMatrix = pCamera->GetProjectionMatrix();

            // Verify view-projection is the correct product
            auto expectedViewProj = viewMatrix * projMatrix;
            Assert::IsTrue(AlmostEqual(viewProjMatrix, expectedViewProj));

            // Test: Calling individual getters first, then combined
            pCameraNode->SetLocalTranslation(FloatVector4(20.0f, 10.0f, 0.0f, 1.0f));
            Assert::IsTrue(Succeeded(pScene->Update(0.0f)));

            auto viewMatrix2 = pCamera->GetViewMatrix();
            auto projMatrix2 = pCamera->GetProjectionMatrix();
            auto viewProjMatrix2 = pCamera->GetViewProjectionMatrix();

            // Verify consistency
            auto expectedViewProj2 = viewMatrix2 * projMatrix2;
            Assert::IsTrue(AlmostEqual(viewProjMatrix2, expectedViewProj2));

            // Test: Multiple calls without changes should return same matrices
            auto viewMatrix3 = pCamera->GetViewMatrix();
            auto projMatrix3 = pCamera->GetProjectionMatrix();
            auto viewProjMatrix3 = pCamera->GetViewProjectionMatrix();

            Assert::IsTrue(AlmostEqual(viewMatrix2, viewMatrix3));
            Assert::IsTrue(AlmostEqual(projMatrix2, projMatrix3));
            Assert::IsTrue(AlmostEqual(viewProjMatrix2, viewProjMatrix3));
        }
    };
}
