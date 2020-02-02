//================================================================================================
// DataSource
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
template<class _DataType>
class TConstantFunction
{
    const _DataType m_c;

public:
    TConstantFunction(_DataType c) :
        m_c(c) {}

    _DataType Evaluate() const { return m_c; }
};

template<class _DataType>
class TMidpointFunction
{
    _DataType Evaluate(_DataType x, _DataType y) const { return (x + y) / 2; }
};

template<class _DataType>
class TKeyedCurveFunction
{
    _DataType Evaluate(_DataType t) { return 0; }
};

//------------------------------------------------------------------------------------------------
template<class _DataType>
class TDataSource
{
public:
    virtual _DataType Evaluate() const = 0;
};

//------------------------------------------------------------------------------------------------
template<class _DataType>
class TMidpointFunctionDataSource : public TDataSource<_DataType>
{
    TDataSource<_DataType> *m_pSourceA = nullptr;
    TDataSource<_DataType> *m_pSourceB = nullptr;

public:
    TMidpointFunctionDataSource(TDataSource<_DataType> *pSourceA, TDataSource<_DataType> *pSourceB) :
        m_pSourceA(pSourceA),
        m_pSourceB(pSourceB)
    {}

    virtual _DataType Evaluate() const final
    {
        return m_pSourceA->Evaluate() + m_pSourceB->Evaluate();
    }
};

//------------------------------------------------------------------------------------------------
class CRotationDataSource : public TDataSource<Math::FloatVector4>
{

};

//------------------------------------------------------------------------------------------------
class CEulerRotationDataSource : public CRotationDataSource
{
    TDataSource<float> *m_pEulerAngles[4] = {};
    Math::FloatVector4 m_DefaultAngles;

public:
    CEulerRotationDataSource(float DefaultRotX, float DefaultRotY, float DefaultRotZ)
        : m_DefaultAngles(DefaultRotX, DefaultRotY, DefaultRotZ, 0) {}

    virtual Math::FloatVector4 Evaluate() final
    {
        Math::FloatVector4 Value;

        for (int i = 0; i < 4; ++i)
        {
            if (m_pEulerAngles[i])
            {
                Value[i] = m_pEulerAngles[i]->Evaluate();
            }
            else
            {
                Value[i] = m_DefaultAngles[i];
            }
        }

        return Value;
    }
};

//------------------------------------------------------------------------------------------------
class CQuaternionRotationDataSource : public CRotationDataSource
{

};

class CTranslationDataSource : public TDataSource<Math::FloatVector4>
{

};
