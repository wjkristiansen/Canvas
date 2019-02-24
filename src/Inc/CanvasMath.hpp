//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

namespace Canvas
{

//------------------------------------------------------------------------------------------------
template <class _T>
struct TQuaternion
{
    _T Q[4];
};

using FloatQuaternion = TQuaternion<float>;

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
struct TVector
{
    static auto constexpr Dimension = _D;
    _T V[Dimension] = {};

    TVector() = default;
    TVector(const TVector &o) = default;
    TVector &operator=(const TVector &o) = default;
    const _T &operator[](int index) const { return V[index]; }
    _T &operator[](int index) { return V[index]; }
};

//------------------------------------------------------------------------------------------------
template<class _T>
struct TVector<_T, 2U>
{
    static auto constexpr Dimension = 2U;
    _T V[Dimension] = {};

    TVector() = default;
    TVector(const TVector &o) = default;
    TVector(_T x, _T y) :
        V{ x, y } {}
    TVector &operator=(const TVector &o) = default;

    const _T &operator[](int index) const { return V[index]; }
    _T &operator[](int index) { return V[index]; }
    const _T &X() const { return V[0]; }
    const _T &Y() const { return V[1]; }
};

//------------------------------------------------------------------------------------------------
template<class _T>
struct TVector<_T, 3U>
{
    static auto constexpr Dimension = 3U;
    _T V[Dimension] = {};

    TVector() = default;
    TVector(const TVector &o) = default;
    TVector(_T x, _T y, _T z) :
        V{ x, y, z } {}
    TVector &operator=(const TVector &o) = default;

    const _T &operator[](int index) const { return V[index]; }
    _T &operator[](int index) { return V[index]; }
    const _T &X() const { return V[0]; }
    const _T &Y() const { return V[1]; }
    const _T &Z() const { return V[2]; }
};

//------------------------------------------------------------------------------------------------
template<class _T>
struct TVector<_T, 4U>
{
    static auto constexpr Dimension = 4U;
    _T V[Dimension] = {};

    TVector() = default;
    TVector(const TVector &o) = default;
    TVector(_T x, _T y, _T z, _T w) :
        V{ x, y, z, w } {}
    TVector &operator=(const TVector &o) = default;

