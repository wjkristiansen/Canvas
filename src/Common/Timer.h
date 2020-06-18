//================================================================================================
// Timer
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
// High-resolution timer class
class CTimer
{
    static UINT64 m_Frequency;
    static double m_InvFreq;

public:
    CTimer()
    {
        // m_Frequency only needs to be initialized once for the 
        // whole process.
        if (m_Frequency == 0)
        {
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            m_Frequency = frequency.QuadPart;
            m_InvFreq = 1.0 / frequency.QuadPart;
        }
    }

    // Returns the number of ticks per second
    inline UINT64 Frequency() const { return m_Frequency; }

    // Returns the current tick count
    inline UINT64 Now()
    {
        LARGE_INTEGER result;
        QueryPerformanceCounter(&result);
        return result.QuadPart;
    }

    // Conversion helper functions
    static UINT64 Milliseconds(UINT64 Ticks) { return Ticks * 1000 / m_Frequency; }
    static UINT64 Microseconds(UINT64 Ticks) { return Ticks * 1000000 / m_Frequency; }
    static UINT64 Nanoseconds(UINT64 Ticks) { return Ticks * 1000000000 / m_Frequency; }
    static double DPSeconds(UINT64 Ticks) { return Ticks * m_InvFreq; }
};