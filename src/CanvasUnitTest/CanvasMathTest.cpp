#include "pch.h"
using namespace Canvas;
using namespace Canvas::Math;

namespace CanvasUnitTest
{		
    volatile float g_mul = 1.0f;

    const double g_PI = 3.1415926535897932384626433832795;

namespace {

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

} // anonymous namespace

TEST(CanvasMathTest, SimpleVectors)
{
    TVector<int, 4> V0(1, 2, 3, 4);
    TVector<int, 4> V1(5, 6, 1, 2);
    EXPECT_EQ(V0.Dim, 4);
    EXPECT_EQ(V1[0], 5);
    EXPECT_EQ(V1[1], 6);
    EXPECT_EQ(V1[2], 1);
    EXPECT_EQ(V1[3], 2);
    auto SubResult = V1 - V0;
    EXPECT_TRUE((SubResult == TVector<int, 4>(4, 4, -2, -2)));
    auto AddResult = V1 + V0;
    EXPECT_TRUE((AddResult == TVector<int, 4>(6, 8, 4, 6)));
    auto MulResult = V1 * V0;
    EXPECT_TRUE((MulResult == TVector<int, 4>(5, 12, 3, 8)));
    int Dot = DotProduct(SubResult, V0);
    EXPECT_EQ(4 + 8 - 6 - 8, Dot);
    TVector<int, 3> V2(3, 5, 7);
    TVector<int, 3> V3(2, 11, 13);
    auto Cross = CrossProduct(V2, V3);
    EXPECT_TRUE((TVector<int, 3>(-12, -25, 23) == Cross));

    TVector<int, 2> V4(11, 13);
    EXPECT_EQ(V4[0], 11);
    EXPECT_EQ(V4[1], 13);

    TVector<int, 6> V5 = { 1, 1, 2, 3, 5, 8 };
    TVector<int, 6> V8 = { 2, 2, 4, 6, 10, 16 };
    auto V6 = 2 * V5;
    auto V7 = V5 * 2;
    EXPECT_TRUE(V8 == V7);
    EXPECT_TRUE(V8 == V6);

    FloatVector4 a(1 * g_mul, 2 * g_mul, 3 * g_mul, 4 * g_mul);
    FloatVector4 b(.1f * g_mul, .2f * g_mul, .3f * g_mul, .4f * g_mul);
    FloatVector4 c = Mul(a, b);
    EXPECT_TRUE(AlmostEqual(c, FloatVector4(.1f, .4f, .9f, 1.6f)));
}

TEST(CanvasMathTest, SimpleMatrices)
{
    TMatrix<int, 3, 4> M0 = {
        {1, 1, 2, 3},
        {5, 8, 13, 21},
        {34, 55, 89, 144}
    };
    EXPECT_EQ(M0.Columns, 4);
    EXPECT_EQ(M0.Rows, 3);
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

    EXPECT_TRUE(MMulResult == M2);

    auto T = MatrixTransposeRows(M1);
    EXPECT_TRUE((T == TMatrix<int, 4, 4>(
        {4, 5, 6, 7 },
        {7, 8, 9, 10},
        {10, 11, 12, 13},
        {13, 14, 15, 16}
    )));

    T = MatrixTransposeRows(M1, 2, 2, 1);
    EXPECT_TRUE((T == TMatrix<int, 4, 4>(
        {4, 7, 10, 13 },
        {5, 8, 11, 14},
        {6, 9, 10, 15},
        {7, 12, 13, 16}
    )));
}

TEST(CanvasMathTest, Normalize)
{
    TVector<float, 3> fv[] =
    {
        { 1.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 2.0f, 3.0f }
    };

    for (unsigned int i = 0; i < _countof(fv); ++i)
    {
        auto nv = fv[i].Normalize();

        // Verify the magnitude is nearly 1.0f
        float magsq = DotProduct(nv, nv);
        float diff = std::abs(magsq - 1.0f);
        EXPECT_TRUE(diff < 0.000001f);
    }

    TVector<double, 4> dv[] =
    {
        { 0.0, 1.0, 0.0, 0.0 },
        { 1.0, 1.0, 1.0, 1.0 },
        { 0.0, 1.0, 2.0, 3.0 }
    };

    for (unsigned int i = 0; i < _countof(dv); ++i)
    {
        auto nv = dv[i].Normalize();

        // Verify the magnitude is nearly 1.0f
        double magsq = DotProduct(nv, nv);
        double diff = std::abs(magsq - 1.0);
        EXPECT_TRUE(diff < 0.000001);
    }
}

TEST(CanvasMathTest, MatrixRotation)
{
    using MatrixType = FloatMatrix4x4;
    using VecType = MatrixType::RowType;
    using ElementType = MatrixType::ElementType;
    static const auto Rows = MatrixType::Rows;
    static const auto Columns = MatrixType::Columns;

    MatrixType rx = XRotationMatrix<ElementType>(float(g_PI) / 2);
    EXPECT_TRUE(AlmostEqual(rx[0], VecType(1, 0, 0, 0)));
    EXPECT_TRUE(AlmostEqual(rx[1], VecType(0, 0, -1, 0)));
    EXPECT_TRUE(AlmostEqual(rx[2], VecType(0, 1, 0, 0)));

    rx = XRotationMatrix<ElementType>(-float(g_PI) / 2);
    EXPECT_TRUE(AlmostEqual(rx[0], VecType(1, 0, 0, 0)));
    EXPECT_TRUE(AlmostEqual(rx[1], VecType(0, 0, 1, 0)));
    EXPECT_TRUE(AlmostEqual(rx[2], VecType(0, -1, 0, 0)));

    MatrixType ry = YRotationMatrix<ElementType>(float(g_PI) / 2);
    EXPECT_TRUE(AlmostEqual(ry[0], VecType(0, 0, 1, 0)));
    EXPECT_TRUE(AlmostEqual(ry[1], VecType(0, 1, 0, 0)));
    EXPECT_TRUE(AlmostEqual(ry[2], VecType(-1, 0, 0, 0)));
    EXPECT_TRUE(AlmostEqual(ry[3], VecType(0, 0, 0, 1)));

    ry = YRotationMatrix<ElementType>(-float(g_PI) / 2);
    EXPECT_TRUE(AlmostEqual(ry[0], VecType(0, 0, -1, 0)));
    EXPECT_TRUE(AlmostEqual(ry[1], VecType(0, 1, 0, 0)));
    EXPECT_TRUE(AlmostEqual(ry[2], VecType(1, 0, 0, 0)));
    EXPECT_TRUE(AlmostEqual(ry[3], VecType(0, 0, 0, 1)));

    MatrixType rz = ZRotationMatrix<ElementType>(float(g_PI) / 2);
    EXPECT_TRUE(AlmostEqual(rz[0], VecType(0, -1, 0, 0)));
    EXPECT_TRUE(AlmostEqual(rz[1], VecType(1, 0, 0, 0)));
    EXPECT_TRUE(AlmostEqual(rz[2], VecType(0, 0, 1, 0)));
    EXPECT_TRUE(AlmostEqual(ry[3], VecType(0, 0, 0, 1)));

    rz = ZRotationMatrix<ElementType>(-float(g_PI) / 2);
    EXPECT_TRUE(AlmostEqual(rz[0], VecType(0, 1, 0, 0)));
    EXPECT_TRUE(AlmostEqual(rz[1], VecType(-1, 0, 0, 0)));
    EXPECT_TRUE(AlmostEqual(rz[2], VecType(0, 0, 1, 0)));
    EXPECT_TRUE(AlmostEqual(ry[3], VecType(0, 0, 0, 1)));
}

TEST(CanvasMathTest, LookAt)
{
    // ComposePointToBasisVectors is a general utility that works with any coordinate system.
    // It computes: basisSideVector = WorldUp × ForwardVector, basisUpVector = ForwardVector × basisSideVector
    // This test verifies the math works correctly regardless of camera conventions.

    auto WorldUp = FloatVector3(0, 0, 1); // World-up is the world Z-axis
    FloatMatrix3x3 M;

    // Test case 1: Forward vector pointing in -Y direction
    auto forward1 = FloatVector3(0, -1, 0);
    ComposePointToBasisVectors(WorldUp, forward1, M[0], M[1]);
    M[2] = forward1;

    // Expected:
    // side = WorldUp × forward = (0,0,1) × (0,-1,0) = (1,0,0)
    // up = forward × side = (0,-1,0) × (1,0,0) = (0,0,1)
    EXPECT_TRUE(AlmostEqual(M, FloatMatrix3x3(
        {
            {1, 0, 0},     // side
            {0, 0, 1},     // up  
            {0, -1, 0},    // forward
        }
    )));

    // Degenerate case 1: Forward vector aligned with world-up
    auto forward2 = FloatVector3(0, 0, 1);
    ComposePointToBasisVectors(WorldUp, forward2, M[0], M[1]);
    M[2] = forward2;

    // When forward is parallel to WorldUp, function picks an arbitrary perpendicular side vector
    EXPECT_TRUE(AlmostEqual(M, FloatMatrix3x3(
        {
            {1, 0, 0},     // side (arbitrary perpendicular)
            {0, 1, 0},     // up (perpendicular to forward and side)
            {0, 0, 1},     // forward
        }
    )));

    // Degenerate case 2: Forward vector anti-parallel to world-up
    auto forward3 = FloatVector3(0, 0, -1);
    ComposePointToBasisVectors(WorldUp, forward3, M[0], M[1]);
    M[2] = forward3;

    // When forward is anti-parallel to WorldUp, function picks an arbitrary perpendicular side vector (flipped)
    // side = (-1, 0, 0) from function's formula: (-localUp[2], localUp[0], -localUp[1])
    // up = forward × side = (0,0,-1) × (-1,0,0) = (0, 1, 0)
    EXPECT_TRUE(AlmostEqual(M, FloatMatrix3x3(
        {
            {-1, 0, 0},    // side (arbitrary perpendicular, negative)
            {0, 1, 0},     // up (perpendicular to forward and side)
            {0, 0, -1},    // forward
        }
    )));
}

TEST(CanvasMathTest, LookAtArbitraryBasis)
{
    // Test ComposeArbitraryPointToBasisVectors which automatically selects
    // a reference up vector based on the forward vector
    FloatVector3 side, up;

    // Test case 1: Forward pointing in +X direction
    // Should choose Y or Z as reference (whichever is less aligned)
    auto forward1 = FloatVector3(1, 0, 0);
    ComposeArbitraryPointToBasisVectors(forward1, side, up);

    // Verify orthonormality
    EXPECT_TRUE(AlmostZero(DotProduct(forward1, side)));
    EXPECT_TRUE(AlmostZero(DotProduct(forward1, up)));
    EXPECT_TRUE(AlmostZero(DotProduct(side, up)));
    float len1 = DotProduct(forward1, forward1);
    EXPECT_TRUE(AlmostZero(len1 - 1.0f));
    float lenSide1 = DotProduct(side, side);
    EXPECT_TRUE(AlmostZero(lenSide1 - 1.0f));
    float lenUp1 = DotProduct(up, up);
    EXPECT_TRUE(AlmostZero(lenUp1 - 1.0f));

    // Verify right-handedness: forward × side = up
    auto computedUp = CrossProduct(forward1, side);
    EXPECT_TRUE(AlmostEqual(computedUp, up));

    // Test case 2: Forward pointing in +Y direction
    auto forward2 = FloatVector3(0, 1, 0);
    ComposeArbitraryPointToBasisVectors(forward2, side, up);

    EXPECT_TRUE(AlmostZero(DotProduct(forward2, side)));
    EXPECT_TRUE(AlmostZero(DotProduct(forward2, up)));
    EXPECT_TRUE(AlmostZero(DotProduct(side, up)));
    float lenSide2 = DotProduct(side, side);
    EXPECT_TRUE(AlmostZero(lenSide2 - 1.0f));
    float lenUp2 = DotProduct(up, up);
    EXPECT_TRUE(AlmostZero(lenUp2 - 1.0f));
    computedUp = CrossProduct(forward2, side);
    EXPECT_TRUE(AlmostEqual(computedUp, up));

    // Test case 3: Forward pointing in +Z direction
    auto forward3 = FloatVector3(0, 0, 1);
    ComposeArbitraryPointToBasisVectors(forward3, side, up);

    EXPECT_TRUE(AlmostZero(DotProduct(forward3, side)));
    EXPECT_TRUE(AlmostZero(DotProduct(forward3, up)));
    EXPECT_TRUE(AlmostZero(DotProduct(side, up)));
    float lenSide3 = DotProduct(side, side);
    EXPECT_TRUE(AlmostZero(lenSide3 - 1.0f));
    float lenUp3 = DotProduct(up, up);
    EXPECT_TRUE(AlmostZero(lenUp3 - 1.0f));
    computedUp = CrossProduct(forward3, side);
    EXPECT_TRUE(AlmostEqual(computedUp, up));

    // Test case 4: Forward pointing in -X direction
    auto forward4 = FloatVector3(-1, 0, 0);
    ComposeArbitraryPointToBasisVectors(forward4, side, up);

    EXPECT_TRUE(AlmostZero(DotProduct(forward4, side)));
    EXPECT_TRUE(AlmostZero(DotProduct(forward4, up)));
    EXPECT_TRUE(AlmostZero(DotProduct(side, up)));
    float lenSide4 = DotProduct(side, side);
    EXPECT_TRUE(AlmostZero(lenSide4 - 1.0f));
    float lenUp4 = DotProduct(up, up);
    EXPECT_TRUE(AlmostZero(lenUp4 - 1.0f));
    computedUp = CrossProduct(forward4, side);
    EXPECT_TRUE(AlmostEqual(computedUp, up));

    // Test case 5: Arbitrary diagonal direction
    auto forward5 = FloatVector3(1, 1, 1).Normalize();
    ComposeArbitraryPointToBasisVectors(forward5, side, up);

    EXPECT_TRUE(AlmostZero(DotProduct(forward5, side)));
    EXPECT_TRUE(AlmostZero(DotProduct(forward5, up)));
    EXPECT_TRUE(AlmostZero(DotProduct(side, up)));
    float lenSide5 = DotProduct(side, side);
    EXPECT_TRUE(AlmostZero(lenSide5 - 1.0f));
    float lenUp5 = DotProduct(up, up);
    EXPECT_TRUE(AlmostZero(lenUp5 - 1.0f));
    computedUp = CrossProduct(forward5, side);
    EXPECT_TRUE(AlmostEqual(computedUp, up));

    // Test case 6: Another arbitrary direction
    auto forward6 = FloatVector3(0.6f, -0.8f, 0).Normalize();
    ComposeArbitraryPointToBasisVectors(forward6, side, up);

    EXPECT_TRUE(AlmostZero(DotProduct(forward6, side)));
    EXPECT_TRUE(AlmostZero(DotProduct(forward6, up)));
    EXPECT_TRUE(AlmostZero(DotProduct(side, up)));
    float lenSide6 = DotProduct(side, side);
    EXPECT_TRUE(AlmostZero(lenSide6 - 1.0f));
    float lenUp6 = DotProduct(up, up);
    EXPECT_TRUE(AlmostZero(lenUp6 - 1.0f));
    computedUp = CrossProduct(forward6, side);
    EXPECT_TRUE(AlmostEqual(computedUp, up));

    // Test case 7: Near-axis aligned (stress test)
    auto forward7 = FloatVector3(0.0001f, 0.9999f, 0).Normalize();
    ComposeArbitraryPointToBasisVectors(forward7, side, up);

    EXPECT_TRUE(AlmostZero(DotProduct(forward7, side)));
    EXPECT_TRUE(AlmostZero(DotProduct(forward7, up)));
    EXPECT_TRUE(AlmostZero(DotProduct(side, up)));
    float lenSide7 = DotProduct(side, side);
    EXPECT_TRUE(AlmostZero(lenSide7 - 1.0f));
    float lenUp7 = DotProduct(up, up);
    EXPECT_TRUE(AlmostZero(lenUp7 - 1.0f));
    computedUp = CrossProduct(forward7, side);
    EXPECT_TRUE(AlmostEqual(computedUp, up));
}

TEST(CanvasMathTest, BasicQuaternion)
{
    auto Q = IdentityQuaternion<double>();
    EXPECT_TRUE(Q == DoubleQuaternion(0, 0, 0, 1));
    auto M = IdentityMatrix<double, 4, 4>();
    auto N = QuaternionToRotationMatrix(Q);
    EXPECT_TRUE(AlmostEqual(M, N));

    auto R = QuaternionFromAngleAxis(DoubleVector4(1, 0, 0, 0));
    EXPECT_TRUE(AlmostEqual(Q, R));

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
    EXPECT_TRUE(AlmostEqual(M, N));

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
                EXPECT_TRUE(AlmostEqual(M, N));

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
                    EXPECT_TRUE(AlmostEqual(Ident, DoubleQuaternion(0, 0, 0, 1)));

                    // Quaternion transform - convert vector to quaternion with w=0
                    DoubleQuaternion VQuat(V.X, V.Y, V.Z, 0);
                    auto VByQ = R * VQuat * InvR;

                    EXPECT_TRUE(AlmostEqual(VByM, VByQ));
                }
            }
        }
    }
}

