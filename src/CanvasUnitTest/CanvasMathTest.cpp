#include "pch.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Canvas;
using namespace Canvas::Math;

namespace CanvasUnitTest
{		
    volatile float g_mul = 1.0f;

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

        __declspec(noinline) FloatVector4 Mul(const FloatVector4 &a, const FloatVector4 &b)
        {
            return a * b;

        }
		TEST_METHOD(SimpleVectors)
		{
            TVector<int, 4> V0(1, 2, 3, 4);
            TVector<int, 4> V1(5, 6, 1, 2);
            Assert::AreEqual(V0.Dim, 4);
            Assert::AreEqual(V1[0], 5);
            Assert::AreEqual(V1[1], 6);
            Assert::AreEqual(V1[2], 1);
            Assert::AreEqual(V1[3], 2);
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

            TVector<int, 6> V5 = { 1, 1, 2, 3, 5, 8 };
            TVector<int, 6> V8 = { 2, 2, 4, 6, 10, 16 };
            auto V6 = 2 * V5;
            auto V7 = V5 * 2;
            Assert::IsTrue(V8 == V7);
            Assert::IsTrue(V8 == V6);

            FloatVector4 a(1 * g_mul, 2 * g_mul, 3 * g_mul, 4 * g_mul);
            FloatVector4 b(.1f * g_mul, .2f * g_mul, .3f * g_mul, .4f * g_mul);
            FloatVector4 c = Mul(a, b);
            Assert::IsTrue(AlmostEqual(c, FloatVector4(.1f, .4f, .9f, 1.6f)));
        }

        TEST_METHOD(SimpleMatrices)
        {
            TMatrix<int, 3, 4> M0 = {
                {1, 1, 2, 3},
                {5, 8, 13, 21},
                {34, 55, 89, 144}
            };
            Assert::AreEqual(M0.Columns, 4);
            Assert::AreEqual(M0.Rows, 3);
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

            auto T = MatrixTransposeRows(M1);
            Assert::IsTrue(T == TMatrix<int, 4, 4>(
                {4, 5, 6, 7 },
                {7, 8, 9, 10},
                {10, 11, 12, 13},
                {13, 14, 15, 16}
            ));

            T = MatrixTransposeRows(M1, 2, 2, 1);
            Assert::IsTrue(T == TMatrix<int, 4, 4>(
                {4, 7, 10, 13 },
                {5, 8, 11, 14},
                {6, 9, 10, 15},
                {7, 12, 13, 16}
            ));
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
            using MatrixType = FloatMatrix4x4;
            using VecType = MatrixType::RowType;
            using ElementType = MatrixType::ElementType;
            static const auto Rows = MatrixType::Rows;
            static const auto Columns = MatrixType::Columns;

            MatrixType rx = XRotationMatrix<ElementType>(float(g_PI) / 2);
            Assert::IsTrue(AlmostEqual(rx[0], VecType(1, 0, 0, 0)));
            Assert::IsTrue(AlmostEqual(rx[1], VecType(0, 0, -1, 0)));
            Assert::IsTrue(AlmostEqual(rx[2], VecType(0, 1, 0, 0)));

            rx = XRotationMatrix<ElementType>(-float(g_PI) / 2);
            Assert::IsTrue(AlmostEqual(rx[0], VecType(1, 0, 0, 0)));
            Assert::IsTrue(AlmostEqual(rx[1], VecType(0, 0, 1, 0)));
            Assert::IsTrue(AlmostEqual(rx[2], VecType(0, -1, 0, 0)));

            MatrixType ry = YRotationMatrix<ElementType>(float(g_PI) / 2);
            Assert::IsTrue(AlmostEqual(ry[0], VecType(0, 0, 1, 0)));
            Assert::IsTrue(AlmostEqual(ry[1], VecType(0, 1, 0, 0)));
            Assert::IsTrue(AlmostEqual(ry[2], VecType(-1, 0, 0, 0)));
            Assert::IsTrue(AlmostEqual(ry[3], VecType(0, 0, 0, 1)));

            ry = YRotationMatrix<ElementType>(-float(g_PI) / 2);
            Assert::IsTrue(AlmostEqual(ry[0], VecType(0, 0, -1, 0)));
            Assert::IsTrue(AlmostEqual(ry[1], VecType(0, 1, 0, 0)));
            Assert::IsTrue(AlmostEqual(ry[2], VecType(1, 0, 0, 0)));
            Assert::IsTrue(AlmostEqual(ry[3], VecType(0, 0, 0, 1)));

            MatrixType rz = ZRotationMatrix<ElementType>(float(g_PI) / 2);
            Assert::IsTrue(AlmostEqual(rz[0], VecType(0, -1, 0, 0)));
            Assert::IsTrue(AlmostEqual(rz[1], VecType(1, 0, 0, 0)));
            Assert::IsTrue(AlmostEqual(rz[2], VecType(0, 0, 1, 0)));
            Assert::IsTrue(AlmostEqual(ry[3], VecType(0, 0, 0, 1)));

            rz = ZRotationMatrix<ElementType>(-float(g_PI) / 2);
            Assert::IsTrue(AlmostEqual(rz[0], VecType(0, 1, 0, 0)));
            Assert::IsTrue(AlmostEqual(rz[1], VecType(-1, 0, 0, 0)));
            Assert::IsTrue(AlmostEqual(rz[2], VecType(0, 0, 1, 0)));
            Assert::IsTrue(AlmostEqual(ry[3], VecType(0, 0, 0, 1)));
        }

