// CanvasConsole.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>

float DoVectorThing(float v[2])
{
    return v[0] + v[1];
}

int main()
{
	Canvas::FloatVector2 fv2a(1.0f, 2.0f);
	Canvas::FloatVector2 fv2b(3.0f, 3.0f);
	Canvas::FloatVector2 fv2c = fv2a + fv2b;

    Canvas::DoubleVector3 dv3a(2.2, 3.3, 4.4);
    Canvas::DoubleVector3 dv3b(3.0, 4.0, 5.0);
    Canvas::DoubleVector3 dv3c = dv3b - dv3a;

	float X = fv2c.X();
	float Y = fv2c.Y();
    float Dot = Canvas::DotProduct(fv2a, fv2b);
    auto Cross = Canvas::CrossProduct(dv3a, dv3b);

    Canvas::TMatrix<float, 4, 4> fm44a(
        1.1f, 1.2f, 1.3f, 1.4f,
        2.1f, 2.2f, 2.3f, 2.4f,
        3.1f, 3.2f, 3.3f, 3.4f,
        4.1f, 4.2f, 4.3f, 4.4f
    );
    Canvas::TVector<float, 4> fv4a(3.1f, 4.2f, 5.3f, 6.4f);
    auto fv4b = fv4a * fm44a;
}
