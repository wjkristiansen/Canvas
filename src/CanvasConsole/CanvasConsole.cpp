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
	CanvasMath::TFloatVector2 Float2A(1.0f, 2.0f);
	CanvasMath::TFloatVector2 Float2B(3.0f, 3.0f);
	CanvasMath::TFloatVector2 Float2C = Float2A + Float2B;

    CanvasMath::TDoubleVector3 d3a(2.2, 3.3, 4.4);
    CanvasMath::TDoubleVector3 d3b(3.0, 4.0, 5.0);

	float X = Float2C.X();
	float Y = Float2C.Y();
    float Dot = CanvasMath::DotProduct(Float2A, Float2B);
    auto Cross = CanvasMath::CrossProduct(d3a, d3b);
}