TEST(CanvasMathTest, VectorUnaryMinus)
{
    IntVector3 v1(1, -2, 3);
    IntVector3 v2 = -v1;
    EXPECT_TRUE(v2 == IntVector3(-1, 2, -3));

    FloatVector4 v3(1.5f, -2.5f, 3.5f, -4.5f);
    FloatVector4 v4 = -v3;
    EXPECT_TRUE(AlmostEqual(v4, FloatVector4(-1.5f, 2.5f, -3.5f, 4.5f)));
}

TEST(CanvasMathTest, VectorInequalityAndDivision)
{
    IntVector3 v1(1, 2, 3);
    IntVector3 v2(1, 2, 3);
    IntVector3 v3(1, 2, 4);

    EXPECT_FALSE(v1 != v2);
    EXPECT_TRUE(v1 != v3);

    FloatVector4 v4(10.0f, 20.0f, 30.0f, 40.0f);
    FloatVector4 v5(2.0f, 4.0f, 5.0f, 8.0f);
    auto v6 = v4 / v5;
    EXPECT_TRUE(AlmostEqual(v6, FloatVector4(5.0f, 5.0f, 6.0f, 5.0f)));
}

TEST(CanvasMathTest, VectorSumTest)
{
    IntVector4 v1(1, 2, 3, 4);
    int sum = VectorSum(v1);
    EXPECT_EQ(10, sum);

    FloatVector3 v2(1.5f, 2.5f, 3.0f);
    float fsum = VectorSum(v2);
    EXPECT_TRUE(AlmostZero(fsum - 7.0f));
}

