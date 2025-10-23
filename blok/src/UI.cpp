/*
* File: app
* Project: blok
* Author: Julia Moraes
* Created on: 9/12/2025
*/

#include "ui.hpp"

#include <imgui.h>
#include "backends/imgui_impl_glfw.h"
//#include "backends/imgui_impl_opengl3.h"

#include "camera.hpp"
#include <iostream>

using namespace blok;

void addWindow()
{
	//ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	//ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

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

	if (ImGui::IsWindowHovered())
	{
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
	else
	{
		//ImGui::GetIO().ConfigFlags &= !ImGuiConfigFlags_NoMouseCursorChange;
	}

	/* Mouse Cursor changes must be made through glfw
	*                           GLFWcursor* handCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
                          glfwSetCursor(win, handCursor);
	*/

	ImGui::End();
}

UI::UI(GLFWwindow* window) :
	m_window(window),
	m_camera(nullptr),
	m_mouseSetting(DEFAULT)
{
	glfwSetWindowUserPointer(window, this);
}

blok::UI::~UI()
{
}

void UI::mouseCameraCallback(GLFWwindow* window, double xpos, double ypos) {
	
	auto* handler = static_cast<UI*>(glfwGetWindowUserPointer(window));
	if (handler)
	{
		if (handler->m_mouseData.firstMouse) {
			handler->m_mouseData.lastX = (float)xpos;
			handler->m_mouseData.lastY = (float)ypos;
			handler->m_mouseData.firstMouse = false;
		}

		float dx = (float)xpos - handler->m_mouseData.lastX;
		float dy = handler->m_mouseData.lastY - (float)ypos;
		handler->m_mouseData.lastX = (float)xpos;
		handler->m_mouseData.lastY = (float)ypos;

		if (handler->m_camera != nullptr)
		{
			handler->m_camera->processMouse(dx * 2, dy * 2);
		}
	}
}

void UI::swapMouseBehaviour(MouseBehaviour behaviour)
{
	switch (behaviour)
	{
	case (UI::MouseBehaviour::CAMERA_CONTROL):
	{
		glfwSetCursorPosCallback(m_window, mouseCameraCallback);
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		break;
	}
	case (UI::MouseBehaviour::DEFAULT):
	default:
	{
		glfwSetCursorPosCallback(m_window, ImGui_ImplGlfw_CursorPosCallback);
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		break;
	}
	}
}


void UI::handleCameraControls(Camera* cam)
{
	m_camera = cam;

	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_RootAndChildWindows))
	{
		GLFWcursor* handCursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		glfwSetCursor(m_window, handCursor);


		if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && m_camera != nullptr)
		{
			if (m_mouseData.firstMouse == true)
			{
				swapMouseBehaviour(CAMERA_CONTROL);
			}		
		}
		else
		{
			if (m_mouseData.firstMouse == false)
			{
				swapMouseBehaviour(DEFAULT);
			}

			m_mouseData.firstMouse = true;
		}
	}
	else
	{
		glfwSetCursor(m_window, nullptr);
		
		if (m_mouseData.firstMouse == false)
		{
			swapMouseBehaviour(DEFAULT);
			m_mouseData.firstMouse = true;
		}
	}
}
