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
            mA_expected[0][3] = 1.0f; mA_expected[1][3] = 2.0f; mA_expected[2][3] = 0.0f; mA_expected[3][3] = 1.0f;
            Assert::IsTrue(AlmostEqual(mA_local, mA_expected));

            // Verify B local matrix
            auto mB_local = pB->GetLocalMatrix();
            auto mB_expected = QuaternionToRotationMatrix(qB);
            mB_expected[0][3] = 1.0f; mB_expected[1][3] = 0.0f; mB_expected[2][3] = 0.0f; mB_expected[3][3] = 1.0f;
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
        }
    };
}