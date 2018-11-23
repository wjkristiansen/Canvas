//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

namespace Canvas
{

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
template<class _T, unsigned int _Rows, unsigned int _Columns>
struct TMatrix
{
    static auto constexpr Rows = _Rows;
    static auto constexpr Columns = _Columns;
    using RowType = TVector<_T, Columns>;
    RowType M[Rows] = {};
};

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _Rows>
struct TMatrix<_T, _Rows, 2U>
{
    static auto constexpr Rows = _Rows;
    static auto constexpr Columns = 2U;
    using RowType = TVector<_T, Columns>;
    RowType M[Rows] = {};

    TMatrix() = default;
    TMatrix(
        const _T &m00, const _T &m01,
        const _T &m10, const _T &m11
    ) :
        M{
        TVector<_T, Columns>(m00, m01),
        TVector<_T, Columns>(m10, m11)
    } {}
    TMatrix(const RowType &Row0, const RowType &Row1) :
        M{ Row0, Row1 } {}

    const RowType &operator[](int index) const { return M[index]; }
    RowType &operator[](int index) { return M[index]; }
};

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _Rows>
struct TMatrix<_T, _Rows, 3U>
{
    static auto constexpr Rows = _Rows;
    static auto constexpr Columns = 3U;
    using RowType = TVector<_T, Columns>;
    RowType M[Rows] = {};

    TMatrix() = default;
    TMatrix(
        const _T &m00, const _T &m01, const _T &m02,
        const _T &m10, const _T &m11, const _T &m12,
        const _T &m20, const _T &m21, const _T &m22
        ) :
        M{
        TVector<_T, Columns>(m00, m01, m02),
        TVector<_T, Columns>(m10, m11, m12),
        TVector<_T, Columns>(m20, m21, m22)
    } {}
    TMatrix(const RowType &Row0, const RowType &Row1, const RowType &Row2) :
        M{ Row0, Row1, Row2 } {}

    const RowType &operator[](int index) const { return M[index]; }
    RowType &operator[](int index) { return M[index]; }
};

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _Rows>
struct TMatrix<_T, _Rows, 4U>
{
    static auto constexpr Rows = _Rows;
    static auto constexpr Columns = 4U;
    using RowType = TVector<_T, Columns>;
    RowType M[Rows] = {};

    TMatrix() = default;
    TMatrix(
        const _T &m00, const _T &m01, const _T &m02, const _T &m03,
        const _T &m10, const _T &m11, const _T &m12, const _T &m13,
        const _T &m20, const _T &m21, const _T &m22, const _T &m23,
        const _T &m30, const _T &m31, const _T &m32, const _T &m33
    ) :
        M{ 
        TVector<_T, Columns>(m00, m01, m02, m03),
        TVector<_T, Columns>(m10, m11, m12, m13),
        TVector<_T, Columns>(m20, m21, m22, m23),
        TVector<_T, Columns>(m30, m31, m32, m33)
    } {}
    TMatrix(const RowType &Row0, const RowType &Row1, const RowType &Row2, const RowType &Row3) :
        M{ Row0, Row1, Row2, Row3 } {}

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
using FloatMatrix2x2 = TMatrix<float, 2U, 2U>;
using FloatMatrix3x3 = TMatrix<float, 3U, 3U>;
using FloatMatrix4x4 = TMatrix<float, 4U, 4U>;
using DoubleMatrix2x2 = TMatrix<double, 2U, 2U>;
using DoubleMatrix3x3 = TMatrix<double, 3U, 3U>;
using DoubleMatrix4x4 = TMatrix<double, 4U, 4U>;

}
