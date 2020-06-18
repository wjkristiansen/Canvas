//================================================================================================
// TimeLine
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CTimeline
{
public:
    enum class Mode
    {
        Limited,
        Repeating,
        Reversing,
    };

protected:
    double m_BaseTime;
    float m_Duration;
    Mode m_Mode;

public:
    float Evaluate(double ActualTime)
    {
        float Delta = float(ActualTime - m_BaseTime);
        switch (m_Mode)
        {
        case Mode::Limited:
            return min(Delta, m_Duration);
            break;

        case Mode::Repeating:
            return std::fmod(Delta, m_Duration);
            break;

        case Mode::Reversing:
            return m_Duration - std::abs(std::fmod(Delta, m_Duration * 2) - m_Duration);
            break;
        }
    }
};

