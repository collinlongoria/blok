/*
* File: app
* Project: blok
* Author: Julia Moraes
* Created on: 9/12/2025
*/

#include "UI.hpp"

#include <imgui.h>
//#include "backends/imgui_impl_glfw.h"
//#include "backends/imgui_impl_opengl3.h"

#include <iostream>

void addWindow()
{
	ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
	ImGui::Begin("Controls");

	ImGui::Text("OpenGL Window");

	if (ImGui::Selectable("Selectable")) std::cout << "PRESSED!" << std::endl;

	if (ImGui::Button("Select"))
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
	}
	ImGui::SetItemTooltip("Default Mouse Settings");

	if (ImGui::Button("Move"))
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
	ImGui::SetItemTooltip("Move Camera");

	ImGui::Button("Draw");
	ImGui::SetItemTooltip("Draw Terrain");

	ImGui::Button("Erase");
	ImGui::SetItemTooltip("Erase Terrain");

	

	ImGui::End();
}
