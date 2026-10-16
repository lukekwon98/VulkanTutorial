#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) //picks between 2 ways of pulling in the Vulkan-Hpp API, both of which provide the same functions, structures, and enumerations
#include <vulkan/vulkan_raii.hpp> //traditional header
#else
import vulkan_hpp; //if C++20 modules is enabled, we import this instead
#endif //eitherh way, VS and VS Code's intellisenes engine doesn't yet understand import for modules

//to create window. Vulkan can work without this for off-screen rendering
//window management, context creation (manages opengl, vulkan rendering contexts), input handling (keyboard, mouse, joystick), cross-platform
//GLFW creates our window and later creates a Vulkan surface, which connects Vulkan to that window, vulkan uses C API types for this, even though this application uses the C++ wrappers
#define  GLFW_INCLUDE_VULKAN //Tells GLFW to include the Vulkan C API headers when the compiler processes it. GLFW_INCLUDE_NONE tells it not to include graphics API headers automatically
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib> 

constexpr uint32_t WIDTH = 800; //calculates its value eduring compilation, speeds up runtime execution, unlike const for which initialization can still happen at runtime
constexpr uint32_t HEIGHT = 600;

class HelloTriangleApplication {
public:
	void run() {
		initWindow();
		initVulkan();
		mainLoop();
		cleanup();
	}

private:

	GLFWwindow* window = nullptr; //actual window

	void initWindow() {
		//first call should be glfwInit() - initializes the GLFW library (it's originally designed to create an OpenGL context, so we need to tell it not to create an OpenGL context)
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); //telling GLFW not to create OpenGL context
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); //disable handling resized windows - takes special care

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr); //optionally specify a monitor, and last parameter is only relevant to OpenGL

	}

	void initVulkan() {

	}

	void mainLoop() {
		while (!glfwWindowShouldClose(window)) { //checks for events like pressing the X button until the user has closed the window
			glfwPollEvents();
		}


	}

	void cleanup() {
		glfwDestroyWindow(window);

		glfwTerminate(); //terminates the library - deallocating glfwINit()
	}
	
	void createInstance() {

	}
};

//vulkan objects are created directly with vkCreate or allocated using vkAllocate / vkDestroy, vkFree
//pAllocator: optional parameter that allows you to specify callbacks for a custom memory allocator -> ignored in the tutorial, will always pass nullptr
int main() {
	try {
		HelloTriangleApplication app;
		app.run();
	}
	catch (const std::exception& e){
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

//Without Vulkan RAII
//VkInstance instance;
//VkApplicationInfo appInfo{};
//appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
//appInfo.pApplicationName = "Hello Triangle";
//appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
//appInfo.pEngineName = "No Engine";
//appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
//appInfo.apiVersion = VK_API_VERSION_1_0;
//
//VkInstanceCreateInfo createInfo{};
//createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
//createInfo.pApplicationInfo = &appInfo;
//createInfo.enabledExtensionCount = 0;
//createInfo.ppEnabledExtensionNames = nullptr;
//createInfo.enabledLayerCount = 0;
//createInfo.ppEnabledLayerNames = nullptr;
//
//if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
//	throw std::runtime_error("failed to create instance!");
//}
//
//vkDestroyInstance(instance, nullptr);

//With Vulkan RAII
//constexpr vk::ApplicationInfo appInfo{ .pApplicationName = "Hello Triangle",
//									  .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
//									  .pEngineName = "No Engine",
//									  .engineVersion = VK_MAKE_VERSION(1, 0, 0),
//									  .apiVersion = vk::ApiVersion14 };
//
//vk::InstanceCreateInfo createInfo{
//	.pApplicationInfo = &appInfo
//};
//
//instance = vk::raii::Instance(context, createInfo);