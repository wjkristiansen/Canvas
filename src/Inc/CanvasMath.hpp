//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

namespace Canvas
{

//------------------------------------------------------------------------------------------------
template<class _Type, unsigned int _Dim>
struct TVector
{
    static auto constexpr Dimension = _Dim;
    using ElementType = _Type;

    _Type V[Dimension] = {};

    TVector() = default;
    TVector(const TVector &o) = default;
    TVector &operator=(const TVector &o) = default;
    const _Type &operator[](int index) const { return V[index]; }
    _Type &operator[](int index) { return V[index]; }

    TVector &operator-()
    {
        for (unsigned int index = 0; index < _Dim; ++index)
        {
            V[index] = -V[index];
        }

        return *this;
    }
};

//------------------------------------------------------------------------------------------------
template<class _Type>
struct TVector<_Type, 2U>
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
    TVector &operator-()
    {
        V[0] = -V[0];
        V[1] = -V[1];
        return *this;
    }
};

//------------------------------------------------------------------------------------------------
template<class _Type>
struct TVector<_Type, 3U>
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
    TVector &operator-()
    {
        V[0] = -V[0];
        V[1] = -V[1];
        V[2] = -V[2];
        return *this;
    }
};

//------------------------------------------------------------------------------------------------
template<class _Type>
struct TVector<_Type, 4U>
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
    TVector &operator-()
    {
        V[0] = -V[0];
        V[1] = -V[1];
        V[2] = -V[2];
        V[3] = -V[3];
        return *this;
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
struct TMatrix
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
struct TMatrix<_Type, 2U, _Columns>
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
struct TMatrix<_Type, 3U, _Columns>
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
struct TMatrix<_Type, 4U, _Columns>
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
template<class _Type, unsigned int _Rows, unsigned int _Columns>
TMatrix<_Type, _Rows, _Columns> XRotMatrix(_Type rot)
{
    TMatrix<_Type, _Rows, _Columns> m = IdentityMatrix<_Type, _Rows, _Columns>();

    _Type c = cos(rot);
    _Type s = sin(rot);
    m[1][1] = c;
    m[1][2] = -s;
    
    m[2][1] = s;
    m[2][2] = c;

    return m;
}

//------------------------------------------------------------------------------------------------
// Sets the upper-left 3x3 as a rotation about the x-axis
template<class _Type, unsigned int _Rows, unsigned int _Columns>
TMatrix<_Type, _Rows, _Columns> YRotMatrix(_Type rot)
{
    TMatrix<_Type, _Rows, _Columns> m = IdentityMatrix<_Type, _Rows, _Columns>();

    _Type c = cos(rot);
    _Type s = sin(rot);
    m[0][0] = c;
    m[0][2] = s;

    m[2][0] = -s;
    m[2][2] = c;

    return m;
}

//------------------------------------------------------------------------------------------------
// Sets the upper-left 3x3 as a rotation about the x-axis
template<class _Type, unsigned int _Rows, unsigned int _Columns>
TMatrix<_Type, _Rows, _Columns> ZRotMatrix(_Type rot)
{
    TMatrix<_Type, _Rows, _Columns> m = IdentityMatrix<_Type, _Rows, _Columns>();

    _Type c = cos(rot);
    _Type s = sin(rot);
    m[0][0] = c;
    m[0][1] = -s;
    
    m[1][0] = s;
    m[1][1] = c;

    return m;
}

//------------------------------------------------------------------------------------------------
// Transposes a square _SquareDim x _SquareDim matrix
template <class _ElementType, unsigned int _SquareDim>
TMatrix<_ElementType, _SquareDim, _SquareDim> TransposeMatrix(const TMatrix<_ElementType, _SquareDim, _SquareDim> &m)
{
    using MatrixType = TMatrix<_ElementType, _SquareDim, _SquareDim>;
    MatrixType t;

    for (unsigned int i = 0; i < _SquareDim; ++i)
    {
        for (unsigned int j = 0; j < _SquareDim; ++j)
        {
            t[i][j] = m[j][i];
        }
    }

    return t;
}

//------------------------------------------------------------------------------------------------
template<class _Type>
TMatrix<_Type, 4, 4> InvertAffineTransform4x4(const TMatrix<_Type, 4, 4> &m)
{
    TMatrix<_Type, 4, 4> invm;

    // Set the last column to 0, 0, 0, 1
    invm[0][3] = 0.0f;
    invm[1][3] = 0.0f;
    invm[2][3] = 0.0f;
    invm[3][3] = 1.0f;

    // Transpose upper-left 3x3 and negate
    invm[0][0] = m[0][0];
    invm[1][0] = m[0][1];
    invm[2][0] = m[0][2];
    invm[0][1] = m[1][0];
    invm[1][1] = m[1][1];
    invm[2][1] = m[1][2];
    invm[0][2] = m[2][0];
    invm[1][2] = m[2][1];
    invm[2][2] = m[2][2];

    // Negate the last row
    invm[3][0] = -m[3][0];
    invm[3][1] = -m[3][1];
    invm[3][2] = -m[3][2];
}

//------------------------------------------------------------------------------------------------
using FloatMatrix2x2 = TMatrix<float, 2U, 2U>;
using FloatMatrix3x3 = TMatrix<float, 3U, 3U>;
using FloatMatrix4x4 = TMatrix<float, 4U, 4U>;
using DoubleMatrix2x2 = TMatrix<double, 2U, 2U>;
using DoubleMatrix3x3 = TMatrix<double, 3U, 3U>;
using DoubleMatrix4x4 = TMatrix<double, 4U, 4U>;

//------------------------------------------------------------------------------------------------
// Quaternion math from http://www.gamasutra.com/view/feature/131686/rotating_objects_using_quaternions.php
// 
// Addition: q + q´ = [w + w´, v + v´] 
//
// Multiplication: qq´ = [ww´ - v . v´, v x v´ + wv´ + w´v] (. is vector dot product and x is vector cross product); Note: qq´ ? q´q 
// Conjugate: q* = [w, -v] 
//
// Norm: N(q) = sqrt(w2 + x2 + y2 + z2) 
//
// Inverse: q-1 = q* / N(q) 
//
// Unit Quaternion: q is a unit quaternion if N(q)= 1 and then q-1 = q*
//
// Identity: [1, (0, 0, 0)] (when involving multiplication) and [0, (0, 0, 0)] (when involving addition) 
template<class _Type>
struct TQuaternion
{
    _Type W;
    TVector<_Type, 3> V;
    TQuaternion() = default;
    TQuaternion(_Type w, const TVector<_Type, 3> v) :
        V(v) {}
    TQuaternion(_Type w, _Type a, _Type b, _Type c) :
        W(w)
        V{ a, b, c,} {}

    TQuaternion Conjugate() const
    {
        return TQuaternion(W, -V);
    }

    _Type
};

template<class _Type>
TQuaternion<_Type> UnitQuaternionConjugate(const TQuaternion<_Type> &Q)
{

}

template<class _Type>
TQuaternion<_Type> UnitQuaternionInverse(const TQuaternion<_Type> &Q)
{

}

template<class _Type>
TQuaternion<_Type> NormalizeQuaternion(const TQuaternion<_Type> &Q)
{
    return TQuaternion(NormalizeVector(Q.V));
}

template<class _Type>
_Type QuaternionDotProduct(const TQuaternion<_Type> &Q, const TQuaternion<_Type> &R)
{
    return DotProduct(Q.V, R.V);
}

//------------------------------------------------------------------------------------------------
template<class _Type>
TQuaternion<_Type> operator*(const TQuaternion<_Type> &Q, const TQuaternion<_Type> &R)
{
    TQuaternion T;
    T[0] = R[0] * Q[0] - R[1] * Q[1] - R[2] * Q[2] - R[3] * Q[3];
    T[1] = R[0] * Q[1] + R[1] * Q[0] - R[2] * Q[3] + R[3] * Q[4];
    T[2] = R[0] * Q[2] + R[1] * Q[3] + R[2] * Q[0] - R[3] * Q[1];
    T[3] = R[0] * Q[3] - R[1] * Q[2] + R[2] * Q[1] + R[3] * Q[0];
    return T;
}

//------------------------------------------------------------------------------------------------
template<class _Type>
TQuaternion<_Type> QuaternionSlerp(const TQuaternion<_Type> &Q, const TQuaternion<_Type> &R, _Type t)
{
    // Assumes unit quaternions

    _Type dot = QuaternionDotProduct(Q, R);
    if (dot < 0)
    {
        Q = -Q;
        dot = -dot;
    }
}

using FloatQuaternion = TQuaternion<float>;
using DoubleQuaternion = TQuaternion<double>;


}
