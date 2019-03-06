//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

#include <math.h>
#include <float.h>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
struct alignas(16) TVector
{
    static auto constexpr Dimension = _Dim;
    using ElementType = _Type;

    _Type V[Dimension] = {};

    TVector() = default;
    TVector(const TVector &o) = default;
    TVector &operator=(const TVector &o) = default;
    const _Type &operator[](int index) const { return V[index]; }
    _Type &operator[](int index) { return V[index]; }

    // Unary operators
    TVector operator-()
    {
        TVector result;
        for (unsigned int index = 0; index < _Dim; ++index)
        {
            result[index] = -V[index];
        }

        return result;
    }
};

//------------------------------------------------------------------------------------------------
template<class _Type>
struct alignas(16) TVector<_Type, 2U>
{
    static auto constexpr Dimension = 2U;
    using ElementType = _Type;

    _Type V[Dimension] = {};

    TVector() = default;
    TVector(const TVector &o) = default;
    TVector(_Type x, _Type y) :
        V{ x, y } {}
    TVector &operator=(const TVector &o) = default;

    const _Type &operator[](int index) const { return V[index]; }
    _Type &operator[](int index) { return V[index]; }
    const _Type &X() const { return V[0]; }
    const _Type &Y() const { return V[1]; }

    // Unary operators
    TVector operator-()
    {
        return TVector(-V[0], -V[1]);
    }
};

//------------------------------------------------------------------------------------------------
template<class _Type>
struct alignas(16) TVector<_Type, 3U>
{
    static auto constexpr Dimension = 3U;
    using ElementType = _Type;

    _Type V[Dimension] = {};

    TVector() = default;
    TVector(const TVector &o) = default;
    TVector(_Type x, _Type y, _Type z) :
        V{ x, y, z } {}
    TVector &operator=(const TVector &o) = default;

    const _Type &operator[](int index) const { return V[index]; }
    _Type &operator[](int index) { return V[index]; }
    const _Type &X() const { return V[0]; }
    const _Type &Y() const { return V[1]; }
    const _Type &Z() const { return V[2]; }

    // Unary operators
    TVector operator-()
    {
        return TVector(-V[0], -V[1], -V[2]);
    }
};

//------------------------------------------------------------------------------------------------
template<class _Type>
struct alignas(16) TVector<_Type, 4U>
{
    static auto constexpr Dimension = 4U;
    using ElementType = _Type;

    _Type V[Dimension] = {};

    TVector() = default;
    TVector(const TVector &o) = default;
    TVector(_Type x, _Type y, _Type z, _Type w) :
        V{ x, y, z, w } {}
    TVector &operator=(const TVector &o) = default;

    const _Type &operator[](int index) const { return V[index]; }
    _Type &operator[](int index) { return V[index]; }
    const _Type &X() const { return V[0]; }
    const _Type &Y() const { return V[1]; }
    const _Type &Z() const { return V[2]; }
    const _Type &W() const { return V[3]; }

    // Unary operators
    TVector operator-()
    {
        return TVector(-V[0], -V[1], -V[2], -V[3]);
    }
};

using UIntVector2 = TVector<unsigned int, 2>;
using UIntVector3 = TVector<unsigned int, 3>;
using UIntVector4 = TVector<unsigned int, 4>;

using IntVector2 = TVector<int, 2>;
using IntVector3 = TVector<int, 3>;
using IntVector4 = TVector<int, 4>;

using FloatVector2 = TVector<float, 2U>;
using FloatVector3 = TVector<float, 3U>;
using FloatVector4 = TVector<float, 4U>;

