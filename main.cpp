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

#include <cassert>
#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <cstdlib> 
#include <algorithm>
#include <cstdint> //uint32_t
#include <limits> //std::numeric_limits
#include <fstream>

constexpr uint32_t WIDTH = 800; //calculates its value eduring compilation, speeds up runtime execution, unlike const for which initialization can still happen at runtime
constexpr uint32_t HEIGHT = 600;

//Vulkan API is designed around the idea of minimal driver overhead = very limited error checking
//basically custom checks added to the API to check for mistakes like using a new GPU feature and forgetting to request it at logical device creation time
//can be freely stacked to include all debugging functionality you're interested in, and can enable validation layers for debug builds and disable for release builds
const std::vector<char const*> validationLayers = {
	"VK_LAYER_KHRONOS_validation" //enabled by specifying name (this is a bundle of all the useful standard validation)
};

//Advantages of the ___info -> createObject(device, ___info) pattern
//readable configuration: named fields are easier to understand than a long list of positional arguments
//extensibility: many Vulkan structures support a pNext chain for additional feature or extension settings
//explicit control: the driver receives the configuration together, allowing validation and object preparation at creation time

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

//we'll be reading the SPIR-V .spv instead of a text - shader
static std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary); //start reading at the end of the file | read the file as a binary file (no text transformations)

	if (!file.is_open()) {
		throw std::runtime_error{ "failed to open file!" };
	}

	std::vector<char> buffer(file.tellg()); //since we're reading at the end, we can determine the size of the file

	file.seekg(0, std::ios::beg); //seek to the beginning
	file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())); //read file by amoutn size

	file.close();

	return buffer;
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

	uint32_t queueIndex = ~0; //bitwise not, all bits are set to 1

	//needs to be created using HWND and HMODULE on windows
	vk::raii::SurfaceKHR surface = nullptr; //connects Vulkan to the native window system, and GLFW helps create that connection. platform-independent handle to the window's presentation destination, GLFW handles the platform specifid work of creating the surface for our window

	//format and extent for the swap chain images stored as member vars
	vk::raii::SwapchainKHR swapChain = nullptr;
	std::vector<vk::Image> swapChainImages;
	vk::SurfaceFormatKHR swapChainSurfaceFormat;
	vk::Extent2D swapChainExtent;

	std::vector<vk::raii::ImageView> swapChainImageViews; //in order to use any image, we need to create a ImageView object: how to access the image and which part of the iamge to access

	vk::raii::PipelineLayout pipelineLayout = nullptr;

	vk::raii::Pipeline graphicsPipeline = nullptr;
	
	//commands in Vulkan are not executed directly using function calls - you have to record all the operations you want to perform in command buffer objs: advantage - when we're ready to tell Vulkan what we want to do, the commands are submitted together (more efficient porcess of commands since all of them are available together + allows command recording to happen in multiple threads if desired)
	//command pools - manage memory that is used to store the buffers, and command buffers are allocated from them
	vk::raii::CommandPool commandPool = nullptr;

	vk::raii::CommandBuffer commandBuffer = nullptr;


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
		createImageViews();
		createGraphicsPipeline();
		createCommandPool();
		createCommandBuffer();
	}

	//before rendering to an image, we need to transition its layout to one that is suitable for rendering (some are optimized to be presented to the screen, others are optimized for being used as color attachments)
	void transition_image_layout(
								uint32_t imageIndex,
								vk::ImageLayout old_layout,
								vk::ImageLayout new_layout,
								vk::AccessFlags2 src_access_mask,
								vk::AccessFlags2 dst_access_mask,
								vk::PipelineStageFlags2 src_stage_mask,
								vk::PipelineStageFlags2 dst_stage_mask) {
		
		vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = src_stage_mask,
			.srcAccessMask = src_access_mask,
			.dstStageMask = dst_stage_mask,
			.dstAccessMask = dst_access_mask,
			.oldLayout = old_layout,
			.newLayout = new_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = swapChainImages[imageIndex],
			.subresourceRange = {
				   .aspectMask = vk::ImageAspectFlagBits::eColor,
				   .baseMipLevel = 0,
				   .levelCount = 1,
				   .baseArrayLayer = 0,
				   .layerCount = 1} };

		vk::DependencyInfo dependency_info = {
			.dependencyFlags = {},
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &barrier };

		commandBuffer.pipelineBarrier2(dependency_info);
	}

	void recordCommandBuffer(uint32_t imageIndex) {
		commandBuffer.begin({}); //start recording, we must always start with this

		transition_image_layout(
			imageIndex,
			vk::ImageLayout::eUndefined, //old layout
			vk::ImageLayout::eColorAttachmentOptimal,//transitioning image layout to this
			{}, //srcAccessMask(no need to wait for previous operations)
			vk::AccessFlagBits2::eColorAttachmentWrite, //dst AccessMask
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, //srcStage
			vk::PipelineStageFlagBits2::eColorAttachmentOutput //dstStage
		);

		vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
		vk::RenderingAttachmentInfo attachmentInfo = {
			.imageView = swapChainImageViews[imageIndex], //which image view to render to
			.imageLayout = vk::ImageLayout::eColorAttachmentOptimal, // specifies the layout of the image will be in during rendering
			.loadOp = vk::AttachmentLoadOp::eClear, //what to do with th eimage before rendering
			.storeOp = vk::AttachmentStoreOp::eStore, //what to do with the image after rendering
			.clearValue = clearColor };

		vk::RenderingInfo renderingInfo = {
			.renderArea = {.offset = {0,0}, .extent = swapChainExtent}, //size fo the render area (similar to render area in a rener pass)
			.layerCount = 1, //number of layers to render to (1 for non-layered image)
			.colorAttachmentCount = 1,
			.pColorAttachments = &attachmentInfo };

		//start recording rendering session
		commandBuffer.beginRendering(renderingInfo);

		//eGraphics: specifies if graphics or compute pipeline
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline); 

		//we specified viewport and scissor state to be dynamic, so we have to set them in the command buffer before issuing the draw command
		commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
		commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));

		//vertexCount, instanceCount, firstVertex (offset into the vertex buffer - lowest value of SV_VertexId), firstInstance (offset for instanced rendering, lowest value of SV_InstanceID)
		commandBuffer.draw(3, 1, 0, 0);

		//end rendering command recording
		commandBuffer.endRendering();

		//after rendering, transition the swapchain image to vk::ImageLayout::ePresentSrcKHR so it can be presented to the screen
		transition_image_layout(
			imageIndex,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::AccessFlagBits2::eColorAttachmentWrite, //srcAccessMask
			{}, //dstAccessMask
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, //srcStage
			vk::PipelineStageFlagBits2::eBottomOfPipe //dstStage
		);

		commandBuffer.end();
	}

	void createCommandBuffer() {
		vk::CommandBufferAllocateInfo allocInfo{ .commandPool = commandPool, 
												.level = vk::CommandBufferLevel::ePrimary, //ePrimary: can be submitted to a queue for execution but cannot be called from other command buffers / eSecondary - cannot be submitted directly, but can be called from primary commandb uffers
												.commandBufferCount = 1 };

		commandBuffer = std::move(vk::raii::CommandBuffers(device, allocInfo).front()); //comandbuffer consturctor returns a commandBuffer vector, but we only want 1 so we're moving it out to a single variable
	}

	void createCommandPool() {
		//eTransient - Hint that command buffers are rerecorded with new commands very often, eResetCommandBuffer - alolw command buffers to be rerecorded individually, or else they must all be reset together
		vk::CommandPoolCreateInfo poolInfo{ .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = queueIndex };

		commandPool = vk::raii::CommandPool(device, poolInfo);
	}
	
	//nodiscard asks the compiler to warn if you ignore a function's return value (calling a function without assigning return value to anything)
	//must wrap the shader code in a shader module before passing it into the pipeline
	[[nodiscard]]
	vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) {
		//size of bytecode is in bytes, but the bytecoe pointer is a uint32_t (4 bytes). reinterpret_cast interprets a value as another type
		vk::ShaderModuleCreateInfo createInfo{ .codeSize = code.size() * sizeof(char), .pCode = reinterpret_cast<const uint32_t*>(code.data()) }; //specify a pointer to the buffer with the bytecode and length
		vk::raii::ShaderModule shaderModule{ device, createInfo };

		//shader modules are just a thin wrapper around the shader bytecode that we've previously loaded, 
		//compilation and linking of the SPIR-V bytecode to machine code doesn't happen until the graphics pipeline is created -> we're allowed to destroy the shader modules as soon as the pipeline creation is finished
		return shaderModule;
	}

	//Vulkan shader code has to be specified in a bytecode format - SPIR-V, the compilers written by GPU vendors to turn shader code into native code are less complex, no risk of other GPU vendors misinterpreting shader code
	//Khronos has their own vendor-independent compiler that compiles slang to SPIR-V (used via slangc)
	//Slang is a C-style shading language
	//Vulkan NDC has depth [0,1] and top y coord is -1, while bottom is 1
	void createGraphicsPipeline() {
		auto shaderCode = readFile("shaders/slang.spv");
		vk::raii::ShaderModule shaderModule = createShaderModule(shaderCode); //shader modules can be destoryed as soon as the pipeline is created, which is why it's a local var

		///////////////////////////////
		// Shader modules
		///////////////////////////////

		//1 - eVertex = stage we're working in, 2 - specifie sshadermodule containing the code 3 - specifies the function to invoke (entrypoint): possible to combine multiple fragment shaders inot a single shader module and use different entry points to differentiate between behaviors
		vk::PipelineShaderStageCreateInfo vertShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "vertMain" };

		vk::PipelineShaderStageCreateInfo fragShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain" };

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		//pSpecializationInfo allows you to specify values for shader constants - more efficient than configuring shader variables at render time because the compiler can do optimizations like eliminating if statements that depend on these values

		//specifies bindings - spacing between data and whether it's per-vertex or per-instance / attribute descriptions - types of attributse passed ot the vertex sahder, and which binding to laod them from and at which offset
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo; //hard coding vertex data for now

		///////////////////////////////
		// Fixed function setup (only configuring) - dynamic state, input assembly, viewports and scissors, rasterizer, multisampling, depth and stencil testing, colorblending, pipeline layout
		///////////////////////////////

		//what kind of geometry will be drawn and if primitive restart should be enabled - with an element buffer we can specify the indicies ourselves, which allows for reusing vertices
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

		//viewports define the transformation from the image(NDC) to the framebuffer 'coordinates'(pixel, coordiante space of the rendering destination) - we still need this if we're doing dynamic rendering, here it covers the full swapchain image
		//size of the swap chain and it simages may diffeer from widht and height, but we should stick to their size as they'll be used as the color attachments of the framebuffer later on - 0.0 and 1.0 are mindepth, maxdepth (mindepth may be higher than maxdepth but just go with standard for now)
		vk::Viewport viewport{ 0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f }; //describes the region of the framebuffer that the output will be rendered to, almost always be width, height

		//scissors reject fragments before they write to the fraembuffer
		vk::Rect2D scissor{ vk::Offset2D{0,0}, swapChainExtent };  //scissor rectangles define which region of pixels will actually be stored, rasterizer discards any pixels outside the scissored rectangles - like a filter than a transformation

		//specify the count at pipeline creation time - note that our specified viewport and scissor are not connected since we're using dynamic states for these
		vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

		vk::PipelineRasterizationStateCreateInfo rasterizer{ .depthClampEnable = vk::False, //true -> fragments beyond th near and far planes are clamped instead of discarding (useful for shadow maps)
															.rasterizerDiscardEnable = vk::False, //true -> geometry never passes through the rasterizer stage (disables any output to the framebuffer)
															.polygonMode = vk::PolygonMode::eFill, //fill, wireframe, or points
															.cullMode = vk::CullModeFlagBits::eBack, //disable, front, back, or both
															.frontFace = vk::FrontFace::eClockwise, //specifies vertex order for the faces considered front-facing <-> counterclockwise
															.depthBiasEnable = vk::False, //can alter depth values by adding a constant value or biasing them based on a fragment's slope, sometimes used for shadow mapping
															.lineWidth = 1.0f }; //thickness of lines in terms of number of fragments


		//multisampling - one of the ways to perform antialiasing (combines the fragment shader results to multiple polyogns that rasterize to the same pixel)
		vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False }; //doesn't need to run the fragment shader multiple times if only one polygon maps to a pixel - significantly less expensive than simply rendering to a higher res then downscaling

		//no depth/stencil buffer for now

		//colorBlending configuration per attached framebuffer 
		vk::PipelineColorBlendAttachmentState colorBlendAttachment{
			.blendEnable = vk::False,
			.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA };

		//attachment count is color attachments - 1 deferred rendering framebuffer would have something like 3 color attachments (albedo normals worldpositions) and 1 depth attachment
		vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .logicOp = vk::LogicOp::eCopy, .attachmentCount = 1, .pAttachments = &colorBlendAttachment };  //global color blending settings - logicOpEnable should be true to enable bitwise blending, then bitwise operation is specified in the logicOp field, colorWriteMask is used to determine which channels will actually be affected
		
		//uniform values - must be specified in pipeline layout
		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 0, .pushConstantRangeCount = 0 }; //push constants - another way of passing dynamic values to shaders
		pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

		//graphics pipeline: how to draw: shaders, veretx input, rasterization, depth testing, blending etc
		//vkRenderPass: the attachment setup and organization of rendering: attachment formats, load/store operations, subpasses, and their dependencies
		//vkFramebuffer: the particular image views used as attachments with a compatible render pass

		///////////////////////////////
		// Dynamic Rendering
		///////////////////////////////

		//older APIs provided the default state (not stages of the pipeline, which pipeline 'settings' you can supply through commands at draw time instead of fixing them when creating the pipeline) for most of the stages of the graphics pipeline - in Vulkan, you have to be explicit about the pipeline states since they are baked into an immutable pipeline object
		//a limited amount of state (size of the viewport, line width and blend constants) can be changed without recreating the pipeline at draw time
		std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState{ .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data() };

		//dynamic rendering advantages - simplified code with fewer objects to manage - more flexibility in changing attachments during rendering - better compatibility with modern rendering techniques
		//before we had to create framebuffers to bind our image views to a render pass - since vulkan 1.3 we can bypass this and render directly to image views (specify color, depth, and stencil attachments directly when being rendered)
		//to use dynamic rendering, we need to specify the formats of the attachments that will be used during rendering
		vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{ .colorAttachmentCount = 1, .pColorAttachmentFormats = &swapChainSurfaceFormat.format }; //one color attachment, with the format o four swap chain images

		//the actual pipeline: Shader stages + Fixed-function state + Pipeline layout + Dynamic rendering
		vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
			{.stageCount = 2,
			.pStages = shaderStages,
			.pVertexInputState = &vertexInputInfo, //pipelineshaderstagecreateinfo structs
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling,
			.pColorBlendState = &colorBlending,
			.pDynamicState = &dynamicState,
			.layout = pipelineLayout, //fixed function stage
			.renderPass = nullptr }, //nullptr because we're using dynamci rendering instead of a traditional renderpass
			{.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapChainSurfaceFormat.format}
		};
		//basePipelineHandle, basePipelineIndex: vulkan allows you to create a new graphics pipeline by deriving from an existing pipeline - less expensive and switching can be done quicker

		graphicsPipeline = vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()); //nullptr = optional vk::raii::PipelineCache object - can be used to store and resue data relevant to pipeline creation acrsos multiple calls to vk::raii::Pipeline constructors, significant speedup pipeline creation at a later time		
	}

	//viewType, format, subresourceRange are identical for each of the image views
	void createImageViews() {
		assert(swapChainImageViews.empty());

		//max number of image views you should expect graphics cards to handle is 16, there are cases where thousands are needed, but that can also be handled

		vk::ImageViewCreateInfo imageViewCreateInfo{
			.viewType = vk::ImageViewType::e2D, //specify we're rendering on a 2d screen (3d for 3d screen or cube map, 1d screen option exists too)
			.format = swapChainSurfaceFormat.format, //how the colorspace components are configured
			.components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity}, // not necessary as this is just the default, but just to show 
			.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} //describes what the image's purpose is and which part of the image should be accessed, no mipmap, no multiple layers (can also initialize using .designatedinit = value)
		};
		//components field: allows us to swizzle the color chanels around (can map all the channels to the red channel for a monochrome texture, can also map constant values of 0 and 1) we will stick to default mapping
		//you'd use multiple layers for a stereographic 3D application (subresourceRange)

		//loop over all swap chain images and add them to our swapChainImageViews structure
		for (auto& image : swapChainImages) {
			imageViewCreateInfo.image = image;
			swapChainImageViews.emplace_back(device, imageViewCreateInfo); //only update the image component and keep pushing
		}
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
				{}, //no optional vulkan 1.0 features requested
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