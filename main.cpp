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
	//raii makes memory management easier, contains c++ wrapper classes that manage resource lifetimes using constructors and desturctors
	vk::raii::Context context; //loads vulkan entry points so you can create the instance, doesn't contain the entire rendering setup
	vk::raii::Instance instance = nullptr; //connects the application to vulkan and allows discovering of physical GPUs

	GLFWwindow* window = nullptr; //actual window

	void initWindow() {
		//first call should be glfwInit() - initializes the GLFW library (it's originally designed to create an OpenGL context, so we need to tell it not to create an OpenGL context)
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); //telling GLFW not to create OpenGL context
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); //disable handling resized windows - takes special care

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr); //optionally specify a monitor, and last parameter is only relevant to OpenGL

	}

	void initVulkan() {
		createInstance(); //initializing the vulkan library by creating an instance - it is the connectino between the application and the vulkan library
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

	// general object creation function parameters pattern:
	// 1. pointer to the device, instance, or context on which the constructor depends
	// 2. pointer to struct with createion info
	// 3. pointer to custom allocator callbacks, (the thing we always ignore)
	// 4. returns the pointer to the raii constructed object
	void createInstance() {
		//A lot of info is apssed in through structs instead of function parameters
		//Optional data, but it may provide useful info to the driver to optimize (it uses a well known graphics engine with certain special behavior)
		constexpr vk::ApplicationInfo appInfo{ .pApplicationName = "Hello Triangle",
												.applicationVersion = VK_MAKE_VERSION(1,0,0),
												.pEngineName = "No Engine",
												.engineVersion = VK_MAKE_VERSION(1,0,0),
												.apiVersion = vk::ApiVersion14 }; //14 is 1.4 10(1.0) does exist, but it doesn't work well with RAII or slang

		//an array of layers being requested, with an array of the desired global extensions
		// Vulkan is platform-agnostic, so we need an extension to interface with the window system, GLFW has a built in function that returns the extensions it needs for that
		// Get the required instance extensions from GLFW
		uint32_t glfwExtensionCount = 0;
		auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		// Check if the required GLFW extensions are supported by the Vulkan implementation
		auto extensionProperties = context.enumerateInstanceExtensionProperties();
		for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
			if (std::ranges::none_of(extensionProperties,
				[glfwExtension = glfwExtensions[i]](auto const& extensionProperty) { return strcmp(extensionProperty.extensionName, glfwExtension) == 0; })) {

				throw std::runtime_error("Required GLFW extension not supported: " + std::string(glfwExtensions[i]));
			}
		}
		
		//Not optional - tells the Vulkan driver which global extensions and validation layers we want to use, global means they apply to the entire program and not a specific device
		vk::InstanceCreateInfo createInfo{
			.pApplicationInfo = &appInfo, //points to the appInfo
			.enabledExtensionCount = glfwExtensionCount,
			.ppEnabledExtensionNames = glfwExtensions 
		};

		//finally create the vulkan instance
		instance = vk::raii::Instance(context, createInfo);

		//returns a vector of the available extensions
		auto extensions = context.enumerateInstanceExtensionProperties();

		std::cout << "available extensions" << std::endl;
		for (const auto& extension : extensions) {
			std::cout << "\t" << extension.extensionName << '\n';
		}

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