TEST(CanvasMathTest, MatrixScalarMultiplication)
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

    EXPECT_TRUE(AlmostEqual(m2, expected));
    EXPECT_TRUE(AlmostEqual(m3, expected));
}

TEST(CanvasMathTest, AllRotationMatrixOrders)
{
    float angleX = float(g_PI) / 6.0f;  // 30 degrees
    float angleY = float(g_PI) / 4.0f;  // 45 degrees
    float angleZ = float(g_PI) / 3.0f;  // 60 degrees

    // Test XYZRotationMatrix
    auto mXYZ = XYZRotationMatrix(angleX, angleY, angleZ);
    auto mXYZ_manual = ZRotationMatrix(angleZ) * YRotationMatrix(angleY) * XRotationMatrix(angleX);
    EXPECT_TRUE(AlmostEqual(mXYZ, mXYZ_manual));

    // Test XZYRotationMatrix
    auto mXZY = XZYRotationMatrix(angleX, angleZ, angleY);
    auto mXZY_manual = YRotationMatrix(angleY) * ZRotationMatrix(angleZ) * XRotationMatrix(angleX);
    EXPECT_TRUE(AlmostEqual(mXZY, mXZY_manual));

    // Test YXZRotationMatrix
    auto mYXZ = YXZRotationMatrix(angleY, angleX, angleZ);
    auto mYXZ_manual = ZRotationMatrix(angleZ) * XRotationMatrix(angleX) * YRotationMatrix(angleY);
    EXPECT_TRUE(AlmostEqual(mYXZ, mYXZ_manual));

    // Test YZXRotationMatrix
    auto mYZX = YZXRotationMatrix(angleY, angleZ, angleX);
    auto mYZX_manual = XRotationMatrix(angleX) * ZRotationMatrix(angleZ) * YRotationMatrix(angleY);
    EXPECT_TRUE(AlmostEqual(mYZX, mYZX_manual));

    // Test ZXYRotationMatrix
    auto mZXY = ZXYRotationMatrix(angleZ, angleX, angleY);
    auto mZXY_manual = YRotationMatrix(angleY) * XRotationMatrix(angleX) * ZRotationMatrix(angleZ);
    EXPECT_TRUE(AlmostEqual(mZXY, mZXY_manual));

    // Test ZYXRotationMatrix
    auto mZYX = ZYXRotationMatrix(angleZ, angleY, angleX);
    auto mZYX_manual = XRotationMatrix(angleX) * YRotationMatrix(angleY) * ZRotationMatrix(angleZ);
    EXPECT_TRUE(AlmostEqual(mZYX, mZYX_manual));
}

