//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

#include <math.h>
#include <float.h>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201) // nameless struct/union
#endif

namespace Canvas
{
    namespace Math
    {
        inline constexpr double Pi = 3.14159265358979323846;

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        struct TVector
        {
            static auto constexpr Dim = _Dim;
            using Type = _Type;

            Type V[Dim] = {};

            TVector() = default;
            TVector(const TVector &o) = default;
            TVector &operator=(const TVector &o) = default;

            const _Type &operator[](int index) const { return V[index]; }
            Type &operator[](int index) { return V[index]; }
        };

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        struct TVector<_Type, 2>
        {
            static auto constexpr Dim = 2;
            using Type = _Type;

            union
            {
                Type V[Dim];
                struct
                {
                    Type X;
                    Type Y;
                };
            };
            

            TVector() : V{} {};
            TVector(_Type x, _Type y) :
                V{ x, y } {}
            TVector(const TVector &o) = default;
            TVector &operator=(const TVector &o) = default;

            const _Type &operator[](int index) const { return V[index]; }
            Type &operator[](int index) { return V[index]; }
        };

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        struct TVector<_Type, 3>
        {
            static auto constexpr Dim = 3;
            using Type = _Type;

            union
            {
                Type V[Dim];
                struct
                {
                    Type X;
                    Type Y;
                    Type Z;
                };
            };

            TVector() : V{} {};
            TVector(_Type x, _Type y, _Type z) :
                V{ x, y, z } {}
            TVector(const TVector &o) = default;
            TVector &operator=(const TVector &o) = default;

            const _Type &operator[](int index) const { return V[index]; }
            Type &operator[](int index) { return V[index]; }
        };

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        struct TVector<_Type, 4>
        {
            static auto constexpr Dim = 4;
            using Type = _Type;

            union
            {
                Type V[Dim];
                struct
                {
                    Type X;
                    Type Y;
                    Type Z;
                    Type W;
                };
            };

            TVector() : V{} {};
            TVector(_Type x, _Type y, _Type z, _Type w) :
                V{ x, y, z, w } {}
            TVector(const TVector &o) = default;
            TVector &operator=(const TVector &o) = default;

            const _Type &operator[](int index) const { return V[index]; }
            Type &operator[](int index) { return V[index]; }
        };


        //------------------------------------------------------------------------------------------------
        template<>
        struct alignas(16) TVector<float, 4>
        {
            static auto constexpr Dim = 4;
            using Type = float;

            union
            {
                Type V[Dim];
                struct
                {
                    Type X;
                    Type Y;
                    Type Z;
                    Type W;
                };
            };

            TVector() = default;
            TVector(Type x, Type y, Type z, Type w) :
                V{ x, y, z, w } {}
            TVector(const TVector &o) = default;
            TVector &operator=(const TVector &o) = default;

            const Type &operator[](int index) const { return V[index]; }
            Type &operator[](int index) { return V[index]; }
        };

        using UIntVector2 = TVector<unsigned int, 2>;
        using UIntVector3 = TVector<unsigned int, 3>;
        using UIntVector4 = TVector<unsigned int, 4>;

        using IntVector2 = TVector<int, 2>;
        using IntVector3 = TVector<int, 3>;
        using IntVector4 = TVector<int, 4>;

        using FloatVector2 = TVector<float, 2>;
        using FloatVector3 = TVector<float, 3>;
        using FloatVector4 = TVector<float, 4>;

        using DoubleVector2 = TVector<double, 2>;
        using DoubleVector3 = TVector<double, 3>;
        using DoubleVector4 = TVector<double, 4>;