using DoubleVector2 = TVector<double, 2U>;
using DoubleVector3 = TVector<double, 3U>;
using DoubleVector4 = TVector<double, 4U>;

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
bool operator==(const TVector<_Type, _Dim> &v0, const TVector<_Type, _Dim> &v1)
{
    for (unsigned int i = 0; i < _Dim; ++i)
    {
        if (v0[i] != v1[i]) return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
bool operator!=(const TVector<_Type, _Dim> &v0, const TVector<_Type, _Dim> &v1)
{
    return !operator==(v0, v1);
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
TVector<_Type, _Dim> operator+(const TVector<_Type, _Dim> &v0, const TVector<_Type, _Dim> &v1)
{
    TVector<_Type, _Dim> result;

    for (unsigned int i = 0; i < _Dim; ++i)
    {
        result[i] = v0[i] + v1[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
TVector<_Type, _Dim> operator-(const TVector<_Type, _Dim> &v0, const TVector<_Type, _Dim> &v1)
{
    TVector<_Type, _Dim> result;

    for (unsigned int i = 0; i < _Dim; ++i)
    {
        result[i] = v0[i] - v1[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
TVector<_Type, _Dim> operator*(const TVector<_Type, _Dim> &v0, const TVector<_Type, _Dim> &v1)
{
    TVector<_Type, _Dim> result;

    for (unsigned int i = 0; i < _Dim; ++i)
    {
        result[i] = v0[i] * v1[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
TVector<_Type, _Dim> operator/(const TVector<_Type, _Dim> &v0, const TVector<_Type, _Dim> &v1)
{
    TVector<_Type, _Dim> result;

    for (unsigned int i = 0; i < _Dim; ++i)
    {
        result[i] = v0[i] / v1[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
_Type DotProduct(const TVector<_Type, _Dim> &v0, const TVector<_Type, _Dim> &v1)
{
    _Type result = 0;

    for (unsigned int i = 0; i < _Dim; ++i)
    {
        result += v0[i] * v1[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type>
TVector<_Type, 3U> CrossProduct(const TVector<_Type, 3U> &v0, const TVector<_Type, 3U> &v1)
{
    return TVector<_Type, 3U>(
        v0[1] * v1[2] - v0[2] * v1[1],
        v0[2] * v1[0] - v0[0] * v1[2],
        v0[0] * v1[1] - v0[1] * v1[0]
    );
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
TVector<_Type, _Dim> operator*(const TVector<_Type, _Dim> &v, _Type mul)
{
    TVector<_Type, _Dim> result;

    for (unsigned int i = 0; i < _Dim; ++i)
    {
        result[i] = v[i] * mul;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
TVector<_Type, _Dim> operator*(_Type mul, const TVector<_Type, _Dim> &v)
{
    return operator*(v, mul);
}

//------------------------------------------------------------------------------------------------
// Caller is responsible for ensuring the input vector does not have a
// zero magnitude
template<unsigned int _Dim>
TVector<float, _Dim> NormalizeVector(const TVector<float, _Dim> &v)
{
    float magsq = DotProduct(v, v);
    float recipmag = 1.f / sqrtf(magsq);
    TVector<float, _Dim> result;
    for (unsigned int i = 0; i < _Dim; ++i)
    {
        result[i] = v[i] * recipmag;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
// Caller is responsible for ensuring the input vector does not have a
// zero magnitude
template<unsigned int _Dim>
TVector<double, _Dim> NormalizeVector(const TVector<double, _Dim> &v)
{
    double magsq = DotProduct(v, v);
    double recipmag = 1.f / sqrtl(magsq);
    TVector<double, _Dim> result;
    for (unsigned int i = 0; i < _Dim; ++i)
    {
        result[i] = v[i] * recipmag;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Rows, unsigned int _Columns>
struct alignas(16) TMatrix
{
    static auto constexpr Rows = _Rows;
    static auto constexpr Columns = _Columns;
    using RowType = TVector<_Type, Columns>;
    using ElementType = _Type;

    RowType M[Rows] = {};

    TMatrix(const TMatrix &o) = default;
    TMatrix &operator=(const TMatrix &o) = default;
    const RowType &operator[](int index) const { return M[index]; }
    RowType &operator[](int index) { return M[index]; }
};

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Columns>
struct alignas(16) TMatrix<_Type, 2U, _Columns>
{
    static auto constexpr Rows = 2U;
    static auto constexpr Columns = _Columns;
    using RowType = TVector<_Type, Columns>;
    using ElementType = _Type;
    RowType M[Rows] = {};

    TMatrix() = default;
    TMatrix(
        const RowType &Row0,
        const RowType &Row1) :
        M{
        Row0,
        Row1
    } {}

    const RowType &operator[](int index) const { return M[index]; }
    RowType &operator[](int index) { return M[index]; }
};

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Columns>
struct alignas(16) TMatrix<_Type, 3U, _Columns>
{
    static auto constexpr Rows = 3U;
    static auto constexpr Columns = _Columns;
    using RowType = TVector<_Type, Columns>;
    using ElementType = _Type;
    RowType M[Rows] = {};

    TMatrix() = default;
    TMatrix(
        const RowType &Row0,
        const RowType &Row1,
        const RowType &Row2) :
        M{
        Row0,
        Row1,
        Row2
    } {}

    const RowType &operator[](int index) const { return M[index]; }
    RowType &operator[](int index) { return M[index]; }
};

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Columns>
struct alignas(16) TMatrix<_Type, 4U, _Columns>
{
    static auto constexpr Rows = 4U;
    static auto constexpr Columns = _Columns;
    using RowType = TVector<_Type, Columns>;
    using ElementType = _Type;
    RowType M[Rows] = {};

    TMatrix() = default;
    TMatrix(
        const RowType &Row0,
        const RowType &Row1,
        const RowType &Row2,
        const RowType &Row3) :
        M{
        Row0,
        Row1,
        Row2,
        Row3
    } {}

    const RowType &operator[](int index) const { return M[index]; }
    RowType &operator[](int index) { return M[index]; }
};

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Rows, unsigned int _Columns>
TVector<_Type, _Columns> operator*(const TVector<_Type, _Rows> &v, const TMatrix<_Type, _Rows, _Columns> &m)
{
    TVector<_Type, _Columns> result;

    for (unsigned int Col = 0; Col < _Columns; ++Col)
    {
        for (unsigned int Row = 0; Row < _Rows; ++Row)
        {
            result[Col] += v[Row] * m[Row][Col];
        }
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Rows0, unsigned int _Columns0, unsigned int _Columns1>
TMatrix<_Type, _Rows0, _Columns1> operator*(const TMatrix<_Type, _Rows0, _Columns0> &m0, const TMatrix<_Type, _Columns0, _Columns1> &m1)
{
    TMatrix<_Type, _Rows0, _Columns1> result;

    for (unsigned int Row = 0; Row < _Rows0; ++Row)
    {
        result[Row] = m0[Row] * m1;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Rows, unsigned int _Columns>
TMatrix<_Type, _Rows, _Columns> operator*(const _Type &scale, const TMatrix<_Type, _Rows, _Columns> &m)
{
    TMatrix<_Type, _Rows, _Columns> result;

    for (unsigned int Row = 0; Row < _Rows; ++Row)
    {
        result[Row] = m[Row] * scale;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Rows, unsigned int _Columns>
TMatrix<_Type, _Rows, _Columns> operator*(const TMatrix<_Type, _Rows, _Columns> &m, const _Type &scale)
{
    TMatrix<_Type, _Rows, _Columns> result;

    for (unsigned int Row = 0; Row < _Rows; ++Row)
    {
        result[Row] = m[Row] * scale;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Rows, unsigned int _Columns>
bool operator==(TMatrix<_Type, _Rows, _Columns> m0, TMatrix<_Type, _Rows, _Columns> m1)
{
    for (unsigned int Row = 0; Row < _Rows; ++Row)
    {
        if (m0[Row] != m1[Row]) return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Rows, unsigned int _Columns>
bool operator!=(TMatrix<_Type, _Rows, _Columns> m0, TMatrix<_Type, _Rows, _Columns> m1)
{
    return !operator==(m0, m1);
}

template<class _Type, unsigned int _Rows, unsigned int _Columns>
TMatrix<_Type, _Rows, _Columns> IdentityMatrix()
{
    TMatrix<_Type, _Rows, _Columns> m = {};
    for (unsigned int i = 0; i < (_Rows < _Columns ? _Rows : _Columns); ++i)
    {
        m[i][i] = 1;
    }

    return m;
}

//------------------------------------------------------------------------------------------------
// Sets the upper-left 3x3 as a rotation about the x-axis
// with the given Euler angle
template<class _Type>
TMatrix<_Type, 3, 3> XRotationMatrix(_Type angle)
{
    auto m = IdentityMatrix<_Type, 3, 3>();

    alignas(16) _Type c = cos(angle);
    alignas(16) _Type s = sin(angle);
    m[1][1] = c;
    m[1][2] = -s;
    
    m[2][1] = s;
    m[2][2] = c;

    return m;
}

//------------------------------------------------------------------------------------------------
// Sets the upper-left 3x3 as a rotation about the x-axis
// with the given Euler angle
template<class _Type>
TMatrix<_Type, 3, 3> YRotationMatrix(_Type angle)
{
    auto m = IdentityMatrix<_Type, 3, 3>();

    alignas(16) _Type c = cos(angle);
    alignas(16) _Type s = sin(angle);
    m[0][0] = c;
    m[0][2] = s;

    m[2][0] = -s;
    m[2][2] = c;

    return m;
}

//------------------------------------------------------------------------------------------------
// Sets the upper-left 3x3 as a rotation about the x-axis
// with the given Euler angle
template<class _Type>
TMatrix<_Type, 3, 3> ZRotationMatrix(_Type angle)
{
    auto m = IdentityMatrix<_Type, 3, 3>();

    alignas(16) _Type c = cos(angle);
    alignas(16) _Type s = sin(angle);
    m[0][0] = c;
    m[0][1] = -s;
    
    m[1][0] = s;
    m[1][1] = c;

    return m;
}

//------------------------------------------------------------------------------------------------
// Transposes all or part of a matrix.
// Returns a copy of the given matrix with the specified square ranges transposed.
template<class _MatrixType>
_MatrixType MatrixTransposeRows(const _MatrixType &m, unsigned int Dim = _MatrixType::Rows, unsigned int OffRow = 0, unsigned int OffCol = 0)
{
    _MatrixType n = m;
    for (unsigned int i = 0; i < Dim; ++i)
    {
        for (unsigned int j = 0; j < Dim; ++j)
        {
            n[j + OffRow][i + OffCol] = m[i + OffRow][j + OffCol];
        }
    }
    return n;
}

//------------------------------------------------------------------------------------------------
using FloatMatrix2x2 = TMatrix<float, 2U, 2U>;
using FloatMatrix3x3 = TMatrix<float, 3U, 3U>;
using FloatMatrix4x4 = TMatrix<float, 4U, 4U>;
using DoubleMatrix2x2 = TMatrix<double, 2U, 2U>;
using DoubleMatrix3x3 = TMatrix<double, 3U, 3U>;
using DoubleMatrix4x4 = TMatrix<double, 4U, 4U>;

//------------------------------------------------------------------------------------------------
// Represents a Unit Quaternion representing a rotation in 3-dimensional space
// Quaternion math from http://www.gamasutra.com/view/feature/131686/rotating_objects_using_quaternions.php
// 
// Addition: q + q´ = [w + w´, v + v´] 
//
// Multiplication: qq´ = [ww´ - v . v´, v x v´ + wv´ + w´v] (. is vector dot product and x is vector cross product); Note: qq´ ? q´q 
// Conjugate: q* = [w, -v] 
//
// Inverse: q-1 = q* / N(q) 
//
// Unit Quaternion: q is a unit quaternion if N(q)= 1 and then q-1 = q*
//
// Identity: [1, (0, 0, 0)] (when involving multiplication) and [0, (0, 0, 0)] (when involving addition) 
template<class _Type>
struct alignas(16) TQuaternion
{
    _Type W; // Scalar term
    TVector<_Type, 3> V; // Vector term

    TQuaternion() = default;
    TQuaternion(_Type real) :
        W(real),
        V{ 0, 0, 0 } {}
    TQuaternion(const TVector<_Type, 3> v) :
        W(0),
        V(v) {}
    TQuaternion(const _Type &w, const TVector<_Type, 3> &v) :
        W(w),
        V(v) {}
    TQuaternion(_Type w, _Type a, _Type b, _Type c) :
        W(w),
        V{ a, b, c,} {}

    TQuaternion(const TQuaternion &o) = default;

    TQuaternion &operator=(const TQuaternion &o) = default;

    bool operator==(const TQuaternion &o)
    {
        return W == o.W && V == o.V;
    }

    TQuaternion operator*(const _Type &scale)
    {
        return TQuaternion(scale * W, scale * V[0], scale * V[1], scale * V[2]);
    }

    void ReNormalize();
    TQuaternion Conjugate();
};

//------------------------------------------------------------------------------------------------
template<class _Type>
TQuaternion<_Type> IdentityQuaternion()
{
    return TQuaternion<_Type>(1., 0., 0., 0.);
}

//------------------------------------------------------------------------------------------------
template<class _Type>
_Type DotProduct(const TQuaternion<_Type> &q, const TQuaternion<_Type> &r)
{
    return q.W * r.W + DotProduct(q.V, r.V);
}

//------------------------------------------------------------------------------------------------
// Should be a unit quaternion but may need to be renormalized to correct
// for accumulated floating point errors
template<class _Type>
void TQuaternion<_Type>::ReNormalize()
{
    _Type dot = DotProduct(*this, *this);
    _Type rsq = 1. / sqrt(dot);
    W = W * rsq;
    V = V * rsq;
}

//------------------------------------------------------------------------------------------------
// Returns the conjugate quaternion.
// Note: Since this is expected to be a unit quaternion, the conjugate is the also
// the inverse.
template<class _Type>
TQuaternion<_Type> TQuaternion<_Type>::Conjugate()
{
    return TQuaternion(W, -V);
}

//------------------------------------------------------------------------------------------------
// Returns the product of two unit quaternions.
// See https://en.wikipedia.org/wiki/Quaternion#Hamilton_product
template<class _Type>
TQuaternion<_Type> operator*(const TQuaternion<_Type> q, const TQuaternion<_Type> &r)
{
    _Type w = q.W * r.W - DotProduct(q.V, r.V);
    TVector<_Type, 3> v = CrossProduct(q.V, r.V) + q.W *r.V + r.W * q.V;
    return TQuaternion<_Type>(w, v);
    //_Type x = q.W * r.V[0] + q.V[0] * r.W + q.V[1] * r.V[2] - q.V[2] * r.V[1];
    //_Type y = q.W * r.V[1] - q.V[0] * r.V[2] + q.V[1] * r.W + q.V[2] * r.V[0];
    //_Type z = q.W * r.V[2] + q.V[0] * r.V[1] - q.V[1] * r.V[0] + q.V[2] * r.W;
    //return TQuaternion<_Type>(w, x, y, z);
}

//------------------------------------------------------------------------------------------------
// Returns the product of a quaternion with a vector (treated as a quaternion with a 0 real coordinate)
template<class _Type>
TQuaternion<_Type> operator*(const TQuaternion<_Type> q, const TVector<_Type, 3> &v)
{
    return q * TQuaternion<_Type>(v);

}

//------------------------------------------------------------------------------------------------
// Returns the sum of two quaternions
template<class _Type>
TQuaternion<_Type> operator+(const TQuaternion<_Type> q, const TQuaternion<_Type> &r)
{
    return TQuaternion<_Type>(q.W + r.W, q.V + r.V);
}

//------------------------------------------------------------------------------------------------
// Returns the sum of two quaternions
template<class _Type>
TQuaternion<_Type> operator-(const TQuaternion<_Type> q, const TQuaternion<_Type> &r)
{
    return TQuaternion<_Type>(q.W - r.W, q.V - r.V);

}

//------------------------------------------------------------------------------------------------
// Performs a Spherical-linear interpolation (SLERP) between two Quaternions and
// returns the resulting Quaternion
template<class _Type>
TQuaternion<_Type> QuaternionSlerp(const TQuaternion<_Type> &q, const TQuaternion<_Type> &r, _Type t)
{
    // Assumes unit quaternions

    _Type dot = DotProduct(q, r);
    if (dot < 0)
    {
        q = -q;
        dot = -dot;
    }
}

//------------------------------------------------------------------------------------------------
// Converts the unit quaternion to a 3x3 rotation matrix
template<class _Type>
TMatrix<_Type, 3, 3> QuaternionToRotationMatrix(const TQuaternion<_Type> &q)
{
    return TMatrix<_Type, 3, 3>(
    {
        {
            1. - 2. * (q.V[1] * q.V[1] + q.V[2] * q.V[2]),
            2. * (q.V[0] * q.V[1] - q.V[2] * q.W),
            2. * (q.V[0] * q.V[2] + q.V[1] * q.W)
        },
        {
            2. * (q.V[0] * q.V[1] + q.V[2] * q.W),
            1. - 2. * (q.V[0] * q.V[0] + q.V[2] * q.V[2]),
            2. * (q.V[1] * q.V[2] - q.V[0] * q.W)
        },
        {
            2. * (q.V[0] * q.V[2] - q.V[1] * q.W),
            2. * (q.V[1] * q.V[2] + q.V[0] * q.W),
            1. - 2. * (q.V[0] * q.V[0] + q.V[1] * q.V[1])
        },
    });
}

//------------------------------------------------------------------------------------------------
// Creates a quaternion from the provided angle (in radians) and axis (assumed to be a unit vector)
template<class _Type>
TQuaternion<_Type> QuaternionFromAngleAxis(const _Type &angle, const TVector<_Type, 3> &axis)
{
    _Type HalfAngle = angle / 2;
    _Type c = cos(HalfAngle);
    _Type s = sin(HalfAngle);
    return TQuaternion<_Type>(c, s * axis);
}

//------------------------------------------------------------------------------------------------
// Creates a quaternion from a given matrix
// See http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
template<class _Type>
TQuaternion<_Type> QuaternionFromRotationMatrix(const TMatrix<_Type, 3, 3> &m)
{
    TQuaternion<_Type> Q;
    _Type T = m[0][0] + m[1][1] + m[2][2];

    if (T > 0)
    {
        _Type S = 2 * sqrt(T + 1);
        _Type Sr = 1. / S;
        Q.W    = .25 * S;
        Q.V[0] = (m[2][1] - m[1][2]) * Sr;
        Q.V[1] = (m[0][2] - m[2][0]) * Sr;
        Q.V[2] = (m[1][0] - m[0][1]) * Sr;
    }
    else if(m[0][0] > m[1][1] && (m[0][0] > m[2][2]))
    {
        T = m[0][0] - m[1][1] - m[2][2];
        _Type S = 2 * sqrt(T + 1);
        _Type Sr = 1. / S;
        Q.W    = (m[2][1] - m[1][2]) * Sr;
        Q.V[0] = .25 * S;
        Q.V[1] = (m[0][1] + m[1][0]) * Sr;
        Q.V[2] = (m[0][2] + m[2][0]) * Sr;
    }
    else if(m[1][1] > m[2][2])
    {
        T = m[1][1] - m[0][0] - m[2][2];
        _Type S = 2 * sqrt(T + 1);
        _Type Sr = 1. / S;
        Q.W    = (m[0][2] - m[2][0]) * Sr;
        Q.V[0] = (m[0][1] + m[1][0]) * Sr;
        Q.V[1] = .25 * S;
        Q.V[2] = (m[1][2] + m[2][1]) * Sr;
    }
    else
    {
        T = m[2][2] - m[0][0] - m[1][1];
        _Type S = 2 * sqrt(T + 1);
        _Type Sr = 1. / S;
        Q.W    = (m[1][0] - m[0][1]) * Sr;
        Q.V[0] = (m[0][2] + m[2][0]) * Sr;
        Q.V[1] = (m[1][2] + m[2][1]) * Sr;
        Q.V[2] = .25 * S;
    }

    return Q;
}

template<class _Type>
struct MinNorm
{
};

template<>
struct MinNorm<float>
{
    inline static const float Value = FLT_MIN;
};

template<>
struct MinNorm<double>
{
    inline static const double Value = DBL_MIN;
};

//------------------------------------------------------------------------------------------------
// Calculates an OutVector and an UpVector given a unit UpAxisVector and a unit LookVector.
// Can be used to initialize the orthonormal basis of a "look at" rotation matrix.
// Note, the LookVector may be a Forward vector or a Backward vector, depending
// on the desired direction of the OutVector.
template<class _Type>
void ComposeLookBasisVectors(_In_ const TVector<_Type, 3> &UpAxisVector, _In_ const TVector<_Type, 3> &LookVector, _Out_ TVector<_Type, 3> &OutVector, _Out_ TVector<_Type, 3> &UpVector)
{
    // OutVector is the cross product of UpAxisVector with LookVector
    OutVector = CrossProduct(UpAxisVector, LookVector);
    _Type dot = DotProduct(OutVector, OutVector);
    _Type dotsq = dot * dot;
    if (dotsq < MinNorm<_Type>::Value)
    {
        // LookVector and UpAxisVector appear to be colinear
        // Choose an arbitrary OutVector preserving the sign
        // of the camera up vector
        _Type d2 = DotProduct(LookVector, UpAxisVector);
        if (d2 > 0)
        {
            OutVector = TVector<_Type, 3>(UpAxisVector.V[2], UpAxisVector.V[0], UpAxisVector.V[1]);
        }
        else
        {
            OutVector = TVector<_Type, 3>(-UpAxisVector.V[2], UpAxisVector.V[0], -UpAxisVector.V[1]);
        }
    }
    else
    {
        // Normalize OutVector
        OutVector = OutVector * _Type(1. / sqrt(dotsq));
    }

    // The cross product of OutVector with LookVector is the UpVector.
    // There is no need to normalize the result since the
    // LookVector and UpVector are unit vectors and are 
    // orthogonal. The result must already be a unit vector.
    UpVector = CrossProduct(LookVector, OutVector);
}

using FloatQuaternion = TQuaternion<float>;
using DoubleQuaternion = TQuaternion<double>;


}
