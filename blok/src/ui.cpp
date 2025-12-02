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

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

//#include "window.hpp"
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

UI::UI(const std::shared_ptr<Window>& window) :
	m_window(window),
	m_camera(nullptr),
	m_mouseSetting(DEFAULT),
	frameCount(0),
	averageFps(0.0),
	nextWindowPos(0),
	dt(0)
{
	//ImGuiIO& io = ImGui::GetIO();
	//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	//io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	glfwSetWindowUserPointer(window->getGLFWwindow(), this);
}

blok::UI::~UI()
{
}

void blok::UI::update(float deltatime)
{
	/*framerate stuff*/
	dt = deltatime;
	averageFps += 1 / dt;
	frameCount++;

	nextWindowPos = Vector2(0.0f, 0.0f);
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
			handler->m_camera->processMouse(dx * 5, dy * 5);
		}
	}
}

void UI::swapMouseBehaviour(MouseBehaviour behaviour)
{
	switch (behaviour)
	{
	case (UI::MouseBehaviour::CAMERA_CONTROL):
	{
		glfwSetCursorPosCallback(m_window->getGLFWwindow(), mouseCameraCallback);
		glfwSetInputMode(m_window->getGLFWwindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		break;
	}
	case (UI::MouseBehaviour::DEFAULT):
	default:
	{
		glfwSetCursorPosCallback(m_window->getGLFWwindow(), ImGui_ImplGlfw_CursorPosCallback);
		glfwSetInputMode(m_window->getGLFWwindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		break;
	}
	}
}


void UI::handleCameraControls(Camera* cam)
{
	m_camera = cam;

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_RootAndChildWindows))
	{
		GLFWcursor* handCursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		glfwSetCursor(m_window->getGLFWwindow(), handCursor);


		if (glfwGetMouseButton(m_window->getGLFWwindow(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && m_camera != nullptr)
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

			ImGui::BeginTooltip();
			ImGui::Text("Hold Right Click for Camera Controls");
			ImGui::EndTooltip();
		}
	}
	else
	{
		glfwSetCursor(m_window->getGLFWwindow(), nullptr);
		
		if (m_mouseData.firstMouse == false)
		{
			swapMouseBehaviour(DEFAULT);
			m_mouseData.firstMouse = true;
		}
	}
}

/*void blok::UI::renderToNewWindow(unsigned int texture, std::string windowName)
{
	ImGui::Begin(windowName.c_str());

	ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2((float)m_window->getWidth(), (float)m_window->getHeight()));

	ImGui::End();
}*/

void blok::UI::renderToWindow(unsigned int texture)
{
	ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2((float)m_window->getWidth(), (float)m_window->getHeight()));
}

void blok::UI::displayData()
{
	if (dt == 0) return;


	ImGui::BeginChild("Data Display", ImVec2(200.0f, 100.0f));
	ImGui::SeparatorText("Data Display");
	ImGui::Text("FPS: %f", 1/dt);
	averageFps += 1 / dt;
	ImGui::Text("Average FPS: %f", averageFps/frameCount);


	ImGui::Separator();

	ImGui::EndChild();
}

void blok::UI::beginWindow(std::string windowName)
{
	//ImGui::SetNextWindowPos(ImVec2(nextWindowPos.x, nextWindowPos.y), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(0, 0));// , ImGuiCond_Once);

	ImGui::Begin(windowName.c_str());
}

void blok::UI::endWindow()
{
	//nextWindowPos += Vector2(ImGui::GetWindowSize().x,0.0f);
	ImGui::End();
}

void blok::UI::createButton(std::string windowName, void func())
{
	if (func == nullptr)
	{
		ImGui::Button(windowName.c_str());
	}
	else
	{
		if (ImGui::Button(windowName.c_str()))
		{
			func();
		}
	}
}