TEST(CanvasMathTest, QuaternionFromEulerAngles)
{
    float angleX = float(g_PI) / 6.0f;
    float angleY = float(g_PI) / 4.0f;
    float angleZ = float(g_PI) / 3.0f;

    // Test FromEulerXYZ
    auto qXYZ = FloatQuaternion::FromEulerXYZ(angleX, angleY, angleZ);
    auto mXYZ = XYZRotationMatrix(angleX, angleY, angleZ);
    auto mFromQuat = QuaternionToRotationMatrix(qXYZ);
    EXPECT_TRUE(AlmostEqual(mXYZ, mFromQuat));

    // Test FromEulerXZY
    auto qXZY = FloatQuaternion::FromEulerXZY(angleX, angleZ, angleY);
    auto mXZY = XZYRotationMatrix(angleX, angleZ, angleY);
    mFromQuat = QuaternionToRotationMatrix(qXZY);
    EXPECT_TRUE(AlmostEqual(mXZY, mFromQuat));

    // Test FromEulerYXZ
    auto qYXZ = FloatQuaternion::FromEulerYXZ(angleY, angleX, angleZ);
    auto mYXZ = YXZRotationMatrix(angleY, angleX, angleZ);
    mFromQuat = QuaternionToRotationMatrix(qYXZ);
    EXPECT_TRUE(AlmostEqual(mYXZ, mFromQuat));

    // Test FromEulerYZX
    auto qYZX = FloatQuaternion::FromEulerYZX(angleY, angleZ, angleX);
    auto mYZX = YZXRotationMatrix(angleY, angleZ, angleX);
    mFromQuat = QuaternionToRotationMatrix(qYZX);
    EXPECT_TRUE(AlmostEqual(mYZX, mFromQuat));

    // Test FromEulerZXY
    auto qZXY = FloatQuaternion::FromEulerZXY(angleZ, angleX, angleY);
    auto mZXY = ZXYRotationMatrix(angleZ, angleX, angleY);
    mFromQuat = QuaternionToRotationMatrix(qZXY);
    EXPECT_TRUE(AlmostEqual(mZXY, mFromQuat));

    // Test FromEulerZYX
    auto qZYX = FloatQuaternion::FromEulerZYX(angleZ, angleY, angleX);
    auto mZYX = ZYXRotationMatrix(angleZ, angleY, angleX);
    mFromQuat = QuaternionToRotationMatrix(qZYX);
    EXPECT_TRUE(AlmostEqual(mZYX, mFromQuat));
}

TEST(CanvasMathTest, QuaternionScaledAxisAngle)
{
    // Test round-trip conversion
    FloatVector3 axis1(1.0f, 0.0f, 0.0f);
    float angle1 = float(g_PI) / 4.0f;
    FloatVector3 scaledAxis1(axis1.X * angle1, axis1.Y * angle1, axis1.Z * angle1);

    auto q1 = FloatQuaternion::FromScaledAxisAngle(scaledAxis1);
    auto result1 = q1.ToScaledAxisAngle();
    EXPECT_TRUE(AlmostEqual(scaledAxis1, result1));

    // Test with arbitrary axis
    FloatVector3 axis2(1.0f, 2.0f, 3.0f);
    float angle2 = float(g_PI) / 3.0f;
    float len = std::sqrt(axis2.X * axis2.X + axis2.Y * axis2.Y + axis2.Z * axis2.Z);
    axis2 = axis2 * (1.0f / len); // normalize
    FloatVector3 scaledAxis2(axis2.X * angle2, axis2.Y * angle2, axis2.Z * angle2);

    auto q2 = FloatQuaternion::FromScaledAxisAngle(scaledAxis2);
    auto result2 = q2.ToScaledAxisAngle();
    EXPECT_TRUE(AlmostEqual(scaledAxis2, result2));

    // Test zero rotation
    FloatVector3 scaledAxis3(0.0f, 0.0f, 0.0f);
    auto q3 = FloatQuaternion::FromScaledAxisAngle(scaledAxis3);
    EXPECT_TRUE(AlmostEqual(q3, IdentityQuaternion<float>()));
}

TEST(CanvasMathTest, QuaternionMultiplication)
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

    EXPECT_TRUE(AlmostEqual(mResult, mFromQuat));

    // Test identity property
    auto qIdentity = IdentityQuaternion<float>();
    auto qTest = FloatQuaternion::FromEulerXYZ(0.1f, 0.2f, 0.3f);
    auto qResult1 = qIdentity * qTest;
    auto qResult2 = qTest * qIdentity;
    EXPECT_TRUE(AlmostEqual(qTest, qResult1));
    EXPECT_TRUE(AlmostEqual(qTest, qResult2));
}

TEST(CanvasMathTest, QuaternionRotateVector)
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
    EXPECT_TRUE(AlmostEqual(rotated, FloatVector4(0.0f, 1.0f, 0.0f, 0.0f)));

    // Test 90-degree rotation about X-axis
    auto qX = FloatQuaternion::FromEulerXYZ(angle, 0.0f, 0.0f);
    FloatVector4 v2(0.0f, 1.0f, 0.0f, 0.0f);
    FloatQuaternion vQuat2(v2.X, v2.Y, v2.Z, 0.0f);
    auto qConjX = Conjugate(qX);
    auto rotated2 = qX * vQuat2 * qConjX;

    // Should point along Z-axis
    EXPECT_TRUE(AlmostEqual(rotated2, FloatVector4(0.0f, 0.0f, 1.0f, 0.0f)));
}

TEST(CanvasMathTest, CrossProductFloatVector4)
{
    // Test the specialized FloatVector4 cross product
    FloatVector4 a(1.0f, 0.0f, 0.0f, 0.0f);
    FloatVector4 b(0.0f, 1.0f, 0.0f, 0.0f);

    auto c = CrossProduct(a, b);
    EXPECT_TRUE(AlmostEqual(c, FloatVector4(0.0f, 0.0f, 1.0f, 0.0f)));

    // Test arbitrary vectors
    FloatVector4 v1(2.0f, 3.0f, 4.0f, 0.0f);
    FloatVector4 v2(5.0f, 6.0f, 7.0f, 0.0f);
    auto v3 = CrossProduct(v1, v2);

    // Cross product formula: (a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x)
    FloatVector4 expected(3.0f*7.0f - 4.0f*6.0f, 4.0f*5.0f - 2.0f*7.0f, 2.0f*6.0f - 3.0f*5.0f, 0.0f);
    EXPECT_TRUE(AlmostEqual(v3, expected));
}

