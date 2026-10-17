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
#include <algorithm>
#include <cstdint> //uint32_t
#include <limits> //std::numeric_limits

constexpr uint32_t WIDTH = 800; //calculates its value eduring compilation, speeds up runtime execution, unlike const for which initialization can still happen at runtime
constexpr uint32_t HEIGHT = 600;

//Vulkan API is designed around the idea of minimal driver overhead = very limited error checking
//basically custom checks added to the API to check for mistakes like using a new GPU feature and forgetting to request it at logical device creation time
//can be freely stacked to include all debugging functionality you're interested in, and can enable validation layers for debug builds and disable for release builds
const std::vector<char const*> validationLayers = {
	"VK_LAYER_KHRONOS_validation" //enabled by specifying name (this is a bundle of all the useful standard validation)
};

#ifdef NDEBUG //not debug, c++ macro
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

//returns a list of instance extensions
std::vector<const char*> getRequiredinstanceExtensions() {
	uint32_t glfwExtensionCount = 0;
	auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

//validation layerse will print debu gmessages to standard output by default, but we can also handle them ourselves with an explicit callback -> allows you to decide which kind of messages you'd like to see
if (enableValidationLayers) {
	extensions.push_back(vk::EXTDebugUtilsExtensionName); //extension that sets up debug messenger with a callback (equal to "VK_EXT_debug_utils" string)
}

return extensions;
}

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

	vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr; //tell vulkan about the callback function, you can have as many as you want

	vk::raii::PhysicalDevice physicalDevice = nullptr; //the graphics card that we'll select will be stored here

	std::vector<const char*> requiredDeviceExtension = { vk::KHRSwapchainExtensionName }; //list of required extensions for required extension check during physical device check, KHRSwapchain is needed to present rendered images to the window

	vk::raii::Device device = nullptr; //logical device

	vk::raii::Queue queue = nullptr; //interface to handle queues which are automatically created along with the logical device, implicitly cleaned up when the device is destroyed

	//needs to be created using HWND and HMODULE on windows
	vk::raii::SurfaceKHR surface = nullptr; //connects Vulkan to the native window system, and GLFW helps create that connection. platform-independent handle to the window's presentation destination, GLFW handles the platform specifid work of creating the surface for our window

	//format and extent for the swap chain images stored as member vars
	vk::raii::SwapchainKHR swapChain = nullptr;
	std::vector<vk::Image> swapChainImages;
	vk::SurfaceFormatKHR swapChainSurfaceFormat;
	vk::Extent2D swapChainExtent;

	void initWindow() {
		//first call should be glfwInit() - initializes the GLFW library (it's originally designed to create an OpenGL context, so we need to tell it not to create an OpenGL context)
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); //telling GLFW not to create OpenGL context
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); //disable handling resized windows - takes special care

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr); //optionally specify a monitor, and last parameter is only relevant to OpenGL

	}

	void initVulkan() {
		createInstance(); //initializing the vulkan library by creating an instance - it is the connection between the application and the vulkan library
		setupDebugMessenger();
		createSurface();
		pickPhysicalDevice();
		createLogicalDevice();
		createSwapChain();
	}

	//resolution of the swap chain images, almost exactly equal to the resolution of the window we're drawing to,
	//but some managers allow us to differ using max uint32_t -> we'll pick the resolution that best mathces the window within minImageExtent and maxImageExtent bounds
	vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities) {
		//pixel size and screen coordinates may not be the same in cases like apple's retin adisplay (higher pixel density, resolution of the window inpixel is larger than screen coord resolution

		//currentExtent is only set to the special undefined value when the window manager lets us chosoe the extent ourselves, any other value means the surface already dictates a fixed extent
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return capabilities.currentExtent;
		}
		int width, height;
		glfwGetFramebufferSize(window, &width, &height); //query the resolution of the window in pixel

		return { std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
				std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
	}

	vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes) {
		//eMailbox (triple buffering) is the best if energy usage is not an issue (non-mobile)
		assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
		return std::ranges::any_of(availablePresentModes,
			[](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ? //if any supports eMailbox, then return eMailbox, if not default to eFifo
			vk::PresentModeKHR::eMailbox :
			vk::PresentModeKHR::eFifo;
	}

	//choosing the right settings for the best possible swap chain. considerations: 1. surface format (color depth) 2. presentaiton mode (conditions for swapping images to the screen) 3. swap extent (resolution of images in swapchain)
	vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats) {
		//SurfaceFormatKHR contains .format(specifies the color channels and types) and .colorSpace(indicates if the sRGB color space is supported or not)
		const auto formatIt = std::ranges::find_if(
			availableFormats,
			[](const auto& format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; }); //find one that supports common color format and srgb

		return formatIt != availableFormats.end() ? *formatIt : availableFormats[0]; //if nothing exists, we can start ranking, but it's usually good enough to use the first one
	}
	
	//simply sticking to the minimum means we have to sometimes wait for the driver to complete internal operations before acquiring another image to render to, in practice, it's common to request a few more images than the strict min
	uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities) {
		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount); //min image count is at least 3
		if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)){
			minImageCount = surfaceCapabilities.maxImageCount;
		}

		return minImageCount;
	}

	void createSwapChain() {
		//checking if a swap chain is available is not enough, it must also be compatible with our window surface
		auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface); //1. basic surface capabilities (min/max number of images in swap chain, min/max width and height of images)
		swapChainExtent = chooseSwapExtent(surfaceCapabilities);  //resolution
		uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities); //number of images in swapchain

		std::vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface);
		swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

		std::vector<vk::PresentModeKHR> availablePresentModes = physicalDevice.getSurfacePresentModesKHR(*surface); //this doesn't require member func to store value?

		//Structure to create a swap chain object
		vk::SwapchainCreateInfoKHR swapChainCreateInfo{
			.surface = *surface,
			.minImageCount = minImageCount,
			.imageFormat = swapChainSurfaceFormat.format,
			.imageColorSpace = swapChainSurfaceFormat.colorSpace,
			.imageExtent = swapChainExtent,
			.imageArrayLayers = 1, //specifies the number of layers each image consists of, always 1 unless you're developing a stereoscopic 3D application
			.imageUsage = vk::ImageUsageFlagBits::eColorAttachment, //specifies what operations we'll use the images for, we'll be rendering directly to them, different value for post processing
			.imageSharingMode = vk::SharingMode::eExclusive, // an image is owned by one queue family at a time, ownership must be explicity transferred before using it in another family -> best performance <-> eConcurrent
			.preTransform = surfaceCapabilities.currentTransform, // specify that a certain transform should be applied to images in the swap chain if it is supported (90 deg rotation or horizontal flip), currentTrans means no transformation
			.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque, //specifies if the alpha channel should be used for blending
			.presentMode = chooseSwapPresentMode(availablePresentModes),
			.clipped = true //if true we don't care about the color of pixels that are obscured(another window is in front of them)
		};

		swapChainCreateInfo.oldSwapchain = nullptr; //it's possible that our swap chain becomes invalid or unoptimized during runtime (resizing of window), and the swap chain has to be recreated from scratch, and a reference to the old one must be specified here
	

		swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo); //logical device and swap chain creation info
		swapChainImages = swapChain.getImages(); //retrieving the handles of the vk::Image objects that the swapchain contains
	}

	void createSurface() {
		//KHR stands for Khronos
		VkSurfaceKHR _surface; //GLFW only deals with the Vulkan C API, but it can be prompted to the C++ wrapper
		if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != 0) {
			throw std::runtime_error("failed to create window surface!");
		}
		surface = vk::raii::SurfaceKHR(instance, _surface);

		//not every device in the system may support window system integration -> we need to extend createLogicalDevice to ensure that a device can presnt images to the surface we created
		//presentation is a queue-specific feature, so we must find a queue family that supports presenting to the surface and also graphics commands
	}

	void createLogicalDevice() {
		//we checked for a graphics-capable queue family during physical-device selection
		//now we request a queue from that family when creating the logical device
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
		
		uint32_t queueIndex = ~0; //bitwise not, all bits are set to 1
		for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++) {
			if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) && physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface)) { //also must be surface compatible
				queueIndex = qfpIndex;
				break;
			}
		}
		if (queueIndex == ~0) {
			throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
		}

		//Vulkan lets you assign prioriites to queues to influence the scheduling of the command buffer execution (0.0 - 1.0)
		float queuePriority = 0.5f;
		vk::DeviceQueueCreateInfo deviceQueueCreateInfo{ 
			.queueFamilyIndex = queueIndex, 
			.queueCount = 1, 
			.pQueuePriorities = &queuePriority }; //describes the number of queues we want for a single queue family

		//query for vulkan 1.3 features
		//Vulkan is backwards compatible (by default, we only have access to the basic features from Vulkan 1.0, to use newer features, we need to explicitly request them -> Structure Chaining to enable multiple sets of features
		vk::StructureChain<vk::PhysicalDeviceFeatures2,
			vk::PhysicalDeviceVulkan11Features,
			vk::PhysicalDeviceVulkan13Features,
			vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = { //for enabling each structs data while optionally enabling functionality controlled by feature switches
				{}, //still gets basic functionality
				{.shaderDrawParameters = true}, //using this feature from PhysicalDeviceVulkan11Features
				{.dynamicRendering = true},
				{.extendedDynamicState = true}
		};

		//requirements are similar because logical device interface has to be compatible with physical device
		//logical devices don't interact directly with instances, which is why it's not included as a parameter
		vk::DeviceCreateInfo deviceCreateInfo{
			.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &deviceQueueCreateInfo,
			.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()), //these extensions are device-specific unlike the extensions in vk::InstanceCreateInfo
			.ppEnabledExtensionNames = requiredDeviceExtension.data()
		};

		device = vk::raii::Device(physicalDevice, deviceCreateInfo);
		queue = vk::raii::Queue(device, queueIndex, 0);
	}

	bool isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice) {
		bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= vk::ApiVersion13;

		//almost every operation in vulkan, from drawing to uplaoding textures requires commands to be submitted to a queue
		//there are different types of queues from different queue families, and each family of queues only allows a small subset of commands (some only allow compute commands and other only allow memory transfer commands)
		auto queueFamilies = physicalDevice.getQueueFamilyProperties();
		bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const& qfp) {return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); }); //looking for queues that support graphics commands

		//required extension check (currently we're only using vk::KHRSwapchainExtensionName)
		auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
		bool supportsAllRequiredExtensions = 
		std::ranges::all_of(requiredDeviceExtension,
			[&availableDeviceExtensions](auto const& requiredDeviceExtension)
			{
				return std::ranges::any_of(availableDeviceExtensions,
					[requiredDeviceExtension](auto const& availableDeviceExtension)
					{ return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0; });
			});

		//required feature check
		auto features = physicalDevice.template getFeatures2 <vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
		bool supportsrequiredFeatures = features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
										features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
										features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

		return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsrequiredFeatures;
	}

	//after initializing the Vulkan library, we need to look for and select a graphics card that supports the features we need, we can choose as many as we want
	void pickPhysicalDevice() {
		std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();

		auto const devIter = std::ranges::find_if(physicalDevices, [&](auto const& physicalDevice) { return isDeviceSuitable(physicalDevice); });
		if (devIter == physicalDevices.end()) {
			throw std::runtime_error("failed to find a suitable GPU!");
		}

		physicalDevice = *devIter;
	}

	void setupDebugMessenger() {
		if (!enableValidationLayers) return;

		vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError); //all severity types we'd like our callback to be called for
		vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation); //filter the types of messages the callback is notified about
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{.messageSeverity = severityFlags, .messageType = messageTypeFlags, .pfnUserCallback = &debugCallback }; //specifies the pointer to the callback function

		debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
	}

	//a debug callback function
	//returns a boolean that indicates whether the call that triggered the validation layer should be aborted, if true -> abort (but normally only used to test validation layers themselves, so it should always return false
	static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback( //VKAPI_ATTR and VKAPI_CALL ensure that the function has the right signature for Vulkan to call it
		vk::DebugUtilsMessageSeverityFlagBitsEXT severity,//setup in a way that you can use a comparison operation to check if a message is equal or worse to some level of severity
		vk::DebugUtilsMessageTypeFlagsEXT type, //eGeneral: unrelated to specification/performance, eValidation: violates the specification or indicates a possible mistake, ePerformance: potential non-optmial use of Vulkan
		const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, //contains details of the message itself(debug message as a string, array of vulkan object handles related to the message, number of objects in the array)
		void* pUserData) { // contains a pointer specified during the setup of the callback and allows you to pass your own data to it

		if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning || severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
			std::cerr << "valdiation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
		}

		return vk::False;
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
		// 1. define app info
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
		// uint32_t glfwExtensionCount = 0;
		// -> replaced by step 4-5

		// 2. build requiredlayers
		std::vector<char const*> requiredLayers;
		if (enableValidationLayers) {
			requiredLayers.assign(validationLayers.begin(), validationLayers.end());
		}

		// 3. Check if the required layers are supported by the Vulkan implementation
		auto layerProperties = context.enumerateInstanceLayerProperties();
		auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
			[&layerProperties](auto const& requiredLayer) {return std::ranges::none_of(layerProperties,
				[requiredLayer](auto const& layerProperty) {return strcmp(layerProperty.layerName, requiredLayer) == 0;
				});
			});
		if (unsupportedLayerIt != requiredLayers.end()) {
			throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
		}

		// 4. build requiredExtensions
		//must distinguish between instance and device extensions
		//get the required instance extensions
		auto requiredExtensions = getRequiredinstanceExtensions();

		// 5. check extension support
		//check if the required extensions are supported by the Vulkan implementation
		auto extensionProperties = context.enumerateInstanceExtensionProperties();
		auto unsupportedPropertyIt = std::ranges::find_if(requiredExtensions, [&extensionProperties](auto const& requiredExtension) { return std::ranges::none_of(extensionProperties,
			[requiredExtension](auto const& extensionProperty) {return strcmp(extensionProperty.extensionName, requiredExtension) == 0; });
			});
		if (unsupportedPropertyIt != requiredExtensions.end()) {
			throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
		}

		// 6. fill create info including request layers and extensions
		//Not optional - tells the Vulkan driver which global extensions and validation layers we want to use, global means they apply to the entire program and not a specific device
		vk::InstanceCreateInfo createInfo{
			.pApplicationInfo = &appInfo, //points to the appInfo
			.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
			.ppEnabledLayerNames = requiredLayers.data(),
			.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
			.ppEnabledExtensionNames = requiredExtensions.data()
		};

		//finally create the vulkan instance
		instance = vk::raii::Instance(context, createInfo);
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