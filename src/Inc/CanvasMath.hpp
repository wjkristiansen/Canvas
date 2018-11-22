//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

namespace CanvasMath
{

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
class TVector;

// TVector Specializations

//------------------------------------------------------------------------------------------------
template<class _T>
class TVector<_T, 2>
{
    _T m_v[2] = {};

public:
    TVector() = default;
    TVector(_T x, _T y) :
        m_v{ x, y } {}
    using Type = _T;
    static constexpr unsigned int Dimension = 2UL;

    Type operator[](int index) const { return m_v[index]; }
    Type &operator[](int index) { return m_v[index]; }
    Type X() const { return m_v[0]; }
    Type Y() const { return m_v[1]; }
};

//------------------------------------------------------------------------------------------------
template<class _T>
class TVector<_T, 3>
{
    _T m_v[3] = {};

public:
    TVector() = default;
    TVector(_T x, _T y, _T z) :
        m_v{ x, y, z } {}
    using Type = _T;
    static constexpr unsigned int Dimension = 3UL;

    Type operator[](int index) const { return m_v[index]; }
    Type &operator[](int index) { return m_v[index]; }
    Type X() const { return m_v[0]; }
    Type Y() const { return m_v[1]; }
    Type Z() const { return m_v[2]; }
};

//------------------------------------------------------------------------------------------------
template<class _T>
class TVector<_T, 4>
{
    _T m_v[4] = {};

public:
    TVector() = default;
    TVector(_T x, _T y, _T z, _T w) :
        m_v{ x, y, z, w } {}
    using Type = _T;
    static constexpr unsigned int Dimension = 4UL;

    Type operator[](int index) const { return m_v[index]; }
    Type &operator[](int index) { return m_v[index]; }
    Type X() const { return m_v[0]; }
    Type Y() const { return m_v[1]; }
    Type Z() const { return m_v[2]; }
    Type W() const { return m_v[3]; }
};

using TFloatVector2 = TVector<float, 2>;
using TFloatVector3 = TVector<float, 3>;
using TFloatVector4 = TVector<float, 4>;

using TDoubleVector2 = TVector<double, 2>;
using TDoubleVector3 = TVector<double, 3>;
using TDoubleVector4 = TVector<double, 4>;

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
TVector<_T, _D> operator+(const TVector<_T, _D> &A, const TVector<_T, _D> &B)
{
    TVector<_T, _D> result;

    for (unsigned int i = 0; i < _D; ++i)
    {
        result[i] = A[i] + B[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
TVector<_T, _D> operator-(const TVector<_T, _D> &A, const TVector<_T, _D> &B)
{
    TVector<_T, _D> result;

    for (unsigned int i = 0; i < _D; ++i)
    {
        result[i] = A[i] - B[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
TVector<_T, _D> operator*(const TVector<_T, _D> &A, const TVector<_T, _D> &B)
{
    TVector<_T, _D> result;

    for (unsigned int i = 0; i < _D; ++i)
    {
        result[i] = A[i] * B[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _D>
_T DotProduct(const TVector<_T, _D> &A, const TVector<_T, _D> &B)
{
    _T result = 0;

    for (unsigned int i = 0; i < _D; ++i)
    {
        result += A[i] * B[i];
    }

    return result;
}

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _Rows, unsigned int _Columns>
class TMatrix;

//------------------------------------------------------------------------------------------------
template<class _T, unsigned int _Rows>
class TMatrix<_T, _Rows, 2>
{
    TVector<_T, 2> m_Rows[_Rows];

public:
    TMatrix() = default;
    TMatrix(const TVector<_T, 2> &v0, const TVector<_T, 2> &v1) :
        m_Rows{ v0, v1 } {}
};

}
