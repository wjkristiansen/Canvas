//================================================================================================
// DataSource
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
template<class _DataType>
class CConstantFunction
{
    const _DataType m_c;
    
public:
    CConstantFunction(float c) :
        m_c(c) {}

    _DataType Evaluate() const { return m_c; }
};

template<class _DataType>
class CMidpointFunction
{
    _DataType Evaluate(_DataType x, _DataType y) const { return (x + y) / 2; }
};

template<class _DataType>
class CKeyedCurveFunction
{
    _DataType Evaluate(_DataType t) { return 0; }
};

//------------------------------------------------------------------------------------------------
template<class _DataType>
class CDataSource
{
public:
    virtual _DataType Evaluate() const = 0;
};

//------------------------------------------------------------------------------------------------
template<class _DataType>
class CMidpointFunctionDataSource : public CDataSource<_DataType>
{
    CDataSource<_DataType> *m_pSourceA = nullptr;
    CDataSource<_DataType> *m_pSourceB = nullptr;

public:
    CMidpointFunctionDataSource(CDataSource<_DataType> *pSourceA, CDataSource<_DataType> *pSourceB) :
        m_pSourceA(pSourceA),
        m_pSourceB(pSourceB)
    {}

    virtual _DataType Evaluate() const final
    {
        return m_pSourceA->Evaluate() + m_pSourceB->Evaluate();
    }
};

//------------------------------------------------------------------------------------------------
class CRotationDataSource : public CDataSource<FloatVector4>
{

};

//------------------------------------------------------------------------------------------------
class CEulerRotationDataSource : public CRotationDataSource
{
    CDataSource<float> *m_pEulerAngles[4] = {};
    FloatVector4 m_DefaultAngles;

public:
    CEulerRotationDataSource(float DefaultRotX, float DefaultRotY, float DefaultRotZ)
        : m_DefaultAngles( DefaultRotX, DefaultRotY, DefaultRotZ, 0 ) {}

    virtual FloatVector4 Evaluate() final
    {
        FloatVector4 Value;

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

class CTranslationDataSource : public CDataSource<FloatVector4>
{

};