TEST(CanvasMathTest, AABBTests)
{
    // Default-constructed AABB is the canonical empty (FLT_MAX
    // inside-out sentinel) used as the aggregation starting point.
    AABB box1;
    EXPECT_TRUE(box1.IsEmpty());
    EXPECT_TRUE(box1.IsValid());   // empty is intentional, not malformed

    // A constructor with explicit min<=max produces a non-empty,
    // valid box.
    AABB box2(FloatVector4(0.0f, 0.0f, 0.0f, 0.0f), FloatVector4(1.0f, 1.0f, 1.0f, 0.0f));
    EXPECT_FALSE(box2.IsEmpty());
    EXPECT_TRUE(box2.IsValid());

    // Test center and extents
    auto center = box2.GetCenter();
    auto extents = box2.GetExtents();
    EXPECT_TRUE(AlmostEqual(center, FloatVector4(0.5f, 0.5f, 0.5f, 0.0f)));
    EXPECT_TRUE(AlmostEqual(extents, FloatVector4(0.5f, 0.5f, 0.5f, 0.0f)));

    // Single-point ExpandToInclude on an empty AABB collapses to a
    // degenerate (zero-volume) AABB at that point -- non-empty,
    // and well-formed because min == max.
    AABB box3;
    box3.ExpandToInclude(FloatVector4(1.0f, 2.0f, 3.0f, 0.0f));
    EXPECT_FALSE(box3.IsEmpty());
    EXPECT_TRUE(box3.IsValid());
    EXPECT_TRUE(AlmostEqual(box3.Min, FloatVector4(1.0f, 2.0f, 3.0f, 0.0f)));
    EXPECT_TRUE(AlmostEqual(box3.Max, FloatVector4(1.0f, 2.0f, 3.0f, 0.0f)));
    box3.ExpandToInclude(FloatVector4(4.0f, 5.0f, 6.0f, 0.0f));
    EXPECT_TRUE(AlmostEqual(box3.Min, FloatVector4(1.0f, 2.0f, 3.0f, 0.0f)));
    EXPECT_TRUE(AlmostEqual(box3.Max, FloatVector4(4.0f, 5.0f, 6.0f, 0.0f)));

    // Test ExpandToInclude with another AABB
    AABB box4(FloatVector4(0.0f, 0.0f, 0.0f, 0.0f), FloatVector4(2.0f, 2.0f, 2.0f, 0.0f));
    AABB box5(FloatVector4(1.0f, 1.0f, 1.0f, 0.0f), FloatVector4(3.0f, 3.0f, 3.0f, 0.0f));
    box4.ExpandToInclude(box5);
    EXPECT_TRUE(AlmostEqual(box4.Min, FloatVector4(0.0f, 0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(AlmostEqual(box4.Max, FloatVector4(3.0f, 3.0f, 3.0f, 0.0f)));

    // Reset restores the canonical empty sentinel.
    box4.Reset();
    EXPECT_TRUE(box4.IsEmpty());
    EXPECT_TRUE(box4.IsValid());
}

TEST(CanvasMathTest, AABBInsideOutNonSentinelIsInvalidNotEmpty)
{
    // An AABB that is inside-out in some axis but is NOT the
    // canonical FLT_MAX sentinel is INVALID -- not empty.
    // Treating it as empty would mask a construction-side bug
    // and cause aggregation to silently skip it.
    AABB box(FloatVector4(0.0f, 5.0f, 0.0f, 0.0f),
             FloatVector4(10.0f, 2.0f, 10.0f, 0.0f));  // Y inside-out
    EXPECT_FALSE(box.IsEmpty());
    EXPECT_FALSE(box.IsValid());

    // Mixed: partially sentinel values are still not the canonical
    // empty.  Only the exact FLT_MAX sentinel counts as empty.
    AABB partial(FloatVector4(FLT_MAX, 0.0f, FLT_MAX, 0.0f),
                 FloatVector4(-FLT_MAX, 1.0f, -FLT_MAX, 0.0f));
    EXPECT_FALSE(partial.IsEmpty());
    EXPECT_FALSE(partial.IsValid());
}

TEST(CanvasMathTest, AABBTransformIdentity)
{
    // Identity matrix leaves the AABB unchanged.
    AABB box(FloatVector4(-1.0f, -2.0f, -3.0f, 0.0f),
             FloatVector4( 1.0f,  2.0f,  3.0f, 0.0f));
    auto result = box.Transform(FloatMatrix4x4::Identity());
    EXPECT_TRUE(AlmostEqual(result.Min, box.Min));
    EXPECT_TRUE(AlmostEqual(result.Max, box.Max));
}

TEST(CanvasMathTest, AABBTransformTranslation)
{
    // Pure translation shifts both Min and Max equally.
    AABB box(FloatVector4(-1.0f, -1.0f, -1.0f, 0.0f),
             FloatVector4( 1.0f,  1.0f,  1.0f, 0.0f));
    FloatMatrix4x4 m = FloatMatrix4x4::Identity();
    m[3][0] = 10.0f;
    m[3][1] = 20.0f;
    m[3][2] = 30.0f;
    auto result = box.Transform(m);
    EXPECT_TRUE(AlmostEqual(result.Min, FloatVector4( 9.0f, 19.0f, 29.0f, 0.0f)));
    EXPECT_TRUE(AlmostEqual(result.Max, FloatVector4(11.0f, 21.0f, 31.0f, 0.0f)));
}

TEST(CanvasMathTest, AABBTransformRotation)
{
    // 90-degree rotation around Z (row-vector convention: row 0 ->
    // becomes +Y, row 1 -> becomes -X) of a non-square box.
    // After rotation:  X extent <- old Y extent, Y extent <- old X extent.
    AABB box(FloatVector4(-2.0f, -1.0f, -0.5f, 0.0f),
             FloatVector4( 2.0f,  1.0f,  0.5f, 0.0f));
    FloatMatrix4x4 m = FloatMatrix4x4::Identity();
    m[0][0] =  0.0f; m[0][1] =  1.0f;
    m[1][0] = -1.0f; m[1][1] =  0.0f;
    auto result = box.Transform(m);
    EXPECT_TRUE(AlmostEqual(result.Min, FloatVector4(-1.0f, -2.0f, -0.5f, 0.0f)));
    EXPECT_TRUE(AlmostEqual(result.Max, FloatVector4( 1.0f,  2.0f,  0.5f, 0.0f)));
}

TEST(CanvasMathTest, AABBTransformEmptyPropagates)
{
    // Transforming an empty AABB returns the same empty AABB.
    AABB box;
    EXPECT_TRUE(box.IsEmpty());
    auto result = box.Transform(FloatMatrix4x4::Identity());
    EXPECT_TRUE(result.IsEmpty());
}

TEST(CanvasMathTest, QuaternionNormalize)
{
    // Test that normalize works correctly
    FloatQuaternion q1(0.5f, 0.5f, 0.5f, 0.5f);
    auto qn = q1.Normalize();

    // Should be unit length
    float len = DotProduct(qn, qn);
    EXPECT_TRUE(AlmostZero(len - 1.0f));

    // Test already normalized quaternion
    auto qIdentity = IdentityQuaternion<float>();
    auto qIdentityNorm = qIdentity.Normalize();
    EXPECT_TRUE(AlmostEqual(qIdentity, qIdentityNorm));
}

TEST(CanvasMathTest, QuaternionConjugate)
{
    FloatQuaternion q(0.1f, 0.2f, 0.3f, 0.9f);
    auto qConj = Conjugate(q);

    EXPECT_TRUE(AlmostEqual(qConj, FloatQuaternion(-0.1f, -0.2f, -0.3f, 0.9f)));

    // Test that q * conjugate(q) = identity for unit quaternions
    auto qNorm = q.Normalize();
    auto qConjNorm = Conjugate(qNorm);
    auto result = qNorm * qConjNorm;
    EXPECT_TRUE(AlmostEqual(result, IdentityQuaternion<float>()));
}

TEST(CanvasMathTest, VectorMatrixMultiplication)
{
    // Test vector * matrix (row vector)
    FloatVector3 v(1.0f, 2.0f, 3.0f);
    FloatMatrix3x3 m({
        {1.0f, 0.0f, 0.0f},
        {0.0f, 2.0f, 0.0f},
        {0.0f, 0.0f, 3.0f}
    });

    auto result = v * m;
    EXPECT_TRUE(AlmostEqual(result, FloatVector3(1.0f, 4.0f, 9.0f)));

    // Test matrix * vector (column vector)
    auto result2 = m * v;
    EXPECT_TRUE(AlmostEqual(result2, FloatVector3(1.0f, 4.0f, 9.0f)));
}

TEST(CanvasMathTest, MatrixIdentity)
{
    // Test static Identity() method
    auto m2 = FloatMatrix2x2::Identity();
    EXPECT_EQ(1.0f, m2[0][0]);
    EXPECT_EQ(0.0f, m2[0][1]);
    EXPECT_EQ(0.0f, m2[1][0]);
    EXPECT_EQ(1.0f, m2[1][1]);

    auto m3 = FloatMatrix3x3::Identity();
    EXPECT_TRUE(AlmostEqual(m3, IdentityMatrix<float, 3, 3>()));

    auto m4 = FloatMatrix4x4::Identity();
    EXPECT_TRUE(AlmostEqual(m4, IdentityMatrix<float, 4, 4>()));
}

TEST(CanvasMathTest, PerspectiveReverseZBasic)
{
    // Test basic reverse-Z perspective matrix properties.
    // View space: +X=right, +Y=up, +Z=forward (D3D LHS, standard).
    // Row-vector layout: [x_v, y_v, z_v, 1] * M = [x_c, y_c, z_c, w_c].
    float fovY = static_cast<float>(g_PI / 4.0);  // 45 degrees
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    auto proj = PerspectiveReverseZ(fovY, aspect, nearPlane, farPlane);

    // Non-zero elements per the canonical D3D LHS perspective layout.
    EXPECT_TRUE(proj[0][0] != 0.0f);  // X(right)   -> X_clip scaling (f/aspect)
    EXPECT_TRUE(proj[1][1] != 0.0f);  // Y(up)      -> Y_clip scaling (f)
    EXPECT_TRUE(proj[2][2] != 0.0f);  // Z(forward) -> Z_clip depth
    EXPECT_TRUE(proj[2][3] == 1.0f);  // Z(forward) -> W_clip (perspective divide)

    // Verify f = cot(fovY/2)
    float f = 1.0f / std::tan(fovY * 0.5f);
    EXPECT_TRUE(std::abs(proj[0][0] - (f / aspect)) < FLT_EPSILON);
    EXPECT_TRUE(std::abs(proj[1][1] - f) < FLT_EPSILON);
}

TEST(CanvasMathTest, PerspectiveReverseZDepthMapping)
{
    // Test reverse-Z perspective matrix generates correct depth coefficients.
    // View space: +X=right, +Y=up, +Z=forward.
    float fovY = static_cast<float>(g_PI / 4.0);
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    auto proj = PerspectiveReverseZ(fovY, aspect, nearPlane, farPlane);

    // Row-vector: M[2][2] = A (forward -> z_clip), M[3][2] = B (depth bias).
    EXPECT_TRUE(proj[2][2] != 0.0f);
    EXPECT_TRUE(proj[3][2] != 0.0f);

    // A = nearPlane / (nearPlane - farPlane)
    // B = -(nearPlane * farPlane) / (nearPlane - farPlane)
    float rangeInv = 1.0f / (nearPlane - farPlane);
    EXPECT_TRUE(std::abs(proj[2][2] - nearPlane * rangeInv) < FLT_EPSILON);
    EXPECT_TRUE(std::abs(proj[3][2] - (-(nearPlane * farPlane * rangeInv))) < FLT_EPSILON);

    // Verify depth mapping: ndc_z = (A*z + B) / z where z = forward distance.
    // At near plane (z=near): ndc_z should be 1.0
    float A = proj[2][2];
    float B = proj[3][2];
    float ndc_near = (A * nearPlane + B) / nearPlane;
    EXPECT_TRUE(std::abs(ndc_near - 1.0f) < 1e-5f);
    // At far plane (z=far): ndc_z should be 0.0
    float ndc_far = (A * farPlane + B) / farPlane;
    EXPECT_TRUE(std::abs(ndc_far - 0.0f) < 1e-5f);
}

TEST(CanvasMathTest, PerspectiveReverseZCoordinateTransform)
{
    // Test coordinate system transformation: X_v(right)->X_clip,
    // Y_v(up)->Y_clip, Z_v(forward)->Z_clip + W_clip.
    float fovY = static_cast<float>(g_PI / 4.0);
    float aspect = 1.0f;
    float nearPlane = 1.0f;
    float farPlane = 10.0f;

    auto proj = PerspectiveReverseZ(fovY, aspect, nearPlane, farPlane);

    float f = 1.0f / std::tan(fovY * 0.5f);

    // Row-vector: M[0][0] = f/a (right -> x_clip), M[1][1] = f (up -> y_clip).
    EXPECT_TRUE(std::abs(proj[0][0] - f) < 1e-5f);  // aspect=1
    EXPECT_TRUE(std::abs(proj[1][1] - f) < 1e-5f);

    // Perspective divide: M[2][3] = 1 (forward -> w_clip), M[3][3] = 0.
    EXPECT_TRUE(std::abs(proj[2][3] - 1.0f) < 1e-5f);  // w_clip = z_view
    EXPECT_TRUE(std::abs(proj[3][3] - 0.0f) < 1e-5f);
}

TEST(CanvasMathTest, PerspectiveForwardZBasic)
{
    // Test basic forward-Z perspective matrix properties.
    // View space: +X=right, +Y=up, +Z=forward.
    float fovY = static_cast<float>(g_PI / 4.0);
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    auto proj = PerspectiveForwardZ(fovY, aspect, nearPlane, farPlane);

    // Row-vector: M[i][j] maps input axis i to output axis j.
    EXPECT_TRUE(proj[0][0] != 0.0f);  // X(right)   -> X_clip
    EXPECT_TRUE(proj[1][1] != 0.0f);  // Y(up)      -> Y_clip
    EXPECT_TRUE(proj[2][2] != 0.0f);  // Z(forward) -> Z_clip
    EXPECT_TRUE(proj[2][3] == 1.0f);  // Z(forward) -> W_clip

    // Verify f = cot(fovY/2)
    float f = 1.0f / std::tan(fovY * 0.5f);
    EXPECT_TRUE(std::abs(proj[0][0] - (f / aspect)) < FLT_EPSILON);
    EXPECT_TRUE(std::abs(proj[1][1] - f) < FLT_EPSILON);
}

TEST(CanvasMathTest, PerspectiveForwardZDepthMapping)
{
    // Test forward-Z perspective matrix generates correct depth coefficients.
    float fovY = static_cast<float>(g_PI / 4.0);
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    auto proj = PerspectiveForwardZ(fovY, aspect, nearPlane, farPlane);

    // Row-vector: M[2][2] = A (forward -> z_clip), M[3][2] = B (depth bias).
    EXPECT_TRUE(proj[2][2] != 0.0f);
    EXPECT_TRUE(proj[3][2] != 0.0f);

    // A = farPlane / (farPlane - nearPlane)
    // B = -(nearPlane * farPlane) / (farPlane - nearPlane)
    float rangeInv = 1.0f / (farPlane - nearPlane);
    EXPECT_TRUE(std::abs(proj[2][2] - farPlane * rangeInv) < FLT_EPSILON);
    EXPECT_TRUE(std::abs(proj[3][2] + nearPlane * farPlane * rangeInv) < FLT_EPSILON);

    // Verify depth mapping: ndc_z = (A*z + B) / z.
    // At near plane: ndc_z should be 0.0
    float A = proj[2][2];
    float B = proj[3][2];
    float ndc_near = (A * nearPlane + B) / nearPlane;
    EXPECT_TRUE(std::abs(ndc_near - 0.0f) < 1e-5f);
    // At far plane: ndc_z should be 1.0
    float ndc_far = (A * farPlane + B) / farPlane;
    EXPECT_TRUE(std::abs(ndc_far - 1.0f) < 1e-5f);
}

TEST(CanvasMathTest, PerspectiveForwardZCoordinateTransform)
{
    // Test coordinate system transformation for forward-Z.
    // X_v(right)->X_clip, Y_v(up)->Y_clip, Z_v(forward)->Z/W_clip.
    float fovY = static_cast<float>(g_PI / 4.0);
    float aspect = 1.0f;
    float nearPlane = 1.0f;
    float farPlane = 10.0f;

    auto proj = PerspectiveForwardZ(fovY, aspect, nearPlane, farPlane);

    float f = 1.0f / std::tan(fovY * 0.5f);

    // Row-vector: M[0][0] = f/a (right -> x_clip), M[1][1] = f (up -> y_clip).
    EXPECT_TRUE(std::abs(proj[0][0] - f) < 1e-5f);
    EXPECT_TRUE(std::abs(proj[1][1] - f) < 1e-5f);

    // Perspective divide: M[2][3] = 1 (forward -> w_clip).
    EXPECT_TRUE(std::abs(proj[2][3] - 1.0f) < 1e-5f);  // w_clip = z_view
    EXPECT_TRUE(std::abs(proj[3][3] - 0.0f) < 1e-5f);  // no constant contribution to w_clip
}

TEST(CanvasMathTest, PerspectiveReverseZVsForwardZDifference)
{
    // Verify both reverse-Z and forward-Z functions create valid projection matrices.
    // View space: +X=right, +Y=up, +Z=forward.
    float fovY = static_cast<float>(g_PI / 4.0);
    float aspect = 1.0f;
    float nearPlane = 1.0f;
    float farPlane = 10.0f;

    auto projRZ = PerspectiveReverseZ(fovY, aspect, nearPlane, farPlane);
    auto projFZ = PerspectiveForwardZ(fovY, aspect, nearPlane, farPlane);

    float f = 1.0f / std::tan(fovY * 0.5f);

    // Both should have identical FOV and perspective setup (row-vector layout).
    EXPECT_TRUE(std::abs(projRZ[0][0] - f) < 1e-5f);  // X(right) -> X_clip
    EXPECT_TRUE(std::abs(projRZ[1][1] - f) < 1e-5f);  // Y(up)    -> Y_clip
    EXPECT_TRUE(std::abs(projFZ[0][0] - f) < 1e-5f);
    EXPECT_TRUE(std::abs(projFZ[1][1] - f) < 1e-5f);

    // Verify perspective divide is correctly set for both.
    EXPECT_TRUE(std::abs(projRZ[2][3] - 1.0f) < 1e-5f);  // w_clip = z_view
    EXPECT_TRUE(std::abs(projFZ[2][3] - 1.0f) < 1e-5f);
    EXPECT_TRUE(std::abs(projRZ[3][3] - 0.0f) < 1e-5f);
    EXPECT_TRUE(projFZ[3][3] == 0.0f);

    // Depth coefficients differ: reverse-Z and forward-Z must produce
    // different A and B values (otherwise they are the same mapping).
    EXPECT_TRUE(std::abs(projRZ[2][2] - projFZ[2][2]) > 1e-5f);
    EXPECT_TRUE(std::abs(projRZ[3][2] - projFZ[3][2]) > 1e-5f);
}

TEST(CanvasMathTest, OrthoReverseZDepthMapping)
{
    // Verify that the reverse-Z ortho helper maps zNear -> 1 and
    // zFar -> 0 in clip space, matching the engine's reverse-Z
    // convention used by perspective projections and the
    // GREATER_EQUAL depth test.  View-space convention is LHS
    // (X=right, Y=up, Z=forward); row vectors throughout.
    const float halfW   = 100.0f;
    const float halfH   = 50.0f;
    const float zNear   = 1.0f;
    const float zFar    = 200.0f;

    auto proj = OrthoReverseZ(halfW, halfH, zNear, zFar);

    // Ortho has no perspective divide: row 3 column 3 is the
    // identity-affine 1, row 2 column 3 must be 0 so w_clip
    // equals 1 for every input.
    EXPECT_TRUE(std::abs(proj[3][3] - 1.0f) < FLT_EPSILON);
    EXPECT_TRUE(std::abs(proj[2][3] - 0.0f) < FLT_EPSILON);

    // XY scaling: 1/halfWidth, 1/halfHeight.
    EXPECT_TRUE(std::abs(proj[0][0] - (1.0f / halfW)) < FLT_EPSILON);
    EXPECT_TRUE(std::abs(proj[1][1] - (1.0f / halfH)) < FLT_EPSILON);

    // Pump a point at the near plane through the matrix and
    // verify ndc_z = 1.0.  At z=zNear: clip_z = -zNear/(zFar-zNear)
    // + zFar/(zFar-zNear) = (zFar - zNear) / (zFar - zNear) = 1.
    FloatVector4 ptNear(0.0f, 0.0f, zNear, 1.0f);
    FloatVector4 clipNear = ptNear * proj;
    EXPECT_TRUE(std::abs(clipNear.W - 1.0f) < 1e-5f);
    EXPECT_TRUE(std::abs(clipNear.Z / clipNear.W - 1.0f) < 1e-5f);

    // Same at the far plane: ndc_z = 0.
    FloatVector4 ptFar(0.0f, 0.0f, zFar, 1.0f);
    FloatVector4 clipFar = ptFar * proj;
    EXPECT_TRUE(std::abs(clipFar.W - 1.0f) < 1e-5f);
    EXPECT_TRUE(std::abs(clipFar.Z / clipFar.W - 0.0f) < 1e-5f);
}

TEST(CanvasMathTest, OrthoReverseZXYExtents)
{
    // Verify points at the +/- box extents map to +/- 1 in NDC.
    const float halfW = 20.0f;
    const float halfH = 10.0f;
    const float zNear = 0.5f;
    const float zFar  = 100.0f;

    auto proj = OrthoReverseZ(halfW, halfH, zNear, zFar);

    // +halfWidth on x -> +1 in NDC x.
    FloatVector4 pX(halfW, 0.0f, zNear, 1.0f);
    FloatVector4 cX = pX * proj;
    EXPECT_TRUE(std::abs(cX.X - 1.0f) < 1e-5f);
    EXPECT_TRUE(std::abs(cX.Y - 0.0f) < 1e-5f);

    // -halfHeight on y -> -1 in NDC y.
    FloatVector4 pY(0.0f, -halfH, zFar, 1.0f);
    FloatVector4 cY = pY * proj;
    EXPECT_TRUE(std::abs(cY.X - 0.0f) < 1e-5f);
    EXPECT_TRUE(std::abs(cY.Y - (-1.0f)) < 1e-5f);
}

// ----------------------------------------------------------------
// Frustum extraction + AABB intersection tests.
//
// Camera view space: LHS, +X right, +Y up, +Z forward.  Tests use
// view = identity so the projection matrix alone defines the
// frustum, letting us reason about points/AABBs directly in view
// coordinates without an extra matrix layer to debug.
// ----------------------------------------------------------------

// Reverse-Z perspective is Canvas's default.  These tests pin the
// sign convention of FromViewProjection: points truly inside the
// view frustum must produce IntersectsAABB == true; points outside
// any single plane must produce false.
TEST(CanvasMathTest, FrustumReverseZPerspectiveInsideOutside)
{
    const float fovY = float(g_PI * 0.5);  // 90 deg
    const float aspect = 1.0f;
    const float nearZ = 1.0f;
    const float farZ  = 100.0f;
    FloatMatrix4x4 proj = PerspectiveReverseZ<float>(fovY, aspect, nearZ, farZ);
    Frustum f = Frustum::FromViewProjection(proj, /*reverseZ*/ true);

    auto pointAABB = [](float x, float y, float z) {
        FloatVector4 p(x, y, z, 0.0f);
        return AABB(p, p);
    };

    // Squarely inside.
    EXPECT_TRUE(f.IntersectsAABB(pointAABB(0.0f, 0.0f, 10.0f)));

    // Behind the near plane (z < nearZ): outside near.
    EXPECT_FALSE(f.IntersectsAABB(pointAABB(0.0f, 0.0f, 0.5f)));

    // Behind the camera entirely.
    EXPECT_FALSE(f.IntersectsAABB(pointAABB(0.0f, 0.0f, -10.0f)));

    // Beyond the far plane.
    EXPECT_FALSE(f.IntersectsAABB(pointAABB(0.0f, 0.0f, 200.0f)));

    // Off to the right past the 90-deg side plane (x > z at z=10).
    EXPECT_FALSE(f.IntersectsAABB(pointAABB(50.0f, 0.0f, 10.0f)));

    // Above the top plane (y > z at z=10 for 90deg fov).
    EXPECT_FALSE(f.IntersectsAABB(pointAABB(0.0f, 50.0f, 10.0f)));

    // AABB straddling the near plane: must overlap.
    AABB straddleNear(FloatVector4(-0.5f, -0.5f, 0.5f, 0.0f),
                      FloatVector4( 0.5f,  0.5f, 2.0f, 0.0f));
    EXPECT_TRUE(f.IntersectsAABB(straddleNear));

    // Large AABB enclosing the camera: positive-vertex test is
    // conservative and accepts this (every plane has SOME corner
    // on the inside).  Documents the known false-positive regime.
    AABB enclosing(FloatVector4(-1000.0f, -1000.0f, -1000.0f, 0.0f),
                   FloatVector4( 1000.0f,  1000.0f,  1000.0f, 0.0f));
    EXPECT_TRUE(f.IntersectsAABB(enclosing));

    // Empty AABB: never intersects.
    EXPECT_FALSE(f.IntersectsAABB(AABB{}));
}

// Forward-Z swap: only Near/Far plane assignment differs.  Same
// points should yield the same inside/outside verdicts because
// both extractions describe the same world-space frustum.
TEST(CanvasMathTest, FrustumForwardZPerspectiveAgreesWithReverseZ)
{
    const float fovY = float(g_PI * 0.5);
    const float aspect = 16.0f / 9.0f;
    const float nearZ = 0.5f;
    const float farZ  = 50.0f;

    Frustum fRev = Frustum::FromViewProjection(
        PerspectiveReverseZ<float>(fovY, aspect, nearZ, farZ), true);
    Frustum fFwd = Frustum::FromViewProjection(
        PerspectiveForwardZ<float>(fovY, aspect, nearZ, farZ), false);

    const FloatVector4 inside (0.0f, 0.0f, 10.0f, 0.0f);
    const FloatVector4 tooNear(0.0f, 0.0f,  0.1f, 0.0f);
    const FloatVector4 tooFar (0.0f, 0.0f, 99.0f, 0.0f);

    AABB bIn (inside,  inside);
    AABB bN  (tooNear, tooNear);
    AABB bF  (tooFar,  tooFar);

    EXPECT_EQ(fRev.IntersectsAABB(bIn), fFwd.IntersectsAABB(bIn));
    EXPECT_EQ(fRev.IntersectsAABB(bN), fFwd.IntersectsAABB(bN));
    EXPECT_EQ(fRev.IntersectsAABB(bF), fFwd.IntersectsAABB(bF));

    EXPECT_TRUE(fRev.IntersectsAABB(bIn));
    EXPECT_FALSE(fRev.IntersectsAABB(bN));
    EXPECT_FALSE(fRev.IntersectsAABB(bF));
}

}