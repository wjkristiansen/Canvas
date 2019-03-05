#include "stdafx.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Canvas;

namespace CanvasUnitTest
{		
    const double g_PI = 3.1415926535897932384626433832795;

	TEST_CLASS(CanvasMathTest)
	{
	public:
		
        bool AlmostZero(double n)
        {
            return n < DBL_EPSILON;
        }

        bool AlmostZero(float n)
        {
            return n < FLT_EPSILON;
        }

        template<class _Type, unsigned int _D>
        bool AlmostEqual(const TVector<_Type, _D> &v0, const TVector<_Type, _D> &v1)
        {
            TVector<_Type, _D> delta = v1 - v0;
            _Type lensq = DotProduct(delta, delta);
            return AlmostZero(lensq);
        }
        template<class _Type>
        bool AlmostEqual(const TQuaternion<_Type> &q0, const TQuaternion<_Type> &q1)
        {
            TQuaternion<_Type> delta = q1 - q0;
            _Type lensq = DotProduct(delta, delta);
            return AlmostZero(lensq);
        }
        template<class _Type, unsigned int _Rows, unsigned int _Columns>
        bool AlmostEqual(const TMatrix<_Type, _Rows, _Columns> &m0, const TMatrix<_Type, _Rows, _Columns> &m1)
        {
            for (unsigned int row = 0; row < _Rows; ++row)
            {
                if (!AlmostEqual(m0.M[row], m1.M[row]))
                {
                    return false;
                }
            }
            return true;
        }

		TEST_METHOD(SimpleVectors)
		{
            TVector<int, 4> V0(1, 2, 3, 4);
            TVector<int, 4> V1(5, 6, 1, 2);
            Assert::AreEqual(V0.Dimension, 4U);
            Assert::AreEqual(V1.X(), 5);
            Assert::AreEqual(V1.Y(), 6);
            Assert::AreEqual(V1.Z(), 1);
            Assert::AreEqual(V1.W(), 2);
            auto SubResult = V1 - V0;
            Assert::IsTrue(SubResult == TVector<int, 4>(4, 4, -2, -2));
            auto AddResult = V1 + V0;
            Assert::IsTrue(AddResult == TVector<int, 4>(6, 8, 4, 6));
            auto MulResult = V1 * V0;
            Assert::IsTrue(MulResult == TVector<int, 4>(5, 12, 3, 8));
            int Dot = DotProduct(SubResult, V0);
            Assert::AreEqual(4 + 8 - 6 - 8, Dot);
            TVector<int, 3> V2(3, 5, 7);
            TVector<int, 3> V3(2, 11, 13);
            auto Cross = CrossProduct(V2, V3);
            Assert::IsTrue(TVector<int, 3>(-12, -25, 23) == Cross);

            TVector<int, 2> V4(11, 13);
            Assert::AreEqual(V4[0], 11);
            Assert::AreEqual(V4[1], 13);

            TVector<int, 6> V5 = { { 1, 1, 2, 3, 5, 8 } };
            TVector<int, 6> V8 = { { 2, 2, 4, 6, 10, 16 } };
            auto V6 = 2 * V5;
            auto V7 = V5 * 2;
            Assert::IsTrue(V8 == V7);
            Assert::IsTrue(V8 == V6);
        }

        TEST_METHOD(SimpleMatrices)
        {
            TMatrix<int, 3, 4> M0 = {
                {1, 1, 2, 3},
                {5, 8, 13, 21},
                {34, 55, 89, 144}
            };
            Assert::AreEqual(M0.Columns, 4U);
            Assert::AreEqual(M0.Rows, 3U);
            TMatrix<int, 4, 4> M1 = {
                {4, 7, 10, 13 },
                {5, 8, 11, 14},
                {6, 9, 12, 15},
                {7, 10, 13, 16}
            };

            auto M2 = M0 * M1;

            TMatrix<int, 3, 4> MMulResult = {
                { 42, 63, 84, 105 },
                { 285, 426, 567, 708 },
                { 1953, 2919, 3885, 4851 }
            };

            Assert::IsTrue(MMulResult == M2);
        }

        TEST_METHOD(Normalize)
        {
            TVector<float, 3> fv[] =
            {
                { 1.0f, 0.0f, 0.0f },
                { 1.0f, 1.0f, 1.0f },
                { 1.0f, 2.0f, 3.0f }
            };

            for (unsigned int i = 0; i < _countof(fv); ++i)
            {
                auto nv = NormalizeVector(fv[i]);
            
                // Verify the magnitude is nearly 1.0f
                float magsq = DotProduct(nv, nv);
                float diff = std::abs(magsq - 1.0f);
                Assert::IsTrue(diff < 0.000001f);
            }

            TVector<double, 4> dv[] =
            {
                { 0.0, 1.0, 0.0, 0.0 },
                { 1.0, 1.0, 1.0, 1.0 },
                { 0.0, 1.0, 2.0, 3.0 }
            };

            for (unsigned int i = 0; i < _countof(dv); ++i)
            {
                auto nv = NormalizeVector(dv[i]);
            
                // Verify the magnitude is nearly 1.0f
                double magsq = DotProduct(nv, nv);
                double diff = std::abs(magsq - 1.0);
                Assert::IsTrue(diff < 0.000001);
            }
        }

        TEST_METHOD(MatrixRotation)
        {
            using MatrixType = TMatrix<double, 3, 3>;
            using VecType = MatrixType::RowType;
            using ElementType = MatrixType::ElementType;
            static const auto Rows = MatrixType::Rows;
            static const auto Columns = MatrixType::Columns;

            MatrixType rx = XRotMatrix<ElementType, Rows, Columns>(g_PI / 2);
            Assert::IsTrue(AlmostEqual(rx[0], VecType(1, 0, 0)));
            Assert::IsTrue(AlmostEqual(rx[1], VecType(0, 0, -1)));
            Assert::IsTrue(AlmostEqual(rx[2], VecType(0, 1, 0)));

            rx = XRotMatrix<ElementType, Rows, Columns>(-g_PI / 2);
            Assert::IsTrue(AlmostEqual(rx[0], VecType(1, 0, 0)));
            Assert::IsTrue(AlmostEqual(rx[1], VecType(0, 0, 1)));
            Assert::IsTrue(AlmostEqual(rx[2], VecType(0, -1, 0)));

            MatrixType ry = YRotMatrix<ElementType, Rows, Columns>(g_PI / 2);
            Assert::IsTrue(AlmostEqual(ry[0], VecType(0, 0, 1)));
            Assert::IsTrue(AlmostEqual(ry[1], VecType(0, 1, 0)));
            Assert::IsTrue(AlmostEqual(ry[2], VecType(-1, 0, 0)));

            ry = YRotMatrix<ElementType, Rows, Columns>(-g_PI / 2);
            Assert::IsTrue(AlmostEqual(ry[0], VecType(0, 0, -1)));
            Assert::IsTrue(AlmostEqual(ry[1], VecType(0, 1, 0)));
            Assert::IsTrue(AlmostEqual(ry[2], VecType(1, 0, 0)));

            MatrixType rz = ZRotMatrix<ElementType, Rows, Columns>(g_PI / 2);
            Assert::IsTrue(AlmostEqual(rz[0], VecType(0, -1, 0)));
            Assert::IsTrue(AlmostEqual(rz[1], VecType(1, 0, 0)));
            Assert::IsTrue(AlmostEqual(rz[2], VecType(0, 0, 1)));

            rz = ZRotMatrix<ElementType, Rows, Columns>(-g_PI / 2);
            Assert::IsTrue(AlmostEqual(rz[0], VecType(0, 1, 0)));
            Assert::IsTrue(AlmostEqual(rz[1], VecType(-1, 0, 0)));
            Assert::IsTrue(AlmostEqual(rz[2], VecType(0, 0, 1)));
        }

        TEST_METHOD(LookAt)
        {
            // Simulate a "LookAt" transform with world-up as the world Z-axis and
            // Camera-up as the camera-local Y-axis and camera-forward as the
            // negative camera-local Z-axis
            auto WorldUp = FloatVector3(0, 0, 1); // World-up is the world Z-axis
            FloatMatrix3x3 M;
            M[2] = FloatVector3(0, 1, 0); // Camera looks in the -Y direction (this is a backward vector)
            ComposeLookBasisVectors(WorldUp, M[2], M[0], M[1]);

            Assert::IsTrue(AlmostEqual(M, FloatMatrix3x3(
                {
                    {-1, 0, 0},
                    {0, 0, 1},
                    {0, 1, 0},
                }
            )));

            
            // Degenerate cases:

            // Should use default out-vector
            M[2] = FloatVector3(0, 0, 1); // Camera looks in the -Z direction
            ComposeLookBasisVectors(WorldUp, M[2], M[0], M[1]);

            // Assume camera-up aligns with the Y-axis
            Assert::IsTrue(AlmostEqual(M, FloatMatrix3x3(
                {
                    {1, 0, 0},
                    {0, 1, 0},
                    {0, 0, 1},
                }
            )));

            // Look straight up (gimbal lock).
            // Should use default out-vector
            M[2] = FloatVector3(0, 0, -1); // Camera looks in the +Z direction
            ComposeLookBasisVectors(WorldUp, M[2], M[0], M[1]);

            // Assume camera-up aligns with the Y-axis
            Assert::IsTrue(AlmostEqual(M, FloatMatrix3x3(
                {
                    {-1, 0, 0},
                    {0, 1, 0},
                    {0, 0, -1},
                }
            )));
        }

        TEST_METHOD(BasicQuaternion)
        {
            TQuaternion<double> Q = IdentityQuaternion<double>();
            Assert::IsTrue(Q == TQuaternion(1., 0., 0., 0.));
            auto M = IdentityMatrix<double, 3, 3>();
            auto N = QuaternionToRotationMatrix(Q);
            Assert::IsTrue(AlmostEqual(M, N));

            TQuaternion<double> R = QuaternionFromAngleAxis(0., DoubleVector3(1., 0., 0.));
            Assert::IsTrue(AlmostEqual(Q, R));

            M = TMatrix<double, 3, 3>(
                {
                    { -1,  0,  0 },
                    {  0, -1,  0 },
                    {  0,  0,  1 }
                }
            );

            R = QuaternionFromRotationMatrix(M);
            N = QuaternionToRotationMatrix(R);
            Assert::IsTrue(AlmostEqual(M, N));

            for (int i = 0; i < 16; i++)
            {
                for (int j = 0; j < 16; j++)
                {
                    for (int k = 1; k < 16; k++)
                    {
                        double rotx = i * g_PI / 16.;
                        double roty = j * g_PI / 16.;
                        double rotz = k * g_PI / 16.;
                        M = XRotMatrix<double, 3, 3>(rotx) * YRotMatrix<double, 3, 3>(roty) * ZRotMatrix<double, 3, 3>(rotz);
                        R = QuaternionFromRotationMatrix(M);
                        N = QuaternionToRotationMatrix(R);

                        // Verify round-trip conversion from matrix->quaternion->matrix produces the original matrix
                        Assert::IsTrue(AlmostEqual(M, N));

                        // Transform a set of vertices by matrix and by quaternion and verify 
                        // the rotated results match

                        TVector<double, 3> Vectors[] =
                        {
                            {1., 0., 0.},
                            {-5., 7, 13.},
                            {6, -2, 11},
                            {6, 22, -11},
                            {.5, .2, 0.},
                        };

                        // Transform each vector by matrix and by quaternion
                        // and verify the results match
                        for (auto &V : Vectors)
                        {
                            // Matrix transform
                            auto VByM = V * M;

                            auto InvR = R.Conjugate();
                            auto Ident = R * InvR;
                            Assert::IsTrue(AlmostEqual(Ident, TQuaternion<double>(1, 0, 0, 0)));

                            // Quaternion transform
                            auto step1 = InvR * V;
                            auto VByQ = step1 * R;

                            Assert::IsTrue(AlmostEqual(VByM, VByQ.V));
                        }
                    }
                }
            }
        }
	};
}