        TEST_METHOD(LookAt)
        {
            // Simulate a "LookAt" transform with world-up as the world Z-axis and
            // Camera-up as the camera-local Y-axis and camera-forward as the
            // negative camera-local Z-axis
            auto WorldUp = FloatVector3(0, 0, 1); // World-up is the world Z-axis
            FloatMatrix3x3 M;
            M[2] = FloatVector3(0, 1, 0); // Camera looks in the -Y direction (this is a backward vector)
            ComposeLookAtBasisVectors(WorldUp, M[2], M[0], M[1]);

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
            ComposeLookAtBasisVectors(WorldUp, M[2], M[0], M[1]);

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
            ComposeLookAtBasisVectors(WorldUp, M[2], M[0], M[1]);

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
            auto Q = IdentityQuaternion<double>();
            Assert::IsTrue(Q == DoubleQuaternion(0, 0, 0, 1));
            auto M = IdentityMatrix<double, 4, 4>();
            auto N = QuaternionToRotationMatrix(Q);
            Assert::IsTrue(AlmostEqual(M, N));

            auto R = QuaternionFromAngleAxis(DoubleVector4(1, 0, 0, 0));
            Assert::IsTrue(AlmostEqual(Q, R));

            M = TMatrix<double, 4, 4>(
                {
                    { -1,  0,  0, 0 },
                    {  0, -1,  0, 0 },
                    {  0,  0,  1, 0 },
                    {  0,  0,  0, 1 }
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
                        M = XRotationMatrix<double>(rotx) * YRotationMatrix<double>(roty) * ZRotationMatrix<double>(rotz);
                        R = QuaternionFromRotationMatrix(M);
                        N = QuaternionToRotationMatrix(R);

                        // Verify round-trip conversion from matrix->quaternion->matrix produces the original matrix
                        Assert::IsTrue(AlmostEqual(M, N));

                        // Transform a set of vertices by matrix and by quaternion and verify 
                        // the rotated results match

                        TVector<double, 4> Vectors[] =
                        {
                            {1., 0., 0., 0},
                            {-5., 7, 13., 0},
                            {6, -2, 11, 0},
                            {6, 22, -11, 0},
                            {.5, .2, 0., 0},
                        };

                        // Transform each vector by matrix and by quaternion
                        // and verify the results match
                        for (auto &V : Vectors)
                        {
                            // Matrix transform
                            auto VByM = M * V;

                            auto InvR = Conjugate(R);
                            auto Ident = R * InvR;
                            Assert::IsTrue(AlmostEqual(Ident, DoubleQuaternion(0, 0, 0, 1)));

                            // Quaternion transform - convert vector to quaternion with w=0
                            DoubleQuaternion VQuat(V.X, V.Y, V.Z, 0);
                            auto VByQ = R * VQuat * InvR;

                            Assert::IsTrue(AlmostEqual(VByM, VByQ));
                        }
                    }
                }
            }
        }

        TEST_METHOD(VectorUnaryMinus)
        {
            IntVector3 v1(1, -2, 3);
            IntVector3 v2 = -v1;
            Assert::IsTrue(v2 == IntVector3(-1, 2, -3));

            FloatVector4 v3(1.5f, -2.5f, 3.5f, -4.5f);
            FloatVector4 v4 = -v3;
            Assert::IsTrue(AlmostEqual(v4, FloatVector4(-1.5f, 2.5f, -3.5f, 4.5f)));
        }

        TEST_METHOD(VectorInequalityAndDivision)
        {
            IntVector3 v1(1, 2, 3);
            IntVector3 v2(1, 2, 3);
            IntVector3 v3(1, 2, 4);
            
            Assert::IsFalse(v1 != v2);
            Assert::IsTrue(v1 != v3);

            FloatVector4 v4(10.0f, 20.0f, 30.0f, 40.0f);
            FloatVector4 v5(2.0f, 4.0f, 5.0f, 8.0f);
            auto v6 = v4 / v5;
            Assert::IsTrue(AlmostEqual(v6, FloatVector4(5.0f, 5.0f, 6.0f, 5.0f)));
        }

        TEST_METHOD(VectorSumTest)
        {
            IntVector4 v1(1, 2, 3, 4);
            int sum = VectorSum(v1);
            Assert::AreEqual(10, sum);

            FloatVector3 v2(1.5f, 2.5f, 3.0f);
            float fsum = VectorSum(v2);
            Assert::IsTrue(AlmostZero(fsum - 7.0f));
        }

        TEST_METHOD(MatrixScalarMultiplication)
        {
            FloatMatrix3x3 m1({
                {1.0f, 2.0f, 3.0f},
                {4.0f, 5.0f, 6.0f},
                {7.0f, 8.0f, 9.0f}
            });

            auto m2 = m1 * 2.0f;
            auto m3 = 2.0f * m1;
            
            FloatMatrix3x3 expected({
                {2.0f, 4.0f, 6.0f},
                {8.0f, 10.0f, 12.0f},
                {14.0f, 16.0f, 18.0f}
            });

            Assert::IsTrue(AlmostEqual(m2, expected));
            Assert::IsTrue(AlmostEqual(m3, expected));
        }

        TEST_METHOD(AllRotationMatrixOrders)
        {
            float angleX = float(g_PI) / 6.0f;  // 30 degrees
            float angleY = float(g_PI) / 4.0f;  // 45 degrees
            float angleZ = float(g_PI) / 3.0f;  // 60 degrees

            // Test XYZRotationMatrix
            auto mXYZ = XYZRotationMatrix(angleX, angleY, angleZ);
            auto mXYZ_manual = ZRotationMatrix(angleZ) * YRotationMatrix(angleY) * XRotationMatrix(angleX);
            Assert::IsTrue(AlmostEqual(mXYZ, mXYZ_manual));

            // Test XZYRotationMatrix
            auto mXZY = XZYRotationMatrix(angleX, angleZ, angleY);
            auto mXZY_manual = YRotationMatrix(angleY) * ZRotationMatrix(angleZ) * XRotationMatrix(angleX);
            Assert::IsTrue(AlmostEqual(mXZY, mXZY_manual));

            // Test YXZRotationMatrix
            auto mYXZ = YXZRotationMatrix(angleY, angleX, angleZ);
            auto mYXZ_manual = ZRotationMatrix(angleZ) * XRotationMatrix(angleX) * YRotationMatrix(angleY);
            Assert::IsTrue(AlmostEqual(mYXZ, mYXZ_manual));

            // Test YZXRotationMatrix
            auto mYZX = YZXRotationMatrix(angleY, angleZ, angleX);
            auto mYZX_manual = XRotationMatrix(angleX) * ZRotationMatrix(angleZ) * YRotationMatrix(angleY);
            Assert::IsTrue(AlmostEqual(mYZX, mYZX_manual));

            // Test ZXYRotationMatrix
            auto mZXY = ZXYRotationMatrix(angleZ, angleX, angleY);
            auto mZXY_manual = YRotationMatrix(angleY) * XRotationMatrix(angleX) * ZRotationMatrix(angleZ);
            Assert::IsTrue(AlmostEqual(mZXY, mZXY_manual));

            // Test ZYXRotationMatrix
            auto mZYX = ZYXRotationMatrix(angleZ, angleY, angleX);
            auto mZYX_manual = XRotationMatrix(angleX) * YRotationMatrix(angleY) * ZRotationMatrix(angleZ);
            Assert::IsTrue(AlmostEqual(mZYX, mZYX_manual));
        }

        TEST_METHOD(QuaternionFromEulerAngles)
        {
            float angleX = float(g_PI) / 6.0f;
            float angleY = float(g_PI) / 4.0f;
            float angleZ = float(g_PI) / 3.0f;

            // Test FromEulerXYZ
            auto qXYZ = FloatQuaternion::FromEulerXYZ(angleX, angleY, angleZ);
            auto mXYZ = XYZRotationMatrix(angleX, angleY, angleZ);
            auto mFromQuat = QuaternionToRotationMatrix(qXYZ);
            Assert::IsTrue(AlmostEqual(mXYZ, mFromQuat));

            // Test FromEulerXZY
            auto qXZY = FloatQuaternion::FromEulerXZY(angleX, angleZ, angleY);
            auto mXZY = XZYRotationMatrix(angleX, angleZ, angleY);
            mFromQuat = QuaternionToRotationMatrix(qXZY);
            Assert::IsTrue(AlmostEqual(mXZY, mFromQuat));

            // Test FromEulerYXZ
            auto qYXZ = FloatQuaternion::FromEulerYXZ(angleY, angleX, angleZ);
            auto mYXZ = YXZRotationMatrix(angleY, angleX, angleZ);
            mFromQuat = QuaternionToRotationMatrix(qYXZ);
            Assert::IsTrue(AlmostEqual(mYXZ, mFromQuat));

            // Test FromEulerYZX
            auto qYZX = FloatQuaternion::FromEulerYZX(angleY, angleZ, angleX);
            auto mYZX = YZXRotationMatrix(angleY, angleZ, angleX);
            mFromQuat = QuaternionToRotationMatrix(qYZX);
            Assert::IsTrue(AlmostEqual(mYZX, mFromQuat));

            // Test FromEulerZXY
            auto qZXY = FloatQuaternion::FromEulerZXY(angleZ, angleX, angleY);
            auto mZXY = ZXYRotationMatrix(angleZ, angleX, angleY);
            mFromQuat = QuaternionToRotationMatrix(qZXY);
            Assert::IsTrue(AlmostEqual(mZXY, mFromQuat));

            // Test FromEulerZYX
            auto qZYX = FloatQuaternion::FromEulerZYX(angleZ, angleY, angleX);
            auto mZYX = ZYXRotationMatrix(angleZ, angleY, angleX);
            mFromQuat = QuaternionToRotationMatrix(qZYX);
            Assert::IsTrue(AlmostEqual(mZYX, mFromQuat));
        }

        TEST_METHOD(QuaternionScaledAxisAngle)
        {
            // Test round-trip conversion
            FloatVector3 axis1(1.0f, 0.0f, 0.0f);
            float angle1 = float(g_PI) / 4.0f;
            FloatVector3 scaledAxis1(axis1.X * angle1, axis1.Y * angle1, axis1.Z * angle1);
            
            auto q1 = FloatQuaternion::FromScaledAxisAngle(scaledAxis1);
            auto result1 = q1.ToScaledAxisAngle();
            Assert::IsTrue(AlmostEqual(scaledAxis1, result1));

            // Test with arbitrary axis
            FloatVector3 axis2(1.0f, 2.0f, 3.0f);
            float angle2 = float(g_PI) / 3.0f;
            float len = std::sqrt(axis2.X * axis2.X + axis2.Y * axis2.Y + axis2.Z * axis2.Z);
            axis2 = axis2 * (1.0f / len); // normalize
            FloatVector3 scaledAxis2(axis2.X * angle2, axis2.Y * angle2, axis2.Z * angle2);
            
            auto q2 = FloatQuaternion::FromScaledAxisAngle(scaledAxis2);
            auto result2 = q2.ToScaledAxisAngle();
            Assert::IsTrue(AlmostEqual(scaledAxis2, result2));

            // Test zero rotation
            FloatVector3 scaledAxis3(0.0f, 0.0f, 0.0f);
            auto q3 = FloatQuaternion::FromScaledAxisAngle(scaledAxis3);
            Assert::IsTrue(AlmostEqual(q3, IdentityQuaternion<float>()));
        }

        TEST_METHOD(QuaternionMultiplication)
        {
            // Test that quaternion multiplication follows the expected composition
            float angle1 = float(g_PI) / 4.0f;
            float angle2 = float(g_PI) / 3.0f;
            
            auto qX = FloatQuaternion::FromEulerXYZ(angle1, 0.0f, 0.0f);
            auto qY = FloatQuaternion::FromEulerXYZ(0.0f, angle2, 0.0f);
            
            // Quaternion multiplication
            auto qResult = qY * qX;
            
            // Equivalent matrix multiplication
            auto mX = XRotationMatrix(angle1);
            auto mY = YRotationMatrix(angle2);
            auto mResult = mY * mX;
            
            // Convert quaternion result to matrix
            auto mFromQuat = QuaternionToRotationMatrix(qResult);
            
            Assert::IsTrue(AlmostEqual(mResult, mFromQuat));

            // Test identity property
            auto qIdentity = IdentityQuaternion<float>();
            auto qTest = FloatQuaternion::FromEulerXYZ(0.1f, 0.2f, 0.3f);
            auto qResult1 = qIdentity * qTest;
            auto qResult2 = qTest * qIdentity;
            Assert::IsTrue(AlmostEqual(qTest, qResult1));
            Assert::IsTrue(AlmostEqual(qTest, qResult2));
        }

        TEST_METHOD(QuaternionRotateVector)
        {
            // Create a 90-degree rotation about the Z-axis
            float angle = float(g_PI) / 2.0f;
            auto qZ = FloatQuaternion::FromEulerXYZ(0.0f, 0.0f, angle);
            
            // Rotate a vector using quaternion multiplication: q * v * q*
            FloatVector4 v1(1.0f, 0.0f, 0.0f, 0.0f);
            FloatQuaternion vQuat1(v1.X, v1.Y, v1.Z, 0.0f);
            auto qConj = Conjugate(qZ);
            auto rotated = qZ * vQuat1 * qConj;
            
            // Should point along Y-axis
            Assert::IsTrue(AlmostEqual(rotated, FloatVector4(0.0f, 1.0f, 0.0f, 0.0f)));

            // Test 90-degree rotation about X-axis
            auto qX = FloatQuaternion::FromEulerXYZ(angle, 0.0f, 0.0f);
            FloatVector4 v2(0.0f, 1.0f, 0.0f, 0.0f);
            FloatQuaternion vQuat2(v2.X, v2.Y, v2.Z, 0.0f);
            auto qConjX = Conjugate(qX);
            auto rotated2 = qX * vQuat2 * qConjX;
            
            // Should point along Z-axis
            Assert::IsTrue(AlmostEqual(rotated2, FloatVector4(0.0f, 0.0f, 1.0f, 0.0f)));
        }

        TEST_METHOD(CrossProductFloatVector4)
        {
            // Test the specialized FloatVector4 cross product
            FloatVector4 a(1.0f, 0.0f, 0.0f, 0.0f);
            FloatVector4 b(0.0f, 1.0f, 0.0f, 0.0f);
            
            auto c = CrossProduct(a, b);
            Assert::IsTrue(AlmostEqual(c, FloatVector4(0.0f, 0.0f, 1.0f, 0.0f)));

            // Test arbitrary vectors
            FloatVector4 v1(2.0f, 3.0f, 4.0f, 0.0f);
            FloatVector4 v2(5.0f, 6.0f, 7.0f, 0.0f);
            auto v3 = CrossProduct(v1, v2);
            
            // Cross product formula: (a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x)
            FloatVector4 expected(3.0f*7.0f - 4.0f*6.0f, 4.0f*5.0f - 2.0f*7.0f, 2.0f*6.0f - 3.0f*5.0f, 0.0f);
            Assert::IsTrue(AlmostEqual(v3, expected));
        }

        TEST_METHOD(AABBTests)
        {
            // Test default constructor creates invalid box
            AABB box1;
            Assert::IsFalse(box1.IsValid());

            // Test valid constructor
            AABB box2(FloatVector4(0.0f, 0.0f, 0.0f, 0.0f), FloatVector4(1.0f, 1.0f, 1.0f, 0.0f));
            Assert::IsTrue(box2.IsValid());

            // Test center and extents
            auto center = box2.GetCenter();
            auto extents = box2.GetExtents();
            Assert::IsTrue(AlmostEqual(center, FloatVector4(0.5f, 0.5f, 0.5f, 0.0f)));
            Assert::IsTrue(AlmostEqual(extents, FloatVector4(0.5f, 0.5f, 0.5f, 0.0f)));

            // Test ExpandToInclude with point
            AABB box3;
            box3.ExpandToInclude(FloatVector4(1.0f, 2.0f, 3.0f, 0.0f));
            Assert::IsTrue(box3.IsValid());
            box3.ExpandToInclude(FloatVector4(4.0f, 5.0f, 6.0f, 0.0f));
            Assert::IsTrue(AlmostEqual(box3.Min, FloatVector4(1.0f, 2.0f, 3.0f, 0.0f)));
            Assert::IsTrue(AlmostEqual(box3.Max, FloatVector4(4.0f, 5.0f, 6.0f, 0.0f)));

            // Test ExpandToInclude with another AABB
            AABB box4(FloatVector4(0.0f, 0.0f, 0.0f, 0.0f), FloatVector4(2.0f, 2.0f, 2.0f, 0.0f));
            AABB box5(FloatVector4(1.0f, 1.0f, 1.0f, 0.0f), FloatVector4(3.0f, 3.0f, 3.0f, 0.0f));
            box4.ExpandToInclude(box5);
            Assert::IsTrue(AlmostEqual(box4.Min, FloatVector4(0.0f, 0.0f, 0.0f, 0.0f)));
            Assert::IsTrue(AlmostEqual(box4.Max, FloatVector4(3.0f, 3.0f, 3.0f, 0.0f)));

            // Test Reset
            box4.Reset();
            Assert::IsFalse(box4.IsValid());
        }

        TEST_METHOD(QuaternionNormalize)
        {
            // Test that normalize works correctly
            FloatQuaternion q1(0.5f, 0.5f, 0.5f, 0.5f);
            auto qn = q1.Normalize();
            
            // Should be unit length
            float len = DotProduct(qn, qn);
            Assert::IsTrue(AlmostZero(len - 1.0f));

            // Test already normalized quaternion
            auto qIdentity = IdentityQuaternion<float>();
            auto qIdentityNorm = qIdentity.Normalize();
            Assert::IsTrue(AlmostEqual(qIdentity, qIdentityNorm));
        }

        TEST_METHOD(QuaternionConjugate)
        {
            FloatQuaternion q(0.1f, 0.2f, 0.3f, 0.9f);
            auto qConj = Conjugate(q);
            
            Assert::IsTrue(AlmostEqual(qConj, FloatQuaternion(-0.1f, -0.2f, -0.3f, 0.9f)));

            // Test that q * conjugate(q) = identity for unit quaternions
            auto qNorm = q.Normalize();
            auto qConjNorm = Conjugate(qNorm);
            auto result = qNorm * qConjNorm;
            Assert::IsTrue(AlmostEqual(result, IdentityQuaternion<float>()));
        }

        TEST_METHOD(VectorMatrixMultiplication)
        {
            // Test vector * matrix (row vector)
            FloatVector3 v(1.0f, 2.0f, 3.0f);
            FloatMatrix3x3 m({
                {1.0f, 0.0f, 0.0f},
                {0.0f, 2.0f, 0.0f},
                {0.0f, 0.0f, 3.0f}
            });
            
            auto result = v * m;
            Assert::IsTrue(AlmostEqual(result, FloatVector3(1.0f, 4.0f, 9.0f)));

            // Test matrix * vector (column vector)
            auto result2 = m * v;
            Assert::IsTrue(AlmostEqual(result2, FloatVector3(1.0f, 4.0f, 9.0f)));
        }

        TEST_METHOD(MatrixIdentity)
        {
            // Test static Identity() method
            auto m2 = FloatMatrix2x2::Identity();
            Assert::AreEqual(1.0f, m2[0][0]);
            Assert::AreEqual(0.0f, m2[0][1]);
            Assert::AreEqual(0.0f, m2[1][0]);
            Assert::AreEqual(1.0f, m2[1][1]);

            auto m3 = FloatMatrix3x3::Identity();
            Assert::IsTrue(AlmostEqual(m3, IdentityMatrix<float, 3, 3>()));

            auto m4 = FloatMatrix4x4::Identity();
            Assert::IsTrue(AlmostEqual(m4, IdentityMatrix<float, 4, 4>()));
        }
	};
}