    const _T &operator[](int index) const { return V[index]; }
    _T &operator[](int index) { return V[index]; }
    const _T &X() const { return V[0]; }
    const _T &Y() const { return V[1]; }
    const _T &Z() const { return V[2]; }
    const _T &W() const { return V[3]; }
};

using UIntVector2 = TVector<UINT, 2>;
using UIntVector3 = TVector<UINT, 3>;
using UIntVector4 = TVector<UINT, 4>;

using IntVector2 = TVector<INT, 2>;
using IntVector3 = TVector<INT, 3>;
using IntVector4 = TVector<INT, 4>;

using FloatVector2 = TVector<float, 2U>;
using FloatVector3 = TVector<float, 3U>;
using FloatVector4 = TVector<float, 4U>;

using DoubleVector2 = TVector<double, 2U>;
using DoubleVector3 = TVector<double, 3U>;
using DoubleVector4 = TVector<double, 4U>;

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
bool operator==(const TVector<_T, _D> &v0, const TVector<_T, _D> &v1)
{
    for (unsigned int i = 0; i < _D; ++i)
    {
        if (v0[i] != v1[i]) return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
bool operator!=(const TVector<_T, _D> &v0, const TVector<_T, _D> &v1)
{
    return !operator==(v0, v1);
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
TVector<_T, _D> operator+(const TVector<_T, _D> &v0, const TVector<_T, _D> &v1)
{
    TVector<_T, _D> result;

    for (unsigned int i = 0; i < _D; ++i)
    {
        result[i] = v0[i] + v1[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
TVector<_T, _D> operator-(const TVector<_T, _D> &v0, const TVector<_T, _D> &v1)
{
    TVector<_T, _D> result;

    for (unsigned int i = 0; i < _D; ++i)
    {
        result[i] = v0[i] - v1[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
TVector<_T, _D> operator*(const TVector<_T, _D> &v0, const TVector<_T, _D> &v1)
{
    TVector<_T, _D> result;

    for (unsigned int i = 0; i < _D; ++i)
    {
        result[i] = v0[i] * v1[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
_T DotProduct(const TVector<_T, _D> &v0, const TVector<_T, _D> &v1)
{
    _T result = 0;

    for (unsigned int i = 0; i < _D; ++i)
    {
        result += v0[i] * v1[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T>
TVector<_T, 3U> CrossProduct(const TVector<_T, 3U> &v0, const TVector<_T, 3U> &v1)
{
    return TVector<_T, 3U>(
        v0[1] * v1[2] - v0[2] * v1[1],
        v0[2] * v1[0] - v0[0] * v1[2],
        v0[0] * v1[1] - v0[1] * v1[0]
    );
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
TVector<_T, _D> operator*(const TVector<_T, _D> &v, _T mul)
{
    TVector<_T, _D> result;

    for (unsigned int i = 0; i < _D; ++i)
    {
        result[i] = v[i] * mul;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
TVector<_T, _D> operator*(_T mul, const TVector<_T, _D> &v)
{
    return operator*(v, mul);
}

//------------------------------------------------------------------------------------------------
// Caller is responsible for ensuring the input vector does not have a
// zero magnitude
template<unsigned int _D>
TVector<float, _D> NormalizeVector(const TVector<float, _D> &v)
{
    float magsq = DotProduct(v, v);
    float recipmag = 1.f / sqrtf(magsq);
    TVector<float, _D> result;
    for (unsigned int i = 0; i < _D; ++i)
    {
        result[i] = v[i] * recipmag;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
// Caller is responsible for ensuring the input vector does not have a
// zero magnitude
template<unsigned int _D>
TVector<double, _D> NormalizeVector(const TVector<double, _D> &v)
{
    double magsq = DotProduct(v, v);
    double recipmag = 1.f / sqrtl(magsq);
    TVector<double, _D> result;
    for (unsigned int i = 0; i < _D; ++i)
    {
        result[i] = v[i] * recipmag;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _Rows, unsigned int _Columns>
struct TMatrix
{
    static auto constexpr Rows = _Rows;
    static auto constexpr Columns = _Columns;
    using RowType = TVector<_T, Columns>;

    RowType M[Rows] = {};

    TMatrix(const TMatrix &o) = default;
    TMatrix &operator=(const TMatrix &o) = default;
    const RowType &operator[](int index) const { return M[index]; }
    RowType &operator[](int index) { return M[index]; }
};

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _Columns>
struct TMatrix<_T, 2U, _Columns>
{
    static auto constexpr Rows = 2U;
    static auto constexpr Columns = _Columns;
    using RowType = TVector<_T, Columns>;
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
template<class _T, unsigned int _Columns>
struct TMatrix<_T, 3U, _Columns>
{
    static auto constexpr Rows = 3U;
    static auto constexpr Columns = _Columns;
    using RowType = TVector<_T, Columns>;
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
template<class _T, unsigned int _Columns>
struct TMatrix<_T, 4U, _Columns>
{
    static auto constexpr Rows = 4U;
    static auto constexpr Columns = _Columns;
    using RowType = TVector<_T, Columns>;
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
template<class _T, unsigned int _Rows, unsigned int _Columns>
TVector<_T, _Columns> operator*(const TVector<_T, _Rows> &v, const TMatrix<_T, _Rows, _Columns> &m)
{
    TVector<_T, _Columns> result;

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
template<class _T, unsigned int _Rows0, unsigned int _Columns0, unsigned int _Columns1>
TMatrix<_T, _Rows0, _Columns1> operator*(const TMatrix<_T, _Rows0, _Columns0> &m0, const TMatrix<_T, _Columns0, _Columns1> &m1)
{
    TMatrix<_T, _Rows0, _Columns1> result;

    for (unsigned int Row = 0; Row < _Rows0; ++Row)
    {
        result[Row] = m0[Row] * m1;
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _Rows, unsigned int _Columns>
bool operator==(TMatrix<_T, _Rows, _Columns> m0, TMatrix<_T, _Rows, _Columns> m1)
{
    for (unsigned int Row = 0; Row < _Rows; ++Row)
    {
        if (m0[Row] != m1[Row]) return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _Rows, unsigned int _Columns>
bool operator!=(TMatrix<_T, _Rows, _Columns> m0, TMatrix<_T, _Rows, _Columns> m1)
{
    return !operator==(m0, m1);
}

//------------------------------------------------------------------------------------------------
template<class _T>
TMatrix<_T, 4, 4> InvertAffine(const TMatrix<_T, 4, 4> &m)
{
    TMatrix<_T, 4, 4> invm;

    // Set the last column to 0, 0, 0, 1
    invm[0][3] = 0.0f;
    invm[1][3] = 0.0f;
    invm[2][3] = 0.0f;
    invm[3][3] = 1.0f;

    // Transpose upper-left 3x3 and negate
    TMatrix<_T, 4, 4> invm;
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

}
