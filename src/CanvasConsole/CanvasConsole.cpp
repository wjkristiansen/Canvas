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

	float X = Float2C.X();
	float Y = Float2C.Y();
    float Dot = CanvasMath::DotProduct(Float2A, Float2B);
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