        //------------------------------------------------------------------------------------------------
        // Uniary -
        template<class _Type, int _Dim>
        TVector<_Type, _Dim> operator-(const TVector<_Type, _Dim> &v)
        {
            TVector<_Type, _Dim> u;
            for (int i = 0; i < _Dim; ++i)
            {
                u[i] = -v[i];
            }
            return u;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        bool operator==(const TVector<_Type, _Dim> &a, const TVector<_Type, _Dim> &b)
        {
            for (int i = 0; i < _Dim; ++i)
            {
                if (a[i] != b[i]) return false;
            }

            return true;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        bool operator!=(const TVector<_Type, _Dim> &a, const TVector<_Type, _Dim> &b)
        {
            return !operator==(a, b);
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        TVector<_Type, _Dim> operator+(const TVector<_Type, _Dim> &a, const TVector<_Type, _Dim> &b)
        {
            TVector<_Type, _Dim> result;

            for (int i = 0; i < _Dim; ++i)
            {
                result[i] = a[i] + b[i];
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        TVector<_Type, _Dim> operator-(const TVector<_Type, _Dim> &a, const TVector<_Type, _Dim> &b)
        {
            TVector<_Type, _Dim> result;

            for (int i = 0; i < _Dim; ++i)
            {
                result[i] = a[i] - b[i];
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        TVector<_Type, _Dim> operator*(const TVector<_Type, _Dim> &a, const TVector<_Type, _Dim> &b)
        {
            TVector<_Type, _Dim> result;

            for (int i = 0; i < _Dim; ++i)
            {
                result[i] = a[i] * b[i];
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        // There is an optimization bug in the msvc compiler.
        // The compiler tries to use the 4-parameter FloatVector4 constructor
        // post-muliplicatio of each scalar element.  This prevent the use of SSE in some inline
        // cases.
        // Constructing the result first from a then using unary *= tricks the compiler
        // back to using the SSE ops.
        // The problem goes away if the 4-parameter constructor is deleted.
        //
        // Interestingly, this is not always the case.  When operator* is used in the DotProduct
        // implementation, the compiler uses the SSE movups and mulps operations.  May
        // need to report this to the msvc compiler team.
        //
        // Only need to do this for FloatVector4 since the is where SSE can be useful.
        inline TVector<float, 4> operator*(const TVector<float, 4> & a, const TVector<float, 4> & b)
        {
            TVector<float, 4> result =
            {
                a[0] * b[0],
                a[1] * b[1],
                a[2] * b[2],
                a[3] * b[3]
            };

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        TVector<_Type, _Dim> operator/(const TVector<_Type, _Dim> &a, const TVector<_Type, _Dim> &b)
        {
            TVector<_Type, _Dim> result;

            for (int i = 0; i < _Dim; ++i)
            {
                result[i] = a[i] / b[i];
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        _Type VectorSum(const TVector<_Type, _Dim> &v)
        {
            _Type r = 0;
            for (int i = 0; i < _Dim; ++i)
            {
                r += v[i];
            }
            return r;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        _Type DotProduct(const TVector<_Type, _Dim> &a, const TVector<_Type, _Dim> &b)
        {
            TVector<_Type, _Dim> temp = a * b;
            return VectorSum(temp);
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        TVector<_Type, _Dim> operator*(const TVector<_Type, _Dim> &v, _Type mul)
        {
            TVector<_Type, _Dim> result;

            for (int i = 0; i < _Dim; ++i)
            {
                result[i] = v[i] * mul;
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        inline TVector<float, 4> operator*(const TVector<float, 4> & v, float mul)
        {
            TVector<float, 4> result(v);
            result[0] *= mul;
            result[1] *= mul;
            result[2] *= mul;
            result[3] *= mul;

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Dim>
        TVector<_Type, _Dim> operator*(_Type mul, const TVector<_Type, _Dim> &v)
        {
            return operator*(v, mul);
        }

        //------------------------------------------------------------------------------------------------
        // Caller is responsible for ensuring the input vector does not have a
        // zero magnitude
        template<class _Type, int _Dim>
        TVector<_Type, _Dim> NormalizeVector(const TVector<_Type, _Dim> &v)
        {
            const _Type magsq = DotProduct(v, v);
            const _Type recipmag = 1 / std::sqrt(magsq);
            TVector<_Type, _Dim> n;
            for (int i = 0; i < _Dim; ++i)
            {
                n[i] = v[i] * recipmag;
            }

            return n;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Rows, int _Columns>
        struct TMatrix
        {
            static auto constexpr Rows = _Rows;
            static auto constexpr Columns = _Columns;
            using RowType = TVector<_Type, Columns>;
            using ElementType = _Type;

            RowType M[Rows] = {};

            TMatrix() = default;
            TMatrix(const TMatrix &o) = default;
            TMatrix &operator=(const TMatrix &o) = default;
            const RowType &operator[](int index) const { return M[index]; }
            RowType &operator[](int index) { return M[index]; }
            inline static TMatrix Identity()
            {
                TMatrix m = {};
                for (int i = 0; i < std::min(_Rows, _Columns); ++i)
                {
                    m[i][i] = 1;
                }
                return m;
            }
        };

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Columns>
        struct TMatrix<_Type, 2, _Columns>
        {
            static auto constexpr Rows = 2;
            static auto constexpr Columns = _Columns;
            using RowType = TVector<_Type, Columns>;
            using ElementType = _Type;

            RowType M[Rows] = {};

            TMatrix() = default;
            TMatrix(const RowType &r0, const RowType &r1) :
                M{ r0, r1 } {}
            TMatrix(const TMatrix &o) = default;
            TMatrix &operator=(const TMatrix &o) = default;
            const RowType &operator[](int index) const { return M[index]; }
            RowType &operator[](int index) { return M[index]; }
            inline static TMatrix Identity()
            {
                TMatrix m = {};
                for (int i = 0; i < std::min(2, _Columns); ++i)
                {
                    m[i][i] = 1;
                }
                return m;
            }
        };

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Columns>
        struct TMatrix<_Type, 3, _Columns>
        {
            static auto constexpr Rows = 3;
            static auto constexpr Columns = _Columns;
            using RowType = TVector<_Type, Columns>;
            using ElementType = _Type;

            RowType M[Rows] = {};

            TMatrix() = default;
            TMatrix(const RowType &r0, const RowType &r1, const RowType &r2) :
                M{ r0, r1, r2 } {}
            TMatrix(const TMatrix &o) = default;
            TMatrix &operator=(const TMatrix &o) = default;
            const RowType &operator[](int index) const { return M[index]; }
            RowType &operator[](int index) { return M[index]; }
            inline static TMatrix Identity()
            {
                TMatrix m = {};
                for (int i = 0; i < std::min(3, _Columns); ++i)
                {
                    m[i][i] = 1;
                }
                return m;
            }
        };

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Columns>
        struct TMatrix<_Type, 4, _Columns>
        {
            static auto constexpr Rows = 4;
            static auto constexpr Columns = _Columns;
            using RowType = TVector<_Type, Columns>;
            using ElementType = _Type;

            RowType M[Rows] = {};

            TMatrix() = default;
            TMatrix(const RowType &r0, const RowType &r1, const RowType &r2, const RowType &r3) :
                M{ r0, r1, r2, r3 } {}
            TMatrix(const TMatrix &o) = default;
            TMatrix &operator=(const TMatrix &o) = default;
            const RowType &operator[](int index) const { return M[index]; }
            RowType &operator[](int index) { return M[index]; }
            inline static TMatrix Identity()
            {
                TMatrix m = {};
                for (int i = 0; i < std::min(4, _Columns); ++i)
                {
                    m[i][i] = 1;
                }
                return m;
            }
        };

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Rows, int _Columns>
        TVector<_Type, _Columns> operator*(const TVector<_Type, _Rows> &v, const TMatrix<_Type, _Rows, _Columns> &m)
        {
            TVector<_Type, _Columns> result = {};

            for (int Col = 0; Col < _Columns; ++Col)
            {
                for (int Row = 0; Row < _Rows; ++Row)
                {
                    result[Col] += v[Row] * m[Row][Col];
                }
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Rows, int _Columns>
        TVector<_Type, _Columns> operator*(const TMatrix<_Type, _Rows, _Columns> &m, const TVector<_Type, _Rows> &v)
        {
            TVector<_Type, _Columns> result = {};

            for (int i = 0; i < _Rows; ++i)
            {
                result[i] = DotProduct(m[i], v);
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Rows0, int _Columns0, int _Columns1>
        TMatrix<_Type, _Rows0, _Columns1> operator*(const TMatrix<_Type, _Rows0, _Columns0> &m0, const TMatrix<_Type, _Columns0, _Columns1> &m1)
        {
            TMatrix<_Type, _Rows0, _Columns1> result;

            for (int Row = 0; Row < _Rows0; ++Row)
            {
                result[Row] = m0[Row] * m1;
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Rows, int _Columns>
        TMatrix<_Type, _Rows, _Columns> operator*(const _Type &scale, const TMatrix<_Type, _Rows, _Columns> &m)
        {
            TMatrix<_Type, _Rows, _Columns> result;

            for (int Row = 0; Row < _Rows; ++Row)
            {
                result[Row] = m[Row] * scale;
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Rows, int _Columns>
        TMatrix<_Type, _Rows, _Columns> operator*(const TMatrix<_Type, _Rows, _Columns> &m, const _Type &scale)
        {
            TMatrix<_Type, _Rows, _Columns> result;

            for (int Row = 0; Row < _Rows; ++Row)
            {
                result[Row] = m[Row] * scale;
            }

            return result;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Rows, int _Columns>
        bool operator==(TMatrix<_Type, _Rows, _Columns> m0, TMatrix<_Type, _Rows, _Columns> m1)
        {
            for (int Row = 0; Row < _Rows; ++Row)
            {
                if (m0[Row] != m1[Row]) return false;
            }
            return true;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type, int _Rows, int _Columns>
        bool operator!=(TMatrix<_Type, _Rows, _Columns> m0, TMatrix<_Type, _Rows, _Columns> m1)
        {
            return !operator==(m0, m1);
        }

        template<class _Type, int _Rows, int _Columns>
        TMatrix<_Type, _Rows, _Columns> IdentityMatrix()
        {
            TMatrix<_Type, _Rows, _Columns> m = {};
            for (int i = 0; i < (_Rows < _Columns ? _Rows : _Columns); ++i)
            {
                m[i][i] = 1;
            }

            return m;
        }

        //------------------------------------------------------------------------------------------------
        // Sets the upper-left 3x3 as a rotation about the x-axis
        // with the given Euler angle
        template<class _Type>
        TMatrix<_Type, 4, 4> XRotationMatrix(_Type angle)
        {
            auto m = IdentityMatrix<_Type, 4, 4>();

            _Type c = cos(angle);
            _Type s = sin(angle);
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
        TMatrix<_Type, 4, 4> YRotationMatrix(_Type angle)
        {
            auto m = IdentityMatrix<_Type, 4, 4>();

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
        TMatrix<_Type, 4, 4> ZRotationMatrix(_Type angle)
        {
            auto m = IdentityMatrix<_Type, 4, 4>();

            alignas(16) _Type c = cos(angle);
            alignas(16) _Type s = sin(angle);
            m[0][0] = c;
            m[0][1] = -s;

            m[1][0] = s;
            m[1][1] = c;

            return m;
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        TMatrix<_Type, 4, 4> XYZRotationMatrix(_Type angleX, _Type angleY, _Type angleZ)
        {
            return ZRotationMatrix(angleZ) * YRotationMatrix(angleY) * XRotationMatrix(angleX);
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        TMatrix<_Type, 4, 4> XZYRotationMatrix(_Type angleX, _Type angleZ, _Type angleY)
        {
            return YRotationMatrix(angleY) * ZRotationMatrix(angleZ) * XRotationMatrix(angleX);
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        TMatrix<_Type, 4, 4> YXZRotationMatrix(_Type angleY, _Type angleX, _Type angleZ)
        {
            return ZRotationMatrix(angleZ) * XRotationMatrix(angleX) * YRotationMatrix(angleY);
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        TMatrix<_Type, 4, 4> YZXRotationMatrix(_Type angleY, _Type angleZ, _Type angleX)
        {
            return XRotationMatrix(angleX) * ZRotationMatrix(angleZ) * YRotationMatrix(angleY);
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        TMatrix<_Type, 4, 4> ZXYRotationMatrix(_Type angleZ, _Type angleX, _Type angleY)
        {
            return YRotationMatrix(angleY) * XRotationMatrix(angleX) * ZRotationMatrix(angleZ);
        }

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        TMatrix<_Type, 4, 4> ZYXRotationMatrix(_Type angleZ, _Type angleY, _Type angleX)
        {
            return XRotationMatrix(angleX) * YRotationMatrix(angleY) * ZRotationMatrix(angleZ);
        }

        //------------------------------------------------------------------------------------------------
        // Transposes all or part of a matrix.
        // Returns a copy of the given matrix with the specified square ranges transposed.
        template<class _MatrixType>
        _MatrixType MatrixTransposeRows(const _MatrixType &m, int Dim = _MatrixType::Rows, int OffRow = 0, int OffCol = 0)
        {
            _MatrixType n = m;
            for (int i = 0; i < Dim; ++i)
            {
                for (int j = 0; j < Dim; ++j)
                {
                    n[j + OffRow][i + OffCol] = m[i + OffRow][j + OffCol];
                }
            }
            return n;
        }

        //------------------------------------------------------------------------------------------------
        using FloatMatrix2x2 = TMatrix<float, 2, 2>;
        using FloatMatrix3x3 = TMatrix<float, 3, 3>;
        using FloatMatrix4x4 = TMatrix<float, 4, 4>;
        using DoubleMatrix2x2 = TMatrix<double, 2, 2>;
        using DoubleMatrix3x3 = TMatrix<double, 3, 3>;
        using DoubleMatrix4x4 = TMatrix<double, 4, 4>;


        //------------------------------------------------------------------------------------------------
        template<class VectorType>
        VectorType CrossProduct(const VectorType &a, const VectorType &b)
        {
            VectorType c;

            c[0] = a[1] * b[2] - a[2] * b[1];
            c[1] = a[2] * b[0] - a[0] * b[2];
            c[2] = a[0] * b[1] - a[1] * b[0];

            return c;
        }

        //------------------------------------------------------------------------------------------------
        // Mathematically treats the inputs as float3 vectors
        inline FloatVector4 CrossProduct(const FloatVector4 &a, const FloatVector4 &b)
        {
            return FloatVector4(
                a.Y * b.Z - a.Z * b.Y,
                a.Z * b.X - a.X * b.Z,
                a.X * b.Y - a.Y * b.X,
                0.0f
            );
        }


        //------------------------------------------------------------------------------------------------
        // Represents a Unit Quaternion representing a rotation in 3-dimensional space
        // Quaternion math from http://www.gamasutra.com/view/feature/131686/rotating_objects_using_quaternions.php
        // 
        // Addition: q + q' = [w + w', v + v'] 
        //
        // Multiplication: qq' = [ww' - v . v', v x v' + wv' + w'v] (. is vector dot product and x is vector cross product); Note: qq' ? q'q 
        // Conjugate: q* = [w, -v] 
        //
        // Inverse: q-1 = q* / N(q) 
        //
        // Unit Quaternion: q is a unit quaternion if N(q)= 1 and then q-1 = q*
        //
        // Identity: [1, (0, 0, 0)] (when involving multiplication) and [0, (0, 0, 0)] (when involving addition) 
        template<class _Type>
        struct TQuaternion : public TVector<_Type, 4>
        {
            using Type = _Type;

            TQuaternion() = default;
            TQuaternion(_Type scalar) :
                TVector{ 0, 0, 0, scalar } {}
            TQuaternion(const TVector<Type, 4> & v) :
                TVector(v) {}
            TQuaternion(Type x, Type y, Type z, Type w) :
                TVector{ x, y, z, w } {}

            TQuaternion(const TQuaternion &o) = default;

            TQuaternion &operator=(const TQuaternion &o) = default;

            TQuaternion<Type> Normalize() const;


            // Returns a vector whose direction is the axis and whose magnitude is the angle
            // AKA the quaternion Log
            TVector<Type, 3> ToScaledAxisAngle() const
            {
                auto qn = Normalize();
                const Type vLen = std::sqrt(qn.X * qn.X + qn.Y * qn.Y + qn.Z * qn.Z);
                const Type angle = std::atan2(vLen, qn.W); // angle in radians

                if (vLen < 1e-6f)
                    return TVector<Type, 3>(0, 0, 0); // zero rotation

                const Type scale = angle / vLen;
                return TVector<Type, 3>(qn.X * scale, qn.Y * scale, qn.Z * scale);
            }

            // Constructs a quaternion from scaled axis-angle representation
            // The length of `v` represents the rotation angle
            static TQuaternion<Type> FromScaledAxisAngle(const TVector<_Type, 3>& v)
            {
                const Type angle = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);

                if (angle < 1e-6f)
                    return TQuaternion<Type>(0, 0, 0, 1); // identity rotation

                const Type sinAngle = std::sin(angle);
                const Type cosAngle = std::cos(angle);
                const Type scale = sinAngle / angle;

                return TQuaternion<Type>(v.X * scale, v.Y * scale, v.Z * scale, cosAngle);
            }

            static TQuaternion<Type> FromEulerXYZ(Type x, Type y, Type z)
            {
                const Type hx = x * Type(0.5), hy = y * Type(0.5), hz = z * Type(0.5);
                const Type cx = std::cos(hx), sx = std::sin(hx);
                const Type cy = std::cos(hy), sy = std::sin(hy);
                const Type cz = std::cos(hz), sz = std::sin(hz);

                // Intrinsic XYZ => q = qz * qy * qx
                const TQuaternion<Type> qx(sx, 0, 0, cx);
                const TQuaternion<Type> qy(0, sy, 0, cy);
                const TQuaternion<Type> qz(0, 0, sz, cz);
                return (qz * qy * qx).Normalize();
            }

            static TQuaternion<Type> FromEulerXZY(Type x, Type z, Type y)
            {
                const Type hx = x * Type(0.5), hy = y * Type(0.5), hz = z * Type(0.5);
                const Type cx = std::cos(hx), sx = std::sin(hx);
                const Type cy = std::cos(hy), sy = std::sin(hy);
                const Type cz = std::cos(hz), sz = std::sin(hz);

                // Intrinsic XZY => q = qy * qz * qx
                const TQuaternion<Type> qx(sx, 0, 0, cx);
                const TQuaternion<Type> qy(0, sy, 0, cy);
                const TQuaternion<Type> qz(0, 0, sz, cz);
                return (qy * qz * qx).Normalize();
            }

            static TQuaternion<Type> FromEulerYXZ(Type y, Type x, Type z)
            {
                const Type hx = x * Type(0.5), hy = y * Type(0.5), hz = z * Type(0.5);
                const Type cx = std::cos(hx), sx = std::sin(hx);
                const Type cy = std::cos(hy), sy = std::sin(hy);
                const Type cz = std::cos(hz), sz = std::sin(hz);

                // Intrinsic YXZ => q = qz * qx * qy
                const TQuaternion<Type> qx(sx, 0, 0, cx);
                const TQuaternion<Type> qy(0, sy, 0, cy);
                const TQuaternion<Type> qz(0, 0, sz, cz);
                return (qz * qx * qy).Normalize();
            }

            static TQuaternion<Type> FromEulerYZX(Type y, Type z, Type x)
            {
                const Type hx = x * Type(0.5), hy = y * Type(0.5), hz = z * Type(0.5);
                const Type cx = std::cos(hx), sx = std::sin(hx);
                const Type cy = std::cos(hy), sy = std::sin(hy);
                const Type cz = std::cos(hz), sz = std::sin(hz);

                // Intrinsic YZX => q = qx * qz * qy
                const TQuaternion<Type> qx(sx, 0, 0, cx);
                const TQuaternion<Type> qy(0, sy, 0, cy);
                const TQuaternion<Type> qz(0, 0, sz, cz);
                return (qx * qz * qy).Normalize();
            }

            static TQuaternion<Type> FromEulerZXY(Type z, Type x, Type y)
            {
                const Type hx = x * Type(0.5), hy = y * Type(0.5), hz = z * Type(0.5);
                const Type cx = std::cos(hx), sx = std::sin(hx);
                const Type cy = std::cos(hy), sy = std::sin(hy);
                const Type cz = std::cos(hz), sz = std::sin(hz);

                // Intrinsic ZXY => q = qy * qx * qz
                const TQuaternion<Type> qx(sx, 0, 0, cx);
                const TQuaternion<Type> qy(0, sy, 0, cy);
                const TQuaternion<Type> qz(0, 0, sz, cz);
                return (qy * qx * qz).Normalize();
            }

            static TQuaternion<Type> FromEulerZYX(Type z, Type y, Type x)
            {
                const Type hx = x * Type(0.5), hy = y * Type(0.5), hz = z * Type(0.5);
                const Type cx = std::cos(hx), sx = std::sin(hx);
                const Type cy = std::cos(hy), sy = std::sin(hy);
                const Type cz = std::cos(hz), sz = std::sin(hz);

                // Intrinsic ZYX => q = qx * qy * qz
                const TQuaternion<Type> qx(sx, 0, 0, cx);
                const TQuaternion<Type> qy(0, sy, 0, cy);
                const TQuaternion<Type> qz(0, 0, sz, cz);
                return (qx * qy * qz).Normalize();
            }
        };

        //------------------------------------------------------------------------------------------------
        template<class _Type>
        TQuaternion<_Type> IdentityQuaternion()
        {
            return TQuaternion<_Type>(0, 0, 0, 1);
        }

        //------------------------------------------------------------------------------------------------
        // Should be a unit quaternion but may need to be Normalized to correct
        // for accumulated floating point errors
        template<class _Type>
        TQuaternion<_Type> TQuaternion<_Type>::Normalize() const
        {
            TQuaternion<_Type> qn = *this;
            const _Type  dot = DotProduct(qn, qn);
            constexpr _Type epsilon = _Type(1e-6);
            if(std::abs(dot - static_cast<_Type>(1)) > epsilon)
            {
                const _Type  rsq = 1 / std::sqrt(dot);
                qn = qn * rsq;
            }
            return qn;
        }

        //------------------------------------------------------------------------------------------------
        // Returns the conjugate quaternion.
        // Note: Since this is expected to be a unit quaternion, the conjugate is the also
        // the inverse.
        template<class _Type>
        TQuaternion<_Type> Conjugate(const TQuaternion<_Type> &q)
        {
            TQuaternion<_Type> c;
            c[0] = -q.X;
            c[1] = -q.Y;
            c[2] = -q.Z;
            c[3] = q.W;
            return c;
        }

        //------------------------------------------------------------------------------------------------
        // Returns the product of two unit quaternions.
        // See https://en.wikipedia.org/wiki/Quaternion#Hamilton_product
        template<class _Type>
        TQuaternion<_Type> operator*(const TQuaternion<_Type> q, const TQuaternion<_Type> &r)
        {
            TQuaternion<_Type> v = CrossProduct(q, r) + q.W * r + r.W * q; // Leaves garbage in v.W but that gets fixed below
            v.W = q.W * r.W - (q.X * r.X + q.Y * r.Y + q.Z * r.Z);
            return v;
        }

        //------------------------------------------------------------------------------------------------
        // Returns the product of a quaternion with a vector (treated as a quaternion with a 0 real coordinate)
        template<class _Type>
        TVector<_Type, 3> RotateVectorByQuaternion(const TQuaternion<_Type> &q, const TVector<_Type, 3> &v)
        {
            TQuaternion<_Type> p(v.X, v.Y, v.Z, 0); // pure quaternion
            TQuaternion<_Type> qInv = q.Conjugate();  // must be unit-length
            TQuaternion<_Type> rotated = q * p * qInv;
            return TVector<_Type, 3>(rotated.X, rotated.Y, rotated.Z);
        }

        //------------------------------------------------------------------------------------------------
        // Performs a Spherical-linear interpolation (SLERP) between two Quaternions and
        // returns the resulting Quaternion
        //template<class _Type>
        //TQuaternion<_Type> QuaternionSlerp(const TQuaternion<_Type> &q, const TQuaternion<_Type> &r, _Type t)
        //{
        //    // Assumes unit quaternions
        //
        //    _Type dot = DotProduct(q, r);
        //    if (dot < 0)
        //    {
        //        q = -q;
        //        dot = -dot;
        //    }
        //}

        //------------------------------------------------------------------------------------------------
        // Converts the unit quaternion to a 3x3 rotation matrix
        template<class _Type>
        TMatrix<_Type, 4, 4> QuaternionToRotationMatrix(const TQuaternion<_Type> & q)
        {
            return TMatrix<_Type, 4, 4>(
                {
                    {
                        1 - 2 * (q.Y * q.Y + q.Z * q.Z),
                        2 * (q.X * q.Y - q.Z * q.W),
                        2 * (q.X * q.Z + q.Y * q.W),
                        0
                    },
                    {
                        2 * (q.X * q.Y + q.Z * q.W),
                        1 - 2 * (q.X * q.X + q.Z * q.Z),
                        2 * (q.Y * q.Z - q.X * q.W),
                        0
                    },
                    {
                        2 * (q.X * q.Z - q.Y * q.W),
                        2 * (q.Y * q.Z + q.X * q.W),
                        1 - 2 * (q.X * q.X + q.Y * q.Y),
                        0
                    },
                    {
                        0, 0, 0, 1
                    }
                });
        }

        //------------------------------------------------------------------------------------------------
        // Creates a quaternion from the provided angle (in radians) and axis (assumed to be a unit vector)
        // Axis is in v[0-2], angle is in v[3]
        template<class _Type>
        TQuaternion<_Type> QuaternionFromAngleAxis(const TVector<_Type, 4> & v)
        {
            const _Type HalfAngle = v[3] / 2;
            const _Type c = cos(HalfAngle);
            const _Type s = sin(HalfAngle);
            TQuaternion<_Type> q;
            q = s * v;
            q.W = c;
            return q.Normalize();
        }

        //------------------------------------------------------------------------------------------------
        // Creates a quaternion from a given matrix
        // Assumes the matrix is rotation only 3x3 or affine (no scale or translation)
        // See http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
        template<class _MatrixType>
        TQuaternion<typename _MatrixType::ElementType> QuaternionFromRotationMatrix(const _MatrixType &m)
        {
            static_assert(_MatrixType::Columns >= 3);
            static_assert(_MatrixType::Rows >= 3);
            static_assert(_MatrixType::Columns < 5);
            using Type = _MatrixType::ElementType;
            TQuaternion<Type> q;
            Type t = m[0][0] + m[1][1] + m[2][2];

            if (t > 0)
            {
                const Type s = 2 * std::sqrt(t + 1);
                const Type srsq = 1 / s;
                q.W = .25 * s;
                q.X = (m[2][1] - m[1][2]) * srsq;
                q.Y = (m[0][2] - m[2][0]) * srsq;
                q.Z = (m[1][0] - m[0][1]) * srsq;
            }
            else if (m[0][0] > m[1][1] && (m[0][0] > m[2][2]))
            {
                t = m[0][0] - m[1][1] - m[2][2];
                const Type s = 2 * std::sqrt(t + 1);
                const Type srsq = 1 / s;
                q.W = (m[2][1] - m[1][2]) * srsq;
                q.X = .25 * s;
                q.Y = (m[0][1] + m[1][0]) * srsq;
                q.Z = (m[0][2] + m[2][0]) * srsq;
            }
            else if (m[1][1] > m[2][2])
            {
                t = m[1][1] - m[0][0] - m[2][2];
                const Type s = 2 * std::sqrt(t + 1);
                const Type srsq = 1 / s;
                q.W = (m[0][2] - m[2][0]) * srsq;
                q.X = (m[0][1] + m[1][0]) * srsq;
                q.Y = .25 * s;
                q.Z = (m[1][2] + m[2][1]) * srsq;
            }
            else
            {
                t = m[2][2] - m[0][0] - m[1][1];
                const Type s = 2 * std::sqrt(t + 1);
                const Type srsq = 1 / s;
                q.W = (m[1][0] - m[0][1]) * srsq;
                q.X = (m[0][2] + m[2][0]) * srsq;
                q.Y = (m[1][2] + m[2][1]) * srsq;
                q.Z = .25 * s;
            }

            return q.Normalize();
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
        template<class _VectorType>
        void ComposeLookAtBasisVectors(_In_ const _VectorType &localUpVector, _In_ const _VectorType &basisForwardVector, _Out_ _VectorType &basisSideVector, _Out_ _VectorType &basisUpVector)
        {
            using Type = typename _VectorType::Type;

            // OutVector is the cross product of UpAxisVector with LookVector
            basisSideVector = CrossProduct(localUpVector, basisForwardVector);
            Type dot = DotProduct(basisSideVector, basisSideVector);
            constexpr Type epsilon(Type(1e-6));
            if (dot < epsilon)
            {
                // basisForwardVector and localUpVector are nearly colinear
                // Choose an arbitrary basisSideVector preserving the sign
                // of the localUpVector
                Type dotForwardUp = DotProduct(basisForwardVector, localUpVector);
                if (dotForwardUp > 0)
                {
                    // Parallel: use default side vector
                    basisSideVector[0] = localUpVector[2];
                    basisSideVector[1] = localUpVector[0];
                    basisSideVector[2] = localUpVector[1];
                }
                else
                {
                    // Anti-parallel: flip the side vector
                    basisSideVector[0] = -localUpVector[2];
                    basisSideVector[1] = localUpVector[0];
                    basisSideVector[2] = -localUpVector[1];
                }
            }
            else
            {
                // Normalize OutVector
                basisSideVector = basisSideVector * Type(1. / std::sqrt(dot));
            }

            // The cross product of basisSideVector with basisForwardVector is the basisUpVector.
            // There is no need to normalize the result since the
            // basisForwardVector and basisSideVector are unit vectors and are 
            // orthogonal. Thus, the cross product must also be a unit vector.
            basisUpVector = CrossProduct(basisForwardVector, basisSideVector);
        }

        using FloatQuaternion = TQuaternion<float>;
        using DoubleQuaternion = TQuaternion<double>;

        //------------------------------------------------------------------------------------------------
        // Axis-Aligned Bounding Box
        struct AABB
        {
            FloatVector4 Min;
            FloatVector4 Max;

            AABB()
                : Min(FLT_MAX, FLT_MAX, FLT_MAX, 0.0f)
                , Max(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0.0f)
            {
            }

            AABB(const FloatVector4& min, const FloatVector4& max)
                : Min(min), Max(max)
            {
            }

            bool IsValid() const
            {
                return Min.V[0] <= Max.V[0] && Min.V[1] <= Max.V[1] && Min.V[2] <= Max.V[2];
            }

            void Reset()
            {
                Min = FloatVector4(FLT_MAX, FLT_MAX, FLT_MAX, 0.0f);
                Max = FloatVector4(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0.0f);
            }

            void ExpandToInclude(const FloatVector4& point)
            {
                if (point.V[0] < Min.V[0]) Min.V[0] = point.V[0];
                if (point.V[1] < Min.V[1]) Min.V[1] = point.V[1];
                if (point.V[2] < Min.V[2]) Min.V[2] = point.V[2];
                if (point.V[0] > Max.V[0]) Max.V[0] = point.V[0];
                if (point.V[1] > Max.V[1]) Max.V[1] = point.V[1];
                if (point.V[2] > Max.V[2]) Max.V[2] = point.V[2];
            }

            void ExpandToInclude(const AABB& other)
            {
                if (!other.IsValid()) return;
                if (!IsValid())
                {
                    *this = other;
                    return;
                }
                ExpandToInclude(other.Min);
                ExpandToInclude(other.Max);
            }

            FloatVector4 GetCenter() const
            {
                return FloatVector4(
                    (Min.V[0] + Max.V[0]) * 0.5f,
                    (Min.V[1] + Max.V[1]) * 0.5f,
                    (Min.V[2] + Max.V[2]) * 0.5f,
                    0.0f
                );
            }

            FloatVector4 GetExtents() const
            {
                return FloatVector4(
                    (Max.V[0] - Min.V[0]) * 0.5f,
                    (Max.V[1] - Min.V[1]) * 0.5f,
                    (Max.V[2] - Min.V[2]) * 0.5f,
                    0.0f
                );
            }
        };


    }
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
