//========================================================================
// Windows 11 acrylic for the OpenGL window.
//
// The material is built as a Windows.UI.Composition effect graph, the same
// recipe WinUI's AcrylicBrush uses and therefore the same one Windows Terminal
// shows. It replaces the DWM system backdrop the port used before, which DWM
// renders with its own maths and which cannot be matched to Terminal exactly.
//
// The window gets no redirection surface, and the composition tree holds two
// layers: the acrylic underneath, and the OpenGL output above it in a
// composition swapchain. See win32_acrylic.c for why it has to be that way.
//========================================================================

#pragma once

// Everything here is a no-op unless the window is transparent with blur on.
// Failure at any step leaves the window on the plain path with no acrylic,
// never in a half-built state.
bool _glfwWin32AcrylicCreate(_GLFWwindow* window);
void _glfwWin32AcrylicDestroy(_GLFWwindow* window);
bool _glfwWin32AcrylicActive(_GLFWwindow* window);

// Tint colour and opacity as kitty configured them, before any WinUI discount.
// The effect graph applies that discount itself.
void _glfwWin32AcrylicSetTint(_GLFWwindow* window, unsigned int rgb, float opacity);

// Must run with the GL context current. Registers the swapchain back buffer
// with GL and binds it, so drawing aimed at framebuffer 0 lands in it.
bool _glfwWin32AcrylicBindFramebuffer(_GLFWwindow* window);

void _glfwWin32AcrylicResize(_GLFWwindow* window, int width, int height);
void _glfwWin32AcrylicPresent(_GLFWwindow* window);
