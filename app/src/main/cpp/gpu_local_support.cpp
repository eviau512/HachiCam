#include "local_support.h"
#include "gpu_compute.h"
#include "gpu_policy.h"
#include <opencv2/core/version.hpp>
namespace fusion {
namespace {thread_local std::string computeTrace;thread_local GpuGlobalStats globalStats;}
void resetGpuDiagnostics(){computeTrace.clear();globalStats={};}
void recordFusionDiagnostic(const std::string& line){if(computeTrace.size()+line.size()+1<=16352){computeTrace+=line;computeTrace+='\n';}else if(computeTrace.find("[trace truncated]")==std::string::npos)computeTrace+="[trace truncated]\n";}
std::string gpuDiagnostics(){return computeTrace;}
GpuGlobalStats gpuGlobalStats(){return globalStats;}
static void traceDecision(const GpuCostProfile& profile,const char* stage,int key) {
    const auto line=profile.describe(stage,key);
    if(computeTrace.size()+line.size()+1<=16352){computeTrace+=line;computeTrace+='\n';}
    else if(computeTrace.find("[trace truncated]")==std::string::npos)computeTrace+="[trace truncated]\n";
}
}
#ifndef HACHICAM_VULKAN_SUPPORT
namespace fusion {
bool gpuGlobalECC(const cv::Mat&,const cv::Mat&,const cv::Mat&,int,cv::TermCriteria,double,cv::Mat&,double&,std::string& reason){reason="Vulkan backend not built";return false;}
bool gpuLocalSupport(const cv::Mat&,const cv::Mat&,const LocalRegistration&,cv::Mat&,std::string& reason,const cv::Mat&) {
    reason="Vulkan backend not built"; return false;
}
bool gpuPatchFits(const cv::Mat&,const cv::Mat&,const cv::Mat&,const cv::Mat&,const std::vector<cv::Rect>&,bool,
    std::vector<cv::Vec4f>&,std::string& reason,double){reason="Vulkan backend not built";return false;}
bool gpuFittingAvailable(size_t,bool){return false;}
bool gpuPhysicalChecks(const cv::Mat&,const cv::Mat&,const cv::Mat&,const cv::Mat&,const std::vector<PhysicalPatch>&,bool,std::vector<cv::Vec2d>&,GpuPhysicalStats&,std::string& reason){reason="Vulkan backend not built";return false;}
bool gpuStrongSelection(const cv::Mat&,const cv::Mat&,const cv::Mat&,const cv::Mat&,const cv::Mat&,
    const std::vector<cv::Rect>&,const std::vector<cv::Vec2d>&,std::vector<uchar>&,GpuSelectionStats& stats,std::string& reason){stats={};reason="Vulkan backend not built";return false;}
bool gpuNoiseGrid(const cv::Mat&,int,const cv::Mat&,cv::Mat&,std::string& reason){reason="Vulkan backend not built";return false;}
bool gpuMergeAnchors(const std::vector<SamplingPlate>&,const cv::Mat&,const cv::Rect&,cv::Mat&,long long&,long long&,GpuComputeStats&,std::string& reason){reason="Vulkan backend not built";return false;}
}
#else
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "shaders/local_support_spv.h"
#include "shaders/local_ecc_spv.h"
#include "shaders/noise_grid_spv.h"
#include "shaders/color_merge_spv.h"
#include "shaders/strong_selection_spv.h"
#include "shaders/global_ecc_spv.h"
#include "shaders/physical_check_spv.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>
#include <array>
#include <cstdlib>
#include <mutex>
#include <cstring>
#include <limits>
#include <chrono>
#include <android/log.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fusion { namespace {
constexpr VkDeviceSize LIMIT=64ULL*1024*1024;
int sizeBucket(size_t pixels){return pixels<=1000000 ? 0 : pixels<=4000000 ? 1 : 2;}
#ifdef HACHICAM_GPU_TEST_CONTROL
bool testFault(const char* phase) {const auto fault=std::getenv("HACHICAM_GPU_TEST_FAULT");return fault && std::strcmp(fault,phase)==0;}
#else
bool testFault(const char*){return false;}
#endif
cv::Vec4f cpuPatchFit(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,const cv::Mat& validity,const cv::Rect& patch) {
    try {
        cv::Mat mask=localFitMask(raw,validity,patch);
        if(cv::countNonZero(mask)<256)return {0,0,-1,0};
        cv::Mat transform=cv::Mat::eye(2,3,CV_32F);
        const double cc=cv::findTransformECC(ref(patch),src(patch),transform,cv::MOTION_TRANSLATION,
            cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.0001),mask,3);
        const float dx=transform.at<float>(0,2),dy=transform.at<float>(1,2);
        return !std::isfinite(cc) || cc<.65 || std::hypot(dx,dy)>8 ? cv::Vec4f(0,0,-1,0) : cv::Vec4f(dx,dy,float(cc),1);
    } catch(const cv::Exception&){return {0,0,-1,0};}
}
cv::Mat cpuNoiseGrid(const cv::Mat& gray,int spacing,const cv::Mat& active) {
    const int nx=(gray.cols-1+spacing-1)/spacing+1,ny=(gray.rows-1+spacing-1)/spacing+1;
    cv::Mat output=cv::Mat::zeros(ny,nx,CV_64F);
    #pragma omp parallel for schedule(dynamic)
    for(int y=0;y<ny;++y)for(int x=0;x<nx;++x) {
        if(!active.empty() && !active.at<uchar>(y,x))continue;
        const int cx=std::min(x*spacing,gray.cols-1),cy=std::min(y*spacing,gray.rows-1);
        const cv::Rect roi(std::max(0,cx-160),std::max(0,cy-160),
            std::min(gray.cols,cx+161)-std::max(0,cx-160),std::min(gray.rows,cy+161)-std::max(0,cy-160));
        output.at<double>(y,x)=estimateNoiseSigma(gray(roi));
    }
    return output;
}
struct Context {
#define FN(n) PFN_##n n=nullptr
    FN(vkGetInstanceProcAddr); FN(vkCreateInstance); FN(vkDestroyInstance);
    FN(vkEnumeratePhysicalDevices); FN(vkGetPhysicalDeviceProperties);
    FN(vkGetPhysicalDeviceQueueFamilyProperties); FN(vkGetPhysicalDeviceMemoryProperties);
    FN(vkCreateDevice); FN(vkGetDeviceProcAddr); FN(vkDestroyDevice); FN(vkGetDeviceQueue);
    FN(vkCreateShaderModule); FN(vkDestroyShaderModule); FN(vkCreateDescriptorSetLayout);
    FN(vkDestroyDescriptorSetLayout); FN(vkCreatePipelineLayout); FN(vkDestroyPipelineLayout);
    FN(vkCreateComputePipelines); FN(vkDestroyPipeline); FN(vkCreateCommandPool);
    FN(vkDestroyCommandPool); FN(vkAllocateCommandBuffers); FN(vkResetCommandBuffer);
    FN(vkBeginCommandBuffer); FN(vkEndCommandBuffer); FN(vkCmdBindPipeline);
    FN(vkCmdBindDescriptorSets); FN(vkCmdPushConstants); FN(vkCmdDispatch); FN(vkCmdPipelineBarrier);
    FN(vkCreateDescriptorPool); FN(vkDestroyDescriptorPool); FN(vkAllocateDescriptorSets);
    FN(vkUpdateDescriptorSets); FN(vkCreateBuffer); FN(vkDestroyBuffer);
    FN(vkGetBufferMemoryRequirements); FN(vkAllocateMemory); FN(vkFreeMemory);
    FN(vkBindBufferMemory); FN(vkMapMemory); FN(vkUnmapMemory); FN(vkCreateFence);
    FN(vkDestroyFence); FN(vkResetFences); FN(vkWaitForFences); FN(vkQueueSubmit);
#undef FN
    VkInstance instance=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    VkShaderModule shader=VK_NULL_HANDLE;
    VkShaderModule eccShader=VK_NULL_HANDLE;
    VkShaderModule noiseShader=VK_NULL_HANDLE,mergeShader=VK_NULL_HANDLE;
    VkPipeline noisePipeline=VK_NULL_HANDLE,mergePipeline=VK_NULL_HANDLE;
    VkShaderModule physicalShader=VK_NULL_HANDLE;std::array<VkPipeline,2> physicalStages{};GpuCostProfile physicalPolicy[18];
    VkShaderModule selectionShader=VK_NULL_HANDLE;
    std::array<VkPipeline,2> selectionStages{};
    VkShaderModule globalShader=VK_NULL_HANDLE;std::array<VkPipeline,7> globalStages{};
    bool globalAttempted=false;GpuCostProfile globalPolicy[6];
    VkDescriptorSetLayout descriptorLayout=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkPipeline eccPipeline=VK_NULL_HANDLE;
    std::array<VkPipeline,8> eccStages{};
    VkCommandPool commands=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;
    VkDescriptorPool descriptors=VK_NULL_HANDLE;
    VkDescriptorSet descriptor=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceProperties properties{};
    struct Buffer { VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
        VkDeviceSize capacity=0, allocation=0; void* mapped=nullptr; } buffers[3];
    bool healthy=false, inFlight=false;
    bool eccReady=false;
    GpuCostProfile bucketPolicy[6],noisePolicy[6],mergePolicy[9],fitPolicy[18],selectionPolicy[3];
    int fitScopeSkip[2]{0,0};
    // New count-bound returns must not numerically quarantine established converged proposals.
    int boundedFitState[18]{}; // 0 unknown, 1 admitted, 2 CPU completion only
    std::string failure="uninitialized";
    std::mutex guard;
    Context() {
        try { healthy=initialize(); }
        catch(const std::exception&) { healthy=false;failure="Vulkan initialization host failure"; }
        if(!healthy)cleanup();
    }
    // Process-lifetime cache; unresolved in-flight work is quarantined, never freed.
    template<typename F> bool instanceFunction(F& fn,const char* name) {
        fn=reinterpret_cast<F>(vkGetInstanceProcAddr(instance,name));return fn!=nullptr;
    }
    template<typename F> bool deviceFunction(F& fn,const char* name) {
        fn=reinterpret_cast<F>(vkGetDeviceProcAddr(device,name));return fn!=nullptr;
    }
    bool error(const char* phase,VkResult code) {
        healthy=false;
        failure=std::string(phase)+" result="+std::to_string(int(code));return false;
    }
    bool initialize() {
        if(testFault("loader")){failure="injected unavailable loader";return false;}
#ifdef _WIN32
        const auto library=LoadLibraryW(L"vulkan-1.dll");
        if(library)vkGetInstanceProcAddr=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library,"vkGetInstanceProcAddr"));
#else
        const auto library=dlopen("libvulkan.so",RTLD_NOW|RTLD_LOCAL);
        if(library)vkGetInstanceProcAddr=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr"));
#endif
        if(!vkGetInstanceProcAddr){failure="Vulkan loader unavailable";return false;}
        if(!instanceFunction(vkCreateInstance,"vkCreateInstance")){failure="instance entry unavailable";return false;}
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="HachiCam compute";app.apiVersion=VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
        auto r=vkCreateInstance(&ci,nullptr,&instance);if(r!=VK_SUCCESS)return error("instance",r);
#define I(n) if(!instanceFunction(n,#n)){failure="missing " #n;return false;}
        I(vkDestroyInstance);I(vkEnumeratePhysicalDevices);I(vkGetPhysicalDeviceProperties);
        I(vkGetPhysicalDeviceQueueFamilyProperties);I(vkGetPhysicalDeviceMemoryProperties);
        I(vkCreateDevice);I(vkGetDeviceProcAddr);
#undef I
        uint32_t count=0;r=vkEnumeratePhysicalDevices(instance,&count,nullptr);
        if(r!=VK_SUCCESS || count==0 || count>64)return error("enumerate devices",r);
        std::vector<VkPhysicalDevice> devices(count);
        r=vkEnumeratePhysicalDevices(instance,&count,devices.data());if(r!=VK_SUCCESS)return error("devices",r);
        uint32_t family=0;
        for(auto candidate:devices) {
            if(testFault("software"))continue;
            VkPhysicalDeviceProperties p{};vkGetPhysicalDeviceProperties(candidate,&p);
            const bool hardware=p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
                p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || p.deviceType==VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
            if(!hardware || p.limits.maxComputeWorkGroupInvocations<128 ||
                p.limits.maxComputeWorkGroupSize[0]<128 || p.limits.maxPerStageDescriptorStorageBuffers<3 ||
                p.limits.maxPushConstantsSize<32 || p.limits.maxComputeSharedMemorySize<8192)continue;
            uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);
            if(n==0 || n>128)continue;
            std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,families.data());
            for(uint32_t j=0;j<n;++j)if(families[j].queueCount && (families[j].queueFlags&VK_QUEUE_COMPUTE_BIT)) {
                physical=candidate;properties=p;family=j;break;
            }
            if(physical)break;
        }
        if(!physical){failure="no qualified hardware compute device";return false;}
        vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        float priority=1;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&q;
        r=vkCreateDevice(physical,&dc,nullptr,&device);if(r!=VK_SUCCESS)return error("device",r);
#define D(n) if(!deviceFunction(n,#n)){failure="missing " #n;return false;}
        D(vkDestroyDevice);D(vkGetDeviceQueue);D(vkCreateShaderModule);D(vkDestroyShaderModule);
        D(vkCreateDescriptorSetLayout);D(vkDestroyDescriptorSetLayout);D(vkCreatePipelineLayout);
        D(vkDestroyPipelineLayout);D(vkCreateComputePipelines);D(vkDestroyPipeline);
        D(vkCreateCommandPool);D(vkDestroyCommandPool);D(vkAllocateCommandBuffers);
        D(vkResetCommandBuffer);D(vkBeginCommandBuffer);D(vkEndCommandBuffer);
        D(vkCmdBindPipeline);D(vkCmdBindDescriptorSets);D(vkCmdPushConstants);D(vkCmdDispatch);D(vkCmdPipelineBarrier);
        D(vkCreateDescriptorPool);D(vkDestroyDescriptorPool);D(vkAllocateDescriptorSets);D(vkUpdateDescriptorSets);
        D(vkCreateBuffer);D(vkDestroyBuffer);D(vkGetBufferMemoryRequirements);D(vkAllocateMemory);
        D(vkFreeMemory);D(vkBindBufferMemory);D(vkMapMemory);D(vkUnmapMemory);
        D(vkCreateFence);D(vkDestroyFence);D(vkResetFences);D(vkWaitForFences);D(vkQueueSubmit);
#undef D
        vkGetDeviceQueue(device,family,0,&queue);
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=local_support_spv_size;sm.pCode=local_support_spv;
        r=vkCreateShaderModule(device,&sm,nullptr,&shader);if(r!=VK_SUCCESS)return error("shader",r);
        VkDescriptorSetLayoutBinding bindings[3]{};
        for(int i=0;i<3;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=3;dl.pBindings=bindings;
        r=vkCreateDescriptorSetLayout(device,&dl,nullptr,&descriptorLayout);if(r!=VK_SUCCESS)return error("descriptor layout",r);
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,32};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&descriptorLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;
        r=vkCreatePipelineLayout(device,&pl,nullptr,&layout);if(r!=VK_SUCCESS)return error("pipeline layout",r);
        VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pc.layout=layout;
        pc.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pc.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pc.stage.module=shader;pc.stage.pName="main";
        if(testFault("pipeline"))return error("injected pipeline",VK_ERROR_INITIALIZATION_FAILED);
        r=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pc,nullptr,&pipeline);if(r!=VK_SUCCESS)return error("compute pipeline",r);
        sm.codeSize=local_ecc_spv_size;sm.pCode=local_ecc_spv;
        r=vkCreateShaderModule(device,&sm,nullptr,&eccShader);if(r!=VK_SUCCESS)return error("ECC shader",r);
        pc.stage.module=eccShader;
        const VkSpecializationMapEntry specializationEntry{0,0,sizeof(int)};
        for(int stage:{0,1,4,5,6,7}) {
            const VkSpecializationInfo specialization{1,&specializationEntry,sizeof(stage),&stage};
            pc.stage.pSpecializationInfo=&specialization;
            r=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pc,nullptr,&eccStages[stage]);
            if(r!=VK_SUCCESS)return error("ECC specialized pipeline",r);
        }
        eccPipeline=eccStages[1];
        pc.stage.pSpecializationInfo=nullptr;
        for(int i=0;i<2;++i) {
            sm.codeSize=i ? color_merge_spv_size : noise_grid_spv_size;
            sm.pCode=i ? color_merge_spv : noise_grid_spv;
            auto& module=i ? mergeShader : noiseShader;
            auto& compute=i ? mergePipeline : noisePipeline;
            r=vkCreateShaderModule(device,&sm,nullptr,&module);if(r!=VK_SUCCESS)return error("additional shader",r);
            pc.stage.module=module;
            r=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pc,nullptr,&compute);if(r!=VK_SUCCESS)return error("additional pipeline",r);
        }
        sm.codeSize=physical_check_spv_size;sm.pCode=physical_check_spv;
        r=vkCreateShaderModule(device,&sm,nullptr,&physicalShader);if(r!=VK_SUCCESS)return error("physical shader",r);
        pc.stage.module=physicalShader;
        for(int stage=0;stage<2;++stage) {
            const VkSpecializationInfo info{1,&specializationEntry,sizeof(stage),&stage};pc.stage.pSpecializationInfo=&info;
            r=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pc,nullptr,&physicalStages[stage]);if(r!=VK_SUCCESS)return error("physical pipeline",r);
        }
        sm.codeSize=strong_selection_spv_size;sm.pCode=strong_selection_spv;
        r=vkCreateShaderModule(device,&sm,nullptr,&selectionShader);if(r!=VK_SUCCESS)return error("selector shader",r);
        pc.stage.module=selectionShader;
        for(int stage=0;stage<2;++stage) {
            const VkSpecializationInfo specialization{1,&specializationEntry,sizeof(stage),&stage};pc.stage.pSpecializationInfo=&specialization;
            r=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pc,nullptr,&selectionStages[stage]);if(r!=VK_SUCCESS)return error("selector pipeline",r);
        }
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=family;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        r=vkCreateCommandPool(device,&pool,nullptr,&commands);if(r!=VK_SUCCESS)return error("command pool",r);
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=commands;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
        r=vkAllocateCommandBuffers(device,&ca,&command);if(r!=VK_SUCCESS)return error("command buffer",r);
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=1;dp.pPoolSizes=&ps;
        r=vkCreateDescriptorPool(device,&dp,nullptr,&descriptors);if(r!=VK_SUCCESS)return error("descriptor pool",r);
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=descriptors;da.descriptorSetCount=1;da.pSetLayouts=&descriptorLayout;
        r=vkAllocateDescriptorSets(device,&da,&descriptor);if(r!=VK_SUCCESS)return error("descriptor set",r);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        r=vkCreateFence(device,&fi,nullptr,&fence);if(r!=VK_SUCCESS)return error("fence",r);
        healthy=true;
        const bool gateReady=selfTest();
        if(gateReady)eccReady=selfTestFit();
        return gateReady && healthy && selfTestCompute();
    }
    void release(Buffer& b) {
        if(b.mapped && vkUnmapMemory)vkUnmapMemory(device,b.memory);
        if(b.buffer && vkDestroyBuffer)vkDestroyBuffer(device,b.buffer,nullptr);
        if(b.memory && vkFreeMemory)vkFreeMemory(device,b.memory,nullptr);
        b={};
    }
    void cleanup() {
        if(inFlight)return;
        for(auto& b:buffers)release(b);
        if(device) {
#define X(fn,obj) if(obj && fn)fn(device,obj,nullptr)
            X(vkDestroyFence,fence);X(vkDestroyDescriptorPool,descriptors);X(vkDestroyCommandPool,commands);
            X(vkDestroyPipeline,pipeline);
            X(vkDestroyPipeline,noisePipeline);X(vkDestroyPipeline,mergePipeline);
            X(vkDestroyShaderModule,noiseShader);X(vkDestroyShaderModule,mergeShader);
            for(auto stage:selectionStages)if(stage && vkDestroyPipeline)vkDestroyPipeline(device,stage,nullptr);
            X(vkDestroyShaderModule,selectionShader);
            for(auto stage:physicalStages)if(stage && vkDestroyPipeline)vkDestroyPipeline(device,stage,nullptr);X(vkDestroyShaderModule,physicalShader);
            for(auto stage:globalStages)if(stage && vkDestroyPipeline)vkDestroyPipeline(device,stage,nullptr);
            X(vkDestroyShaderModule,globalShader);
            for(auto stage:eccStages)if(stage && vkDestroyPipeline)vkDestroyPipeline(device,stage,nullptr);
            X(vkDestroyShaderModule,eccShader);X(vkDestroyPipelineLayout,layout);
            X(vkDestroyDescriptorSetLayout,descriptorLayout);X(vkDestroyShaderModule,shader);
#undef X
            if(!vkDestroyDevice)return; // Broken loader: retain the child context rather than destroy its parent.
            vkDestroyDevice(device,nullptr);device=VK_NULL_HANDLE;
        }
        if(instance && vkDestroyInstance)vkDestroyInstance(instance,nullptr);instance=VK_NULL_HANDLE;
    }
    bool reserve(int index,VkDeviceSize bytes) {
        if(testFault("allocation"))return error("injected allocation",VK_ERROR_OUT_OF_DEVICE_MEMORY);
        auto& b=buffers[index];if(b.capacity>=bytes)return true;
        release(b);
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=bytes;bc.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;bc.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        auto r=vkCreateBuffer(device,&bc,nullptr,&b.buffer);if(r!=VK_SUCCESS)return error("buffer",r);
        VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,b.buffer,&req);
        VkDeviceSize allocated=req.size;for(const auto& other:buffers)allocated+=other.allocation;
        if(allocated>LIMIT){release(b);failure="GPU pool budget";return false;}
        uint32_t type=std::numeric_limits<uint32_t>::max();
        constexpr auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        int best=-1;
        for(uint32_t i=0;i<memory.memoryTypeCount;++i) {
            const auto available=memory.memoryTypes[i].propertyFlags;
            if(!(req.memoryTypeBits&(1u<<i)) || (available&flags)!=flags)continue;
            const int score=((available&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? 2 : 0)+
                ((available&VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? 1 : 0);
            if(score>best){type=i;best=score;}
        }
        if(type==std::numeric_limits<uint32_t>::max()){release(b);failure="no coherent host memory type";return false;}
        VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=type;
        r=vkAllocateMemory(device,&ma,nullptr,&b.memory);if(r!=VK_SUCCESS){release(b);return error("allocate",r);}
        r=testFault("bind") ? VK_ERROR_OUT_OF_DEVICE_MEMORY : vkBindBufferMemory(device,b.buffer,b.memory,0);if(r!=VK_SUCCESS){release(b);return error("bind",r);}
        r=testFault("map") ? VK_ERROR_MEMORY_MAP_FAILED : vkMapMemory(device,b.memory,0,req.size,0,&b.mapped);if(r!=VK_SUCCESS){release(b);return error("map",r);}
        b.capacity=bytes;b.allocation=req.size;
        __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU buffer=%d allocationBytes=%llu flags=%u poolBytes=%llu",
            index,static_cast<unsigned long long>(req.size),memory.memoryTypes[type].propertyFlags,
            static_cast<unsigned long long>(allocated));
        return true;
    }
    bool workspace(const VkDeviceSize* sizes) {
        if(inFlight || !healthy){failure="GPU workspace unavailable";return false;}
        VkDeviceSize retained=0;
        for(int i=0;i<3;++i)retained+=std::max(sizes[i],buffers[i].allocation);
        if(retained>LIMIT)for(auto& b:buffers)release(b);
        for(int i=0;i<3;++i) {
            if(!sizes[i] || sizes[i]>properties.limits.maxStorageBufferRange){failure="GPU storage limit";return false;}
            if(!reserve(i,sizes[i]))return false;
        }
        return true;
    }
    bool dispatch(VkPipeline selected,const VkDeviceSize* sizes,const std::array<int,8>& shape,uint32_t groups) {
        VkDescriptorBufferInfo bi[3]{};VkWriteDescriptorSet writes[3]{};
        for(int i=0;i<3;++i){bi[i]={buffers[i].buffer,0,sizes[i]};writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=descriptor;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&bi[i];}
        vkUpdateDescriptorSets(device,3,writes,0,nullptr);
        auto r=vkResetCommandBuffer(command,0);if(r!=VK_SUCCESS)return error("reset command",r);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        r=vkBeginCommandBuffer(command,&begin);if(r!=VK_SUCCESS)return error("begin",r);
        VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};before.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;before.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,nullptr,0,nullptr);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,selected);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&descriptor,0,nullptr);
        if(selected==globalStages[0] || selected==globalStages[3]) {
            VkMemoryBarrier dependency{VK_STRUCTURE_TYPE_MEMORY_BARRIER};dependency.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;dependency.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            auto stage=[&](int number,uint32_t count){vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,globalStages[number]);vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,shape.data());vkCmdDispatch(command,count,1,1);vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&dependency,0,nullptr,0,nullptr);};
            if(selected==globalStages[0]){stage(0,groups);stage(1,groups);stage(2,groups);}
            else{stage(3,groups);stage(4,1);stage(5,groups);stage(6,14);}
        } else if(selected==eccPipeline) {
            auto constants=shape;
            VkMemoryBarrier dependency{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            dependency.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            dependency.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            auto stage=[&](int number,bool tiled) {
                constants[3]=number;
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,eccStages[number]);
                vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,constants.data());
                vkCmdDispatch(command,tiled ? uint32_t(shape[4]) : groups,tiled ? groups : 1,1);
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,1,&dependency,0,nullptr,0,nullptr);
            };
            stage(0,false);stage(6,true);stage(7,true);
            for(int iteration=0;iteration<shape[1];++iteration) {
                stage(1,true);stage(4,false);
            }
            stage(5,false);
        } else if(selected==physicalStages[0]) {
            vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,shape.data());
            vkCmdDispatch(command,uint32_t(shape[2]),groups,1);
            VkMemoryBarrier dependency{VK_STRUCTURE_TYPE_MEMORY_BARRIER};dependency.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            dependency.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&dependency,0,nullptr,0,nullptr);
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,physicalStages[1]);vkCmdDispatch(command,groups,1,1);
        } else if(selected==selectionStages[0]) {
            vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,shape.data());
            vkCmdDispatch(command,uint32_t(shape[4]),groups,1);
            VkMemoryBarrier dependency{VK_STRUCTURE_TYPE_MEMORY_BARRIER};dependency.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            dependency.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&dependency,0,nullptr,0,nullptr);
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,selectionStages[1]);vkCmdDispatch(command,groups,1,1);
        } else {
            vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,32,shape.data());vkCmdDispatch(command,groups,1,1);
        }
        VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};after.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,nullptr,0,nullptr);
        r=vkEndCommandBuffer(command);if(r!=VK_SUCCESS)return error("end",r);
        r=vkResetFences(device,1,&fence);if(r!=VK_SUCCESS)return error("reset fence",r);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        if(testFault("submit"))return error("injected submit",VK_ERROR_DEVICE_LOST);
        inFlight=true;
        r=vkQueueSubmit(queue,1,&submit,fence);
        if(r!=VK_SUCCESS){healthy=false;return error("submission quarantine",r);}
        r=vkWaitForFences(device,1,&fence,VK_TRUE,2000000000ULL);
        // Host-only policy test: actual work is completed before simulating an
        // unresolved fence, so the test cannot deliberately hang a driver.
        if(r==VK_SUCCESS && testFault("timeout"))r=VK_TIMEOUT;
        if(r!=VK_SUCCESS){healthy=false;return error("completion quarantine",r);}
        inFlight=false;
        return true;
    }
    bool ensureGlobal() {
        if(globalAttempted)return globalStages[0]!=VK_NULL_HANDLE;
        globalAttempted=true;
        if(testFault("global_shared") || properties.limits.maxComputeSharedMemorySize<16384){failure="global shared-memory capability unavailable";return false;}
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=global_ecc_spv_size;sm.pCode=global_ecc_spv;
        auto r=vkCreateShaderModule(device,&sm,nullptr,&globalShader);if(r!=VK_SUCCESS){failure="global shader unavailable";return false;}
        VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pc.layout=layout;
        pc.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pc.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pc.stage.module=globalShader;pc.stage.pName="main";
        const VkSpecializationMapEntry entry{0,0,sizeof(int)};
        for(int stage=0;stage<7;++stage){
            const VkSpecializationInfo info{1,&entry,sizeof(stage),&stage};pc.stage.pSpecializationInfo=&info;
            r=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pc,nullptr,&globalStages[stage]);
            if(r!=VK_SUCCESS){for(auto& value:globalStages){if(value)vkDestroyPipeline(device,value,nullptr);value=VK_NULL_HANDLE;}failure="global pipeline unavailable";return false;}
        }
        cv::Mat test(73,97,CV_8U),result;cv::RNG rng(61983);rng.fill(test,cv::RNG::UNIFORM,30,225);
        double cc=-1;int iterations=0;const cv::Mat identity=cv::Mat::eye(2,3,CV_32F);
        if(!runGlobal(test,test,identity,cv::MOTION_EUCLIDEAN,{cv::TermCriteria::COUNT|cv::TermCriteria::EPS,4,1e-5},result,cc,iterations) || cc<.9999 || cv::norm(identity,result,cv::NORM_INF)>.001){
            if(!inFlight)for(auto& value:globalStages){if(value)vkDestroyPipeline(device,value,nullptr);value=VK_NULL_HANDLE;}
            failure="global startup conformance unavailable";return false;
        }
        return true;
    }
#include "global_ecc_host.inl"
    bool run(const cv::Mat& ref,const cv::Mat& src,const LocalRegistration& model,cv::Mat& output,const cv::Mat& validity=cv::Mat()) {
        const size_t count=ref.total(), words=(count+3)/4, plane=words*4;
        const VkDeviceSize sizes[]{(model.patchSupport ? 3 : 2)*plane,model.field.total()*16,plane};
        if(sizes[0]>properties.limits.maxStorageBufferRange || (words+127)/128>properties.limits.maxComputeWorkGroupCount[0]){failure="storage/dispatch limit";return false;}
        if(!workspace(sizes))return false;
        auto* input=static_cast<unsigned char*>(buffers[0].mapped);
        for(int y=0;y<ref.rows;++y){std::memcpy(input+size_t(y)*ref.cols,ref.ptr(y),ref.cols);std::memcpy(input+plane+size_t(y)*ref.cols,src.ptr(y),src.cols);}
        if(plane>count){std::memset(input+count,0,plane-count);std::memset(input+plane+count,0,plane-count);}
        if(model.patchSupport) {
            std::memset(input+2*plane,255,plane);
            if(!validity.empty())for(int y=0;y<ref.rows;++y)std::memcpy(input+2*plane+size_t(y)*ref.cols,validity.ptr(y),ref.cols);
        }
        auto* fields=static_cast<float*>(buffers[1].mapped);
        for(int y=0;y<model.field.rows;++y)for(int x=0;x<model.field.cols;++x){const auto p=model.field.at<cv::Vec3f>(y,x);const size_t i=size_t(y*model.field.cols+x)*4;fields[i]=p[0];fields[i+1]=p[1];fields[i+2]=p[2];fields[i+3]=0;}
        const std::array<int,8> shape{ref.cols,ref.rows,int(words),model.field.cols,model.field.rows,model.spacing,int(count),int(model.patchSupport)};
        if(!dispatch(pipeline,sizes,shape,uint32_t((words+127)/128)))return false;
        output.create(ref.size(),CV_8U);const auto* bytes=static_cast<const uchar*>(buffers[2].mapped);
        bool invalid=false;
        #pragma omp parallel for reduction(||:invalid)
        for(int y=0;y<ref.rows;++y)for(int x=0;x<ref.cols;++x){const auto value=bytes[size_t(y)*ref.cols+x];
            if(value!=0 && value!=255 && value!=128){invalid=true;continue;}
            output.at<uchar>(y,x)=value==128 ? supportPixel(ref,src,model,x,y,model.patchSupport,validity) : value;
        }
        if(invalid){failure="invalid shader output";return false;}
        return true;
    }
    bool runFits(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,const cv::Mat& validity,
                 const std::vector<cv::Rect>& patches,std::vector<cv::Vec4f>& results,int iterations=30,int* countLimited=nullptr,bool keepBoundedTags=false) {
        if(patches.empty() || patches.size()>16){failure="unsupported fitting batch";return false;}
        std::vector<uint32_t> packed;
        size_t pixels=0,largest=0;
        for(const auto& roi:patches){pixels+=roi.area();largest=std::max(largest,size_t(roi.area()));}
        packed.reserve(patches.size()*4+pixels);packed.resize(patches.size()*4);
        size_t floatOffset=0;
        for(size_t index=0;index<patches.size();++index) {
            const auto& roi=patches[index];
            const uint32_t offset=uint32_t(floatOffset),n=uint32_t(roi.area());
            cv::Mat mask=localFitMask(raw,validity,roi);
            if(cv::countNonZero(mask)<256)mask.setTo(0);
            packed[index*4]=offset;packed[index*4+1]=uint32_t(roi.width);
            packed[index*4+2]=uint32_t(roi.height);packed[index*4+3]=n;
            for(int y=0;y<roi.height;++y) {
                const auto* t=ref.ptr<uchar>(roi.y+y)+roi.x;
                const auto* source=src.ptr<uchar>(roi.y+y)+roi.x;
                const auto* m=mask.ptr<uchar>(y);
                for(int x=0;x<roi.width;++x)packed.push_back(uint32_t(t[x]) | uint32_t(source[x])<<8 | uint32_t(m[x])<<16);
            }
            floatOffset+=size_t(n)*5;
        }
        const size_t chunks=(largest+1023)/1024;
        const VkDeviceSize sizes[]{floatOffset*sizeof(float),packed.size()*sizeof(uint32_t),patches.size()*(3+4*chunks)*16};
        for(auto size:sizes)if(size>properties.limits.maxStorageBufferRange){failure="fitting storage range";return false;}
        if(chunks>properties.limits.maxComputeWorkGroupCount[0] || patches.size()>properties.limits.maxComputeWorkGroupCount[1]){failure="fitting dispatch limit";return false;}
        if(!workspace(sizes))return false;
        std::memcpy(buffers[1].mapped,packed.data(),sizes[1]);
        std::array<int,8> shape{int(patches.size()),iterations,0,0,int(chunks),0,0,0};const float epsilon=.0001f;
        std::memcpy(&shape[2],&epsilon,sizeof(float));
        if(!dispatch(eccPipeline,sizes,shape,uint32_t(patches.size())))return false;
        results.resize(patches.size());static_assert(sizeof(cv::Vec4f)==16);
        std::memcpy(results.data(),buffers[2].mapped,results.size()*sizeof(cv::Vec4f));
        int bounded=0;
        for(auto& fit:results) {
            if((fit[3]!=1 && fit[3]!=3) || !std::isfinite(fit[0]) || !std::isfinite(fit[1]) || !std::isfinite(fit[2]) || fit[2]<.65f || std::hypot(fit[0],fit[1])>8)
                fit={0,0,-1,0};
            else {bounded+=fit[3]==3;if(!keepBoundedTags)fit[3]=1;}
        }
        if(countLimited)*countLimited=bounded;
        if(bounded)__android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU ECC count-limit v1 returned=%d patches=%zu iterations=%d; proposal only",bounded,patches.size(),iterations);
        return true;
    }
    bool runNoise(const cv::Mat& gray,int spacing,const cv::Mat& active,cv::Mat& output) {
        const int nx=(gray.cols-1+spacing-1)/spacing+1,ny=(gray.rows-1+spacing-1)/spacing+1;
        const VkDeviceSize sizes[]{((gray.total()+3)/4)*4,size_t(nx*ny)*4,size_t(nx*ny)*12};
        if(uint32_t(nx*ny)>properties.limits.maxComputeWorkGroupCount[0] || !workspace(sizes))return false;
        auto* bytes=static_cast<uchar*>(buffers[0].mapped);
        std::memset(bytes,0,size_t(sizes[0]));
        for(int y=0;y<gray.rows;++y)std::memcpy(bytes+size_t(y)*gray.cols,gray.ptr(y),gray.cols);
        auto* enabled=static_cast<uint32_t*>(buffers[1].mapped);
        for(int i=0;i<nx*ny;++i)enabled[i]=active.empty() || active.at<uchar>(i/nx,i%nx) ? 1 : 0;
        std::memset(buffers[2].mapped,0,size_t(sizes[2]));
        if(!dispatch(noisePipeline,sizes,{gray.cols,gray.rows,nx,ny,spacing,0,0,0},uint32_t(nx*ny)))return false;
        output=cv::Mat::zeros(ny,nx,CV_64F);const auto* medians=static_cast<const uint32_t*>(buffers[2].mapped);
        for(int i=0;i<nx*ny;++i) {
            const auto m=std::max({medians[3*i],medians[3*i+1],medians[3*i+2]});
            if(m>510){failure="invalid GPU noise median";return false;}
            output.at<double>(i/nx,i%nx)=std::clamp(double(m)*.5/.67448975,0.,24.);
        }
        return true;
    }
    bool runMergeTile(const std::vector<SamplingPlate>& plates,const cv::Mat& ref,
                      const cv::Rect& tile,cv::Mat& output,long long& contributors,long long& multi,long long& ambiguous) {
        std::vector<uint32_t> images,meta(plates.size()*24,0);
        auto writeFloat=[&](size_t i,float v){std::memcpy(&meta[i],&v,4);};
        for(size_t index=0;index<plates.size();++index) {
            const auto& plate=plates[index];const auto& h=plate.inverse;
            const size_t m=index*24;
            const double denominator=h(2,0)*tile.x+h(2,1)*tile.y+h(2,2);
            if(!std::isfinite(denominator)||std::abs(denominator)<1e-6){failure="unsupported merge transform";return false;}
            double minX=1e30,minY=1e30,maxX=-1e30,maxY=-1e30;
            for(double y:{double(tile.y-16),double(tile.y+tile.height+16)})
                for(double x:{double(tile.x-16),double(tile.x+tile.width+16)}) {
                    const auto q=h*cv::Vec3d(x,y,1.);
                    if(!std::isfinite(q[2]) || q[2]/denominator<.75 || q[2]/denominator>1.25){failure="unsupported merge perspective";return false;}
                    minX=std::min(minX,q[0]/q[2]);maxX=std::max(maxX,q[0]/q[2]);
                    minY=std::min(minY,q[1]/q[2]);maxY=std::max(maxY,q[1]/q[2]);
                }
            if(!std::isfinite(minX)||!std::isfinite(minY)||!std::isfinite(maxX)||!std::isfinite(maxY)||
               minX<-1e6 || minY<-1e6 || maxX>1e6 || maxY>1e6){failure="invalid merge bounds";return false;}
            const int x0=std::clamp(int(std::floor(minX))-2,0,ref.cols-1),y0=std::clamp(int(std::floor(minY))-2,0,ref.rows-1);
            const int x1=std::clamp(int(std::ceil(maxX))+3,x0+1,ref.cols),y1=std::clamp(int(std::ceil(maxY))+3,y0+1,ref.rows);
            const cv::Rect roi(x0,y0,x1-x0,y1-y0);
            if(roi.width>1024 || roi.height>1024){failure="merge tile source extent";return false;}
            meta[m]=uint32_t(images.size());meta[m+1]=roi.width;meta[m+2]=roi.height;
            meta[m+3]=x0;meta[m+4]=y0;
            for(int y=roi.y;y<roi.br().y;++y) {
                const auto* row=plate.image->ptr<cv::Vec3b>(y);
                for(int x=roi.x;x<roi.br().x;++x)images.push_back(uint32_t(row[x][0])|uint32_t(row[x][1])<<8|uint32_t(row[x][2])<<16);
            }
            meta[m+5]=uint32_t(meta.size());meta[m+6]=plate.local->field.cols;meta[m+7]=plate.local->field.rows;meta[m+8]=plate.local->spacing;
            for(int y=0;y<plate.local->field.rows;++y)for(int x=0;x<plate.local->field.cols;++x) {
                const auto n=plate.local->field.at<cv::Vec3f>(y,x);const size_t start=meta.size();meta.resize(start+4);
                for(int c=0;c<3;++c)writeFloat(start+c,n[c]);
            }
            meta[m+9]=uint32_t(-1);
            if(!plate.local->gate.empty()) {
                meta[m+9]=uint32_t(meta.size());const size_t start=meta.size();meta.resize(start+(tile.area()+3)/4,0);
                auto* bytes=reinterpret_cast<uchar*>(meta.data()+start);
                for(int y=0;y<tile.height;++y)std::memcpy(bytes+size_t(y)*tile.width,plate.local->gate.ptr(tile.y+y)+tile.x,tile.width);
            }
            for(int row=0;row<2;++row) {
                const int origin=row ? y0 : x0;
                const double a=(h(row,0)-origin*h(2,0))/denominator,b=(h(row,1)-origin*h(2,1))/denominator;
                const double c=(h(row,0)*tile.x+h(row,1)*tile.y+h(row,2))/denominator-origin;
                if(std::abs(a)>2 || std::abs(b)>2 || std::abs(c)>1024){failure="merge precision envelope";return false;}
                writeFloat(m+12+row*3,float(a));writeFloat(m+13+row*3,float(b));writeFloat(m+14+row*3,float(c));
            }
            writeFloat(m+18,float(h(2,0)/denominator));writeFloat(m+19,float(h(2,1)/denominator));
        }
        const VkDeviceSize sizes[]{images.size()*4,meta.size()*4,size_t(tile.area())*4};
        if(!workspace(sizes))return false;
        std::memcpy(buffers[0].mapped,images.data(),size_t(sizes[0]));std::memcpy(buffers[1].mapped,meta.data(),size_t(sizes[1]));
        if(!dispatch(mergePipeline,sizes,{tile.width,tile.height,tile.x,tile.y,int(plates.size()),0,0,0},uint32_t((tile.area()+127)/128)))return false;
        const auto* pixels=static_cast<const uint32_t*>(buffers[2].mapped);
        output.create(tile.size(),CV_8UC3);contributors=0;multi=0;ambiguous=0;
        bool invalid=false;
        #pragma omp parallel for reduction(+:contributors,multi,ambiguous) reduction(||:invalid)
        for(int y=0;y<tile.height;++y)for(int x=0;x<tile.width;++x) {
            const uint32_t value=pixels[y*tile.width+x];int count=int((value>>24)&15);
            if((value&0x70000000u) || count>int(plates.size())){invalid=true;continue;}
            cv::Vec3b color(value&255,(value>>8)&255,(value>>16)&255);
            if(value&0x80000000u){color=mergePixel(plates,ref,tile.x+x,tile.y+y,count);++ambiguous;}
            output.at<cv::Vec3b>(y,x)=color;contributors+=std::max(1,count);multi+=count>=2;
        }
        if(invalid){healthy=false;failure="invalid GPU merge output";return false;}
        return true;
    }
    bool runPhysical(const cv::Mat& reference,const cv::Mat& refHP,const cv::Mat& srcHP,
        const cv::Mat& validity,const std::vector<PhysicalPatch>& patches,bool highlight,
        std::vector<cv::Vec2d>& metrics,int& ambiguous,int warpMode=-1) {
        if(warpMode<0)warpMode=CV_VERSION_MAJOR>=5 ? 1 : 0;
        size_t largest=0;for(const auto& p:patches)largest=std::max(largest,size_t(p.roi.area()));
        const size_t batch=std::min(size_t(64),patches.size()),chunks=(largest+1023)/1024;
        const VkDeviceSize reserved[]{batch*((largest+3)/4)*8,batch*largest*8,batch*(4+chunks*2)*16};
        if(chunks>properties.limits.maxComputeWorkGroupCount[0] || batch>properties.limits.maxComputeWorkGroupCount[1])return false;
        if(!workspace(reserved))return false;
        metrics.resize(patches.size());ambiguous=0;
        for(size_t begin=0;begin<patches.size();begin+=64) {
            const size_t count=std::min(size_t(64),patches.size()-begin);
            auto* bytes=static_cast<uchar*>(buffers[0].mapped);auto* hp=static_cast<float*>(buffers[1].mapped);
            auto* work=static_cast<cv::Vec4f*>(buffers[2].mapped);size_t byteOffset=0,offset=0;
            for(size_t j=0;j<count;++j) {
                const auto& p=patches[begin+j];const auto& r=p.roi;
                work[j*2]={float(r.width),float(r.height),float(offset),float(byteOffset/4)};
                work[j*2+1]={float(cvRound(p.shift.x*1024)),float(cvRound(p.shift.y*1024)),p.shift.x,p.shift.y};
                const size_t plane=(size_t(r.area())+3)/4*4;std::memset(bytes+byteOffset+plane,255,plane);
                for(int y=0;y<r.height;++y) {
                    std::memcpy(bytes+byteOffset+size_t(y)*r.width,reference.ptr(r.y+y)+r.x,size_t(r.width));
                    if(!validity.empty())std::memcpy(bytes+byteOffset+plane+size_t(y)*r.width,validity.ptr(r.y+y)+r.x,size_t(r.width));
                }
                for(int k=0;k<2;++k)for(int y=0;y<r.height;++y)
                    std::memcpy(hp+offset+size_t(k)*r.area()+size_t(y)*r.width,
                        (k ? srcHP : refHP).ptr<float>(r.y+y)+r.x,size_t(r.width)*4);
                byteOffset+=plane*2;offset+=size_t(r.area())*2;
            }
            const VkDeviceSize sizes[]{byteOffset,offset*4,count*(4+chunks*2)*16};
            if(!dispatch(physicalStages[0],sizes,{int(count),warpMode,int(chunks),0,0,0,0,0},uint32_t(count)))return false;
            for(size_t j=0;j<count;++j) {
                const auto centre=work[count*2+j*2],moments=work[count*2+j*2+1];
                for(float v:{centre[0],centre[1],centre[2],moments[0],moments[1],moments[2]})
                    if(!std::isfinite(v)){failure="invalid GPU physical moments";return false;}
                const double n=centre[2];if(n<0 || n>patches[begin+j].roi.area() || std::floor(n)!=n){failure="invalid physical count";return false;}
                const auto noise=patches[begin+j].noise;
                const double va=n>0 ? moments[0]/n : 0,vb=n>0 ? moments[1]/n : 0,cov=n>0 ? moments[2]/n : 0;
                auto m=physicalMetrics(n,va,vb,cov,noise);
                const double floor=std::max(.1,.1*std::max(noise[0],noise[1]));
                if(n>=256 && (va-noise[0]<floor || vb-noise[1]<floor ||
                    std::abs(m[0]-(highlight ? .80 : .90))<.002 || std::abs(m[0]-.80)<.002 || std::abs(m[1]-.68)<.002)) {
                    ++ambiguous;m=cpuPhysicalCheck(reference,refHP,srcHP,validity,patches[begin+j]);
                }
                metrics[begin+j]=m;
            }
        }
        return true;
    }
    bool runSelection(const cv::Mat& reference,const cv::Mat& source,const cv::Mat& refHP,const cv::Mat& srcHP,
        const cv::Mat& validity,const std::vector<cv::Rect>& patches,const std::vector<cv::Vec2d>& noise,
        std::vector<uchar>& eligible,int& ambiguous) {
        size_t largest=0;for(const auto& patch:patches)largest=std::max(largest,size_t(patch.area()));
        const size_t batchSize=std::min(size_t(16),patches.size()),chunks=(largest+1023)/1024;
        const VkDeviceSize reserved[]{batchSize*((largest+3)/4)*12,batchSize*largest*8,batchSize*(4+chunks*2)*16};
        if(chunks>properties.limits.maxComputeWorkGroupCount[0] || batchSize>properties.limits.maxComputeWorkGroupCount[1] || !workspace(reserved))return false;
        auto* bytes=static_cast<uchar*>(buffers[0].mapped);
        eligible.assign(patches.size(),0);ambiguous=0;
        for(size_t begin=0;begin<patches.size();begin+=16) {
            const size_t count=std::min(size_t(16),patches.size()-begin);size_t offset=0,byteOffset=0;
            auto* hp=static_cast<float*>(buffers[1].mapped);auto* work=static_cast<cv::Vec4f*>(buffers[2].mapped);
            for(size_t j=0;j<count;++j) {
                const auto& roi=patches[begin+j];work[j*2]={float(roi.x),float(roi.y),float(roi.width),float(roi.height)};
                const size_t plane=(size_t(roi.area())+3)/4*4;
                work[j*2+1]={float(offset),float(byteOffset/4),float(plane/4),0};
                // Candidate-only byte planes; preserve ROI strides and default validity.
                std::memset(bytes+byteOffset,255,plane*3);
                for(int p=0;p<3;++p) {
                    const cv::Mat& image=p==0 ? reference : p==1 ? source : validity;
                    if(image.empty())continue;
                    for(int y=0;y<roi.height;++y)std::memcpy(bytes+byteOffset+plane*p+size_t(y)*roi.width,
                        image.ptr(roi.y+y)+roi.x,size_t(roi.width));
                }
                byteOffset+=plane*3;
                for(int planeIndex=0;planeIndex<2;++planeIndex)for(int y=0;y<roi.height;++y)
                    std::memcpy(hp+offset+size_t(planeIndex)*roi.area()+size_t(y)*roi.width,
                        (planeIndex ? srcHP : refHP).ptr<float>(roi.y+y)+roi.x,size_t(roi.width)*4);
                offset+=size_t(roi.area())*2;
            }
            const VkDeviceSize sizes[]{byteOffset,offset*4,count*(4+chunks*2)*16};
            if(!dispatch(selectionStages[0],sizes,{0,0,0,int(count),int(chunks),0,0,0},uint32_t(count)))return false;
            for(size_t j=0;j<count;++j) {
                const auto centre=work[count*2+j*2],moment=work[count*2+j*2+1];
                for(float value:{centre[0],centre[1],centre[2],moment[0],moment[1],moment[2]})if(!std::isfinite(value)){failure="invalid GPU selector moments";return false;}
                if(centre[2]<256)continue;
                const auto n=noise[begin+j];
                const double va=std::max(0.,double(moment[0])/centre[2]-n[0]),vb=std::max(0.,double(moment[1])/centre[2]-n[1]);
                const double ratio=std::sqrt(vb/std::max(va,.01)),score=double(moment[2])/centre[2]/std::max(std::sqrt(va*vb),.01);
                const double varianceFloor=std::max(.1,.1*std::max(n[0],n[1]));
                if(va<varianceFloor || vb<varianceFloor || std::abs(score-.90)<.002 || std::abs(ratio-.68)<.002) {
                    ++ambiguous;eligible[begin+j]=strongPatchEligible(reference,source,refHP,srcHP,validity,patches[begin+j],n);
                } else eligible[begin+j]=score>=.90 && ratio>=.68;
            }
        }
        return true;
    }
    bool selfTestFit() {
        cv::Mat ref(97,113,CV_8U),src,mask(ref.size(),CV_8U,cv::Scalar(255));cv::RNG rng(194731);
        rng.fill(ref,cv::RNG::UNIFORM,40,200);cv::GaussianBlur(ref,ref,{0,0},1.2);
        cv::Mat h=(cv::Mat_<float>(2,3)<<1,0,.25f,0,1,-.125f);
        cv::warpAffine(ref,src,h,ref.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        const std::vector<cv::Rect> patches{{0,0,ref.cols,ref.rows}};std::vector<cv::Vec4f> got;
        if(!runFits(ref,src,src,cv::Mat(),patches,got))return false;
        const auto cpu=cpuPatchFit(ref,src,src,cv::Mat(),patches[0]);
#ifdef HACHICAM_GPU_TEST_CONTROL
        __android_log_print(ANDROID_LOG_INFO,"HachiCam-GPU-Test","ECC startup gpu=%.6f,%.6f,%.6f,%g cpu=%.6f,%.6f,%.6f,%g",
            got[0][0],got[0][1],got[0][2],got[0][3],cpu[0],cpu[1],cpu[2],cpu[3]);
#endif
        return cpu[3]==1 && got[0][3]==1 && std::hypot(cpu[0]-got[0][0],cpu[1]-got[0][1])<=.05 && std::abs(cpu[2]-got[0][2])<=.002;
    }
    bool selfTestCompute() {
        cv::RNG rng(19341);cv::Mat gray(193,257,CV_8U);rng.fill(gray,cv::RNG::UNIFORM,110,142);
        cv::Mat noise;
        if(!runNoise(gray,256,{},noise))return false;
        if(cv::countNonZero(noise!=cpuNoiseGrid(gray,256,{}))){failure="startup noise parity";return false;}
        std::vector<cv::Mat> images(3);std::vector<LocalRegistration> models(3);std::vector<SamplingPlate> plates;
        for(int i=0;i<3;++i) {
            images[i].create(gray.size(),CV_8UC3);rng.fill(images[i],cv::RNG::UNIFORM,0,256);
            if(i){models[i].domain=gray.size();models[i].field=cv::Mat(2,2,CV_32FC3,cv::Scalar(.25*i,-.125*i,.9));}
            plates.push_back({&models[i],cv::Matx33d(1,.0001*i,.2*i,-.0002*i,1,.3*i,1e-7*i,-1e-7*i,1),gray.size(),&images[i]});
        }
        cv::Mat merged;long long n=0,m=0,a=0,expectedN=0,expectedM=0;
        const cv::Rect tile(3,2,247,187);
        if(!runMergeTile(plates,images[0],tile,merged,n,m,a))return false;
        for(int y=0;y<tile.height;++y)for(int x=0;x<tile.width;++x) {
            int count;const auto expected=mergePixel(plates,images[0],tile.x+x,tile.y+y,count);
            if(expected!=merged.at<cv::Vec3b>(y,x)){failure="startup color parity";return false;}
            expectedN+=std::max(1,count);expectedM+=count>=2;
        }
        if(n!=expectedN||m!=expectedM){failure="startup merge count parity";return false;}
        cv::Mat hp;gray.convertTo(hp,CV_32F);std::vector<uchar> flags;int completed=0;
        const std::vector<cv::Rect> selectorPatches{{0,0,97,97},{31,29,129,113}};const std::vector<cv::Vec2d> selectorNoise(selectorPatches.size(),{0,0});
        if(!runSelection(gray,gray,hp,hp,{},selectorPatches,selectorNoise,flags,completed))return false;
        for(size_t i=0;i<selectorPatches.size();++i)if(flags[i]!=strongPatchEligible(gray,gray,hp,hp,{},selectorPatches[i],selectorNoise[i])){failure="startup selector parity";return false;}
        return true;
    }
    bool selfTest() {
        cv::Mat ref(37,65,CV_8U),src(37,65,CV_8U);
        for(int y=0;y<ref.rows;++y)for(int x=0;x<ref.cols;++x){ref.at<uchar>(y,x)=uchar((x*17+y*23)%256);src.at<uchar>(y,x)=uchar((x*17+y*23+14)%256);}
        LocalRegistration m;m.spacing=16;m.domain=ref.size();m.field=cv::Mat(4,5,CV_32FC3,cv::Scalar(.25,-.125,.9));
        for(int test=0;test<4;++test){
            if(test==1)m.field.at<cv::Vec3f>(1,2)={1.75f,-.125f,.9f};
            if(test==2)m.field.at<cv::Vec3f>(2,1)={0,0,0};
            if(test==3)m.field.at<cv::Vec3f>(3,4)={-1.7f,.2f,.9f};
            cv::Mat got;if(!run(ref,src,m,got))return false;
            for(int y=0;y<ref.rows;++y)for(int x=0;x<ref.cols;++x)if(got.at<uchar>(y,x)!=supportPixel(ref,src,m,x,y,false)){failure="GPU startup gate mismatch";return false;}
        }
        failure="";return true;
    }
};
bool validField(const LocalRegistration& m) {
    if(m.spacing<=0 ||
        (m.spacing&(m.spacing-1)) || m.field.empty() || m.field.dims!=2 || m.field.type()!=CV_32FC3 || m.field.total()>4096)return false;
    for(int y=0;y<m.field.rows;++y)for(int x=0;x<m.field.cols;++x){const auto p=m.field.at<cv::Vec3f>(y,x);
        if(!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || std::abs(p[0])>8 || std::abs(p[1])>8 || p[2]<0 || p[2]>1)return false;
    }
    return true;
}
bool valid(const cv::Mat& ref,const cv::Mat& src,const LocalRegistration& m) {
    return !ref.empty() && ref.dims==2 && src.dims==2 && ref.type()==CV_8U && src.type()==CV_8U && ref.size()==src.size() &&
        ref.total()<=16000000 && ref.cols<=8192 && ref.rows<=8192 && (!m.patchSupport || m.domain==ref.size()) && validField(m);
}
Context* sharedContext() {
    static Context* value=[]() noexcept {try{return new Context();}catch(...){return static_cast<Context*>(nullptr);}}();
    return value;
}
}
bool gpuGlobalECC(const cv::Mat& reference,const cv::Mat& source,const cv::Mat& initial,
    int model,cv::TermCriteria criteria,double sourceScale,cv::Mat& output,double& correlation,std::string& reason) {
#ifdef HACHICAM_GPU_TEST_CONTROL
    if(std::getenv("HACHICAM_FORCE_CPU")){reason="forced CPU benchmark";return false;}
#endif
    if(reference.empty()||reference.dims!=2||source.dims!=2||reference.type()!=CV_8U||source.type()!=CV_8U||reference.size()!=source.size()||reference.rows<16||reference.cols<16||reference.rows>1920||reference.cols>1920||reference.total()>2800000||
       (model!=cv::MOTION_EUCLIDEAN && model!=cv::MOTION_HOMOGRAPHY)||initial.type()!=CV_32F||initial.cols!=3||initial.rows!=(model==cv::MOTION_HOMOGRAPHY ? 3 : 2)||!cv::checkRange(initial)||
       (model==cv::MOTION_HOMOGRAPHY && initial.at<float>(2,2)!=1.f)||!std::isfinite(sourceScale)||sourceScale<=0||sourceScale>1||criteria.type!=(cv::TermCriteria::COUNT|cv::TermCriteria::EPS)||criteria.maxCount<1||criteria.maxCount>40||!std::isfinite(criteria.epsilon)||criteria.epsilon<=0){reason="unsupported global ECC workload";return false;}
    const auto started=std::chrono::steady_clock::now();auto* c=sharedContext();if(!c){reason="global GPU context unavailable";return false;}
    std::lock_guard<std::mutex> lock(c->guard);
    if(!c->healthy || !c->ensureGlobal()){reason=c->failure;if(computeTrace.size()<16000)computeTrace+="global unavailable: "+reason+"\n";return false;}
    const int key=(model==cv::MOTION_HOMOGRAPHY ? 3 : 0)+(reference.total()<=300000 ? 0 : reference.total()<=1500000 ? 1 : 2);
    auto& policy=c->globalPolicy[key];
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
    if(!policy.eligible()){traceDecision(policy,"global",key);reason="calibrated CPU global preference";return false;}
#endif
    try {
        cv::Mat result;double cc=-1;int iterations=0;const auto before=std::chrono::steady_clock::now();
        const bool ok=c->runGlobal(reference,source,initial,model,criteria,result,cc,iterations);
        const double cold=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
        if(policy.needsProbe()){
            cv::Mat cpu=initial.clone();const auto cpuStart=std::chrono::steady_clock::now();
            double cpuCC=-1;try{cpuCC=cv::findTransformECC(reference,source,cpu,model,criteria);}catch(const cv::Exception&){reason="CPU global calibration did not converge";policy.record(false,cold,0,1,1,1,reason);traceDecision(policy,"global-calibration",key);return false;}
            const double cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count();
            auto agrees=[&](const cv::Mat& candidate,double score){
                if(candidate.empty()||!cv::checkRange(candidate)||!std::isfinite(score)||std::abs(score-cpuCC)>.002)return false;
                cv::Mat a=cv::Mat::eye(3,3,CV_64F),b=a.clone();candidate.convertTo(a.rowRange(0,candidate.rows),CV_64F);cpu.convertTo(b.rowRange(0,cpu.rows),CV_64F);
                for(int y=0;y<5;++y)for(int x=0;x<5;++x){cv::Matx33d am(a),bm(b);const cv::Vec3d p((reference.cols-1)*x/4.,(reference.rows-1)*y/4.,1.);const auto u=am*p,v=bm*p;if(std::abs(u[2])<1e-9||std::abs(v[2])<1e-9||std::hypot(u[0]/u[2]-v[0]/v[2],u[1]/u[2]-v[1]/v[2])/sourceScale>.05)return false;}return true;
            };
            bool exact=ok && agrees(result,cc);double warm=0;
            if(ok){const auto warmStart=std::chrono::steady_clock::now();const bool warmOk=c->runGlobal(reference,source,initial,model,criteria,result,cc,iterations);warm=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count();exact=exact&&warmOk&&agrees(result,cc);}
            policy.record(exact,cold,warm,cpuMs,GpuCostProfile::reusedCost(cold,warm),cpuMs,"model="+std::to_string(model)+" size="+std::to_string(reference.cols)+"x"+std::to_string(reference.rows));
            traceDecision(policy,"global-calibration",key);
            if(policy.state!=GpuCostProfile::Gpu){output=cpu;correlation=cpuCC;reason="global calibration reused CPU";return true;}
        }
#else
        if(!ok){reason=c->failure;return false;}
#endif
        if(!ok){reason=c->failure;traceDecision(policy,"global-failure",key);return false;}
        output=result;correlation=cc;reason="validated GPU resident global ECC";
        ++globalStats.calls;globalStats.iterations+=iterations;
        const double callMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();globalStats.ms+=callMs;
        traceDecision(policy,"global",key);
        __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU global ECC v1 model=%d size=%dx%d iterations=%d cc=%.6f callMs=%.3f",model,reference.cols,reference.rows,iterations,cc,callMs);
        return true;
    }catch(const cv::Exception&){reason="global GPU numeric failure";return false;}
    catch(const std::exception&){reason="global GPU host failure";return false;}
}
bool gpuLocalSupport(const cv::Mat& ref,const cv::Mat& src,const LocalRegistration& m,cv::Mat& output,std::string& reason,const cv::Mat& validity) {
#ifdef HACHICAM_GPU_TEST_CONTROL
    if(std::getenv("HACHICAM_FORCE_CPU")){reason="forced CPU benchmark";return false;}
#endif
    if(!valid(ref,src,m) || (!validity.empty() && (validity.dims!=2 || validity.type()!=CV_8U || validity.size()!=ref.size()))){reason="unsupported gate workload";return false;}
    auto* context=sharedContext();if(!context){reason="GPU context unavailable";return false;}
    std::lock_guard<std::mutex> lock(context->guard);
    if(!context->healthy){reason=context->failure;return false;}
    try {
        const int bucket=sizeBucket(ref.total())+(m.patchSupport ? 3 : 0);
        auto& policy=context->bucketPolicy[bucket];
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
        if(!policy.eligible()){traceDecision(policy,"gate",bucket);reason="calibrated CPU preference";return false;}
#endif
        const auto start=std::chrono::steady_clock::now();
        if(context->run(ref,src,m,output,validity)) {
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
            if(policy.needsProbe()) {
                const auto gpuEnd=std::chrono::steady_clock::now();
                cv::Mat baseline(ref.size(),CV_8U);
                #pragma omp parallel for schedule(static)
                for(int y=0;y<ref.rows;++y)for(int x=0;x<ref.cols;++x)
                    baseline.at<uchar>(y,x)=supportPixel(ref,src,m,x,y,m.patchSupport,validity);
                const auto cpuEnd=std::chrono::steady_clock::now();
                const double gpuMs=std::chrono::duration<double,std::milli>(gpuEnd-start).count();
                const double cpuMs=std::chrono::duration<double,std::milli>(cpuEnd-gpuEnd).count();
                const bool firstExact=cv::countNonZero(output!=baseline)==0;
                const auto warmStart=std::chrono::steady_clock::now();
                if(!context->run(ref,src,m,output,validity)){reason=context->failure;context->healthy=false;return false;}
                const double warmMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count();
                const bool equal=firstExact && cv::countNonZero(output!=baseline)==0;
                policy.record(equal,gpuMs,warmMs,cpuMs,GpuCostProfile::reusedCost(gpuMs,warmMs),cpuMs);
                __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf",
                    "GPU gate calibration bucket=%d pixels=%zu gpuCallMs=%.3f cpuMs=%.3f exact=%d preferred=%s",
                    bucket,ref.total(),gpuMs,cpuMs,int(equal),policy.state==GpuCostProfile::Gpu ? "Vulkan" : "CPU");
                if(!equal){context->healthy=false;context->failure="real-workload gate mismatch";}
                if(policy.state!=GpuCostProfile::Gpu){traceDecision(policy,"gate",bucket);baseline.copyTo(output);reason=equal ? "calibration selected CPU" : context->failure;return true;}
            }
#endif
            traceDecision(policy,"gate",bucket);reason="validated packed-byte support kernel";return true;
        }
    } catch(const std::exception&){context->failure="GPU gate host failure";}
    context->healthy=false;reason=context->failure;output.release();return false;
}
bool gpuNoiseGrid(const cv::Mat& gray,int spacing,const cv::Mat& active,cv::Mat& output,std::string& reason) {
#ifdef HACHICAM_GPU_TEST_CONTROL
    if(std::getenv("HACHICAM_FORCE_CPU")){reason="forced CPU benchmark";return false;}
#endif
    if(gray.empty()||gray.dims!=2||gray.type()!=CV_8U||gray.total()>16000000||spacing!=256){reason="unsupported noise grid";return false;}
    const cv::Size shape((gray.cols-1+255)/256+1,(gray.rows-1+255)/256+1);
    if(shape.area()>4096 || (!active.empty()&&(active.dims!=2||active.type()!=CV_8U||active.size()!=shape))){reason="invalid noise grid mask";return false;}
    auto* c=sharedContext();if(!c){reason="GPU context unavailable";return false;}
    std::lock_guard<std::mutex> lock(c->guard);if(!c->healthy){reason=c->failure;return false;}
    const int key=sizeBucket(gray.total())*2+(active.empty() ? 0 : 1);
    auto& policy=c->noisePolicy[key];
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
    if(!policy.eligible()){traceDecision(policy,"noise",key);reason="calibrated CPU noise preference";return false;}
#endif
    try {
        const auto start=std::chrono::steady_clock::now();
        if(!c->runNoise(gray,spacing,active,output)){c->healthy=false;reason=c->failure;return false;}
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
        if(policy.needsProbe()) {
            const auto gpuEnd=std::chrono::steady_clock::now();const auto baseline=cpuNoiseGrid(gray,spacing,active);
            const double gpuMs=std::chrono::duration<double,std::milli>(gpuEnd-start).count();
            const double cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-gpuEnd).count();
            const bool firstExact=cv::countNonZero(output!=baseline)==0;
            const auto warmStart=std::chrono::steady_clock::now();
            if(!c->runNoise(gray,spacing,active,output)){c->healthy=false;reason=c->failure;return false;}
            const double warmMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count();
            const bool exact=firstExact && cv::countNonZero(output!=baseline)==0;
            policy.record(exact,gpuMs,warmMs,cpuMs,GpuCostProfile::reusedCost(gpuMs,warmMs),cpuMs);
            __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU noise calibration key=%d nodes=%d gpuCallMs=%.3f cpuMs=%.3f exact=%d preferred=%s",key,shape.area(),gpuMs,cpuMs,int(exact),policy.state==GpuCostProfile::Gpu ? "Vulkan" : "CPU");
            if(!exact){c->healthy=false;c->failure="GPU noise mismatch";reason=c->failure;output.release();return false;}
            if(policy.state!=GpuCostProfile::Gpu){traceDecision(policy,"noise",key);reason="calibration reused CPU noise";output=baseline;return true;}
        }
#endif
        reason="validated GPU noise histogram";
        traceDecision(policy,"noise",key);
        __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU noise v1 nodes=%d pixels=%zu callMs=%.3f",shape.area(),gray.total(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        return true;
    } catch(const std::exception&){c->healthy=false;reason="GPU noise host failure";output.release();return false;}
}
bool gpuPhysicalChecks(const cv::Mat& reference,const cv::Mat& refHP,const cv::Mat& srcHP,
    const cv::Mat& validity,const std::vector<PhysicalPatch>& patches,bool highlight,
    std::vector<cv::Vec2d>& metrics,GpuPhysicalStats& stats,std::string& reason) {
    stats={};metrics.clear();
#ifdef HACHICAM_GPU_TEST_CONTROL
    if(std::getenv("HACHICAM_FORCE_CPU")){reason="forced CPU benchmark";return false;}
#endif
    if(reference.empty()||reference.dims!=2||reference.type()!=CV_8U||reference.total()>16000000||
        refHP.dims!=2||srcHP.dims!=2||refHP.type()!=CV_32F||srcHP.type()!=CV_32F||refHP.size()!=reference.size()||srcHP.size()!=reference.size()||
        patches.empty()||patches.size()>4096||(!validity.empty() && (validity.dims!=2||validity.type()!=CV_8U||validity.size()!=reference.size()))) {
        reason="unsupported physical workload";return false;
    }
    for(const auto& p:patches) {
        const auto r=p.roi;
        if(r.x<0||r.y<0||r.width<32||r.height<32||r.width>321||r.height>321||r.x>reference.cols-r.width||r.y>reference.rows-r.height||
            !std::isfinite(p.shift.x)||!std::isfinite(p.shift.y)||std::hypot(p.shift.x,p.shift.y)>8||
            !std::isfinite(p.noise[0])||!std::isfinite(p.noise[1])||p.noise[0]<0||p.noise[1]<0) {reason="invalid physical patch";return false;}
    }
    auto* c=sharedContext();if(!c){reason="GPU context unavailable";return false;}
    std::lock_guard<std::mutex> lock(c->guard);if(!c->healthy){reason=c->failure;return false;}
    const int key=sizeBucket(reference.total())*6+(highlight ? 3 : 0)+(patches.size()<=4 ? 0 : patches.size()<=16 ? 1 : 2);
    auto& policy=c->physicalPolicy[key];
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
    if(!policy.eligible()){traceDecision(policy,"physical",key);reason="calibrated CPU physical preference";return false;}
#endif
    const auto start=std::chrono::steady_clock::now();int ambiguous=0;
    try {
        if(!c->runPhysical(reference,refHP,srcHP,validity,patches,highlight,metrics,ambiguous)){c->healthy=false;reason=c->failure;return false;}
        const auto gpuEnd=std::chrono::steady_clock::now();const double cold=std::chrono::duration<double,std::milli>(gpuEnd-start).count();
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
        if(policy.needsProbe()) {
            std::vector<cv::Vec2d> cpu(patches.size());
            #pragma omp parallel for schedule(dynamic)
            for(int i=0;i<int(patches.size());++i)cpu[i]=cpuPhysicalCheck(reference,refHP,srcHP,validity,patches[i]);
            const double cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-gpuEnd).count();
            auto matches=[&] {for(size_t i=0;i<cpu.size();++i) {
                const auto a=metrics[i],b=cpu[i];
                if(!std::isfinite(a[0])||!std::isfinite(a[1])||std::abs(a[0]-b[0])>.002||std::abs(a[1]-b[1])>.002||
                    (a[0]>=(highlight ? .80 : .90))!=(b[0]>=(highlight ? .80 : .90))||
                    (a[0]>=.80)!=(b[0]>=.80)||(a[1]>=.68)!=(b[1]>=.68))return false;
            }return true;};
            const bool first=matches();const auto warmStart=std::chrono::steady_clock::now();
            if(!c->runPhysical(reference,refHP,srcHP,validity,patches,highlight,metrics,ambiguous)){c->healthy=false;reason=c->failure;return false;}
            const double warm=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count();
            policy.record(first && matches(),cold,warm,cpuMs,GpuCostProfile::reusedCost(cold,warm),cpuMs,"patches="+std::to_string(patches.size())+" completed="+std::to_string(ambiguous));
            traceDecision(policy,"physical-calibration",key);
            // Reuse the already calculated authority, including a calibration failure.
            metrics=cpu;stats.cpuPatches=int(cpu.size());
            stats.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            reason="calibration reused CPU physical checks";return true;
        }
#endif
        stats.patches=int(patches.size());stats.cpuPatches=ambiguous;
        stats.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        traceDecision(policy,"physical",key);reason="validated GPU physical checks";return true;
    } catch(const std::exception&){c->healthy=false;reason="GPU physical host failure";return false;}
}
bool gpuStrongSelection(const cv::Mat& reference,const cv::Mat& source,const cv::Mat& refHP,const cv::Mat& srcHP,
    const cv::Mat& validity,const std::vector<cv::Rect>& patches,const std::vector<cv::Vec2d>& noise,
    std::vector<uchar>& eligible,GpuSelectionStats& stats,std::string& reason) {
    stats={};
#ifdef HACHICAM_GPU_TEST_CONTROL
    if(std::getenv("HACHICAM_FORCE_CPU")){reason="forced CPU benchmark";return false;}
#endif
    if(reference.empty()||reference.dims!=2||source.dims!=2||refHP.dims!=2||srcHP.dims!=2||
       reference.type()!=CV_8U||source.type()!=CV_8U||reference.size()!=source.size()||reference.total()>16000000||
       refHP.type()!=CV_32F||srcHP.type()!=CV_32F||refHP.size()!=reference.size()||srcHP.size()!=reference.size()||
       patches.empty()||patches.size()>4096||noise.size()!=patches.size()||
       (!validity.empty()&&(validity.dims!=2||validity.type()!=CV_8U||validity.size()!=reference.size()))){reason="unsupported selector workload";return false;}
    for(size_t i=0;i<patches.size();++i) {
        const auto& p=patches[i];const auto n=noise[i];
        if(p.x<0||p.y<0||p.width<32||p.height<32||p.width>321||p.height>321||p.x>reference.cols-p.width||p.y>reference.rows-p.height||
           !std::isfinite(n[0])||!std::isfinite(n[1])||n[0]<0||n[1]<0){reason="invalid selector patch";return false;}
    }
    auto* c=sharedContext();if(!c){reason="GPU context unavailable";return false;}
    std::lock_guard<std::mutex> lock(c->guard);if(!c->healthy){reason=c->failure;return false;}
    const int key=sizeBucket(reference.total());auto& policy=c->selectionPolicy[key];
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
    if(!policy.eligible()){traceDecision(policy,"selector",key);reason="calibrated CPU selector preference";return false;}
#endif
    const auto start=std::chrono::steady_clock::now();int ambiguous=0;
    try {
        if(!c->runSelection(reference,source,refHP,srcHP,validity,patches,noise,eligible,ambiguous)){c->healthy=false;reason=c->failure;return false;}
        const auto gpuEnd=std::chrono::steady_clock::now();const double coldMs=std::chrono::duration<double,std::milli>(gpuEnd-start).count();
        double executionEstimate=coldMs;
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
        if(policy.needsProbe()) {
            std::vector<uchar> cpu(patches.size());
            #pragma omp parallel for schedule(dynamic)
            for(int i=0;i<int(patches.size());++i)cpu[i]=strongPatchEligible(reference,source,refHP,srcHP,validity,patches[i],noise[i]);
            const double cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-gpuEnd).count();
            const bool firstExact=cpu==eligible;const auto warmStart=std::chrono::steady_clock::now();
            if(!c->runSelection(reference,source,refHP,srcHP,validity,patches,noise,eligible,ambiguous)){c->healthy=false;reason=c->failure;return false;}
            const double warmMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count();
            policy.record(firstExact && eligible==cpu,coldMs,warmMs,cpuMs,GpuCostProfile::reusedCost(coldMs,warmMs),cpuMs);
            traceDecision(policy,"selector-calibration",key);
            __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU selector calibration key=%d patches=%zu coldMs=%.3f warmMs=%.3f cpuMs=%.3f exact=%d preferred=%s",key,patches.size(),coldMs,warmMs,cpuMs,int(firstExact && eligible==cpu),policy.state==GpuCostProfile::Gpu ? "Vulkan" : "CPU");
            if(policy.state!=GpuCostProfile::Gpu) {
                eligible=cpu;stats.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();stats.estimateMs=cpuMs;
                reason="calibration reused CPU selector";return true;
            }
            executionEstimate=policy.estimatedGpu;
        }
#endif
        stats.patches=int(patches.size());stats.cpuPatches=ambiguous;
        stats.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();stats.estimateMs=executionEstimate;
        traceDecision(policy,"selector",key);reason="validated GPU strong selector";
        __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU selector v1 patches=%d cpuPatches=%d callMs=%.3f estimateMs=%.3f",stats.patches,stats.cpuPatches,stats.ms,stats.estimateMs);
        return true;
    } catch(const std::exception&){c->healthy=false;reason="GPU selector host failure";return false;}
}
bool gpuMergeAnchors(const std::vector<SamplingPlate>& plates,const cv::Mat& ref,const cv::Rect& crop,
                     cv::Mat& output,long long& contributors,long long& multi,GpuComputeStats& stats,std::string& reason) {
    stats={};
#ifdef HACHICAM_GPU_TEST_CONTROL
    if(std::getenv("HACHICAM_FORCE_CPU")){reason="forced CPU benchmark";return false;}
#endif
    if(ref.empty()||ref.dims!=2||ref.type()!=CV_8UC3||ref.total()>16000000||crop.empty()||
       (crop&cv::Rect({},ref.size()))!=crop||plates.size()<2||plates.size()>4){reason="unsupported merge workload";return false;}
    for(const auto& p:plates) {
        if(!p.local||!p.image||p.image->dims!=2||p.image->type()!=CV_8UC3||p.image->size()!=ref.size()||p.size!=ref.size()||p.local->patchSupport||
           (!p.local->gate.empty()&&(p.local->gate.type()!=CV_8U||p.local->gate.size()!=ref.size()))||
           (!p.local->field.empty() && !validField(*p.local)) || !cv::checkRange(cv::Mat(p.inverse))) {
            reason="invalid merge plate";return false;
        }
    }
    auto* c=sharedContext();if(!c){reason="GPU context unavailable";return false;}
    std::lock_guard<std::mutex> lock(c->guard);if(!c->healthy){reason=c->failure;return false;}
    const int key=int(plates.size()-2)+sizeBucket(ref.total())*3;
    auto& policy=c->mergePolicy[key];
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
    if(!policy.eligible()){traceDecision(policy,"color",key);reason="calibrated CPU merge preference";return false;}
#endif
    const auto start=std::chrono::steady_clock::now();GpuComputeStats actual;
    long long total=0,multiTotal=0;cv::Mat assembled(crop.size(),CV_8UC3);
    std::vector<cv::Rect> tiles;
    for(int y=crop.y;y<crop.br().y;y+=256)for(int x=crop.x;x<crop.br().x;x+=256)
        tiles.emplace_back(x,y,std::min(256,crop.br().x-x),std::min(256,crop.br().y-y));
    if(tiles.size()>4096){reason="unsupported merge tile count";return false;}
    struct CachedTile {size_t index;cv::Mat pixels;long long n=0,m=0,a=0;};
    std::vector<CachedTile> cached;
    try {
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
        if(policy.needsProbe()) {
            const size_t columns=(crop.width+255)/256,rows=(crop.height+255)/256;
            std::vector<size_t> samples;
            for(size_t index:{(rows/2)*columns+columns/2,size_t(0),tiles.size()-1})
                if(std::find(samples.begin(),samples.end(),index)==samples.end())samples.push_back(index);
            double cold=0,firstWarm=0,warmSum=0,cpuSum=0;size_t pixels=0;bool exact=true;
            for(size_t index:samples) {
                const auto& tile=tiles[index];CachedTile item;item.index=index;
                const auto before=std::chrono::steady_clock::now();
                if(!c->runMergeTile(plates,ref,tile,item.pixels,item.n,item.m,item.a)){reason=c->failure;return false;}
                double gpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
                cv::Mat first;long long firstN=item.n,firstM=item.m;
                if(cached.empty()) {
                    cold=gpuMs;first=item.pixels.clone();const auto warmStart=std::chrono::steady_clock::now();
                    if(!c->runMergeTile(plates,ref,tile,item.pixels,item.n,item.m,item.a)){reason=c->failure;return false;}
                    gpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count();firstWarm=gpuMs;
                }
                const auto cpuStart=std::chrono::steady_clock::now();cv::Mat baseline(tile.size(),CV_8UC3);long long bn=0,bm=0;
                #pragma omp parallel for reduction(+:bn,bm)
                for(int yy=0;yy<tile.height;++yy)for(int xx=0;xx<tile.width;++xx) {
                    int count;baseline.at<cv::Vec3b>(yy,xx)=mergePixel(plates,ref,tile.x+xx,tile.y+yy,count);bn+=std::max(1,count);bm+=count>=2;
                }
                const double cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count();
                exact=exact && cv::countNonZero(item.pixels.reshape(1)!=baseline.reshape(1))==0 && item.n==bn && item.m==bm;
                if(!first.empty())exact=exact && cv::countNonZero(first.reshape(1)!=baseline.reshape(1))==0 && firstN==bn && firstM==bm;
                warmSum+=gpuMs;cpuSum+=cpuMs;pixels+=tile.area();cached.push_back(std::move(item));
            }
            const double gpuEstimate=warmSum/pixels*crop.area()+std::max(0.,cold-firstWarm)*.25,cpuEstimate=cpuSum/pixels*crop.area();
            policy.record(exact,cold,warmSum/pixels*tiles[samples[0]].area(),cpuSum/pixels*tiles[samples[0]].area(),gpuEstimate,cpuEstimate,"samples="+std::to_string(samples.size()));
            traceDecision(policy,"color-calibration",key);
            __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU merge calibration v2 key=%d samples=%zu pixels=%zu estimatedGpuMs=%.3f estimatedCpuMs=%.3f exact=%d preferred=%s",key,samples.size(),pixels,gpuEstimate,cpuEstimate,int(exact),policy.state==GpuCostProfile::Gpu ? "Vulkan" : "CPU");
            if(!exact){c->healthy=false;c->failure="GPU merge mismatch";reason=c->failure;return false;}
            if(policy.state!=GpuCostProfile::Gpu){reason="calibrated CPU merge preference";return false;}
        }
#endif
        for(size_t index=0;index<tiles.size();++index) {
            const auto& tile=tiles[index];CachedTile item;item.index=index;
            const auto found=std::find_if(cached.begin(),cached.end(),[&](const CachedTile& value){return value.index==index;});
            if(found!=cached.end())item=*found;
            else if(!c->runMergeTile(plates,ref,tile,item.pixels,item.n,item.m,item.a)){reason=c->failure;return false;}
            item.pixels.copyTo(assembled(cv::Rect(tile.x-crop.x,tile.y-crop.y,tile.width,tile.height)));
            total+=item.n;multiTotal+=item.m;actual.cpuMergePixels+=item.a;++actual.mergeTiles;
        }
        output=assembled;contributors=total;multi=multiTotal;
        actual.mergeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();stats=actual;
        reason="validated GPU tiled color merge";
        traceDecision(policy,"color",key);
        __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU merge v1 tiles=%d cpuPixels=%lld totalPixels=%zu callMs=%.3f",actual.mergeTiles,actual.cpuMergePixels,output.total(),actual.mergeMs);
        return true;
    } catch(const std::exception&){c->healthy=false;reason="GPU merge host failure";return false;}
}
bool gpuPatchFits(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,const cv::Mat& validity,
    const std::vector<cv::Rect>& patches,bool highlight,std::vector<cv::Vec4f>& fits,std::string& reason,double selectionMs) {
    if(ref.empty() || ref.dims!=2 || src.dims!=2 || raw.dims!=2 || (!validity.empty() && validity.dims!=2) ||
        ref.type()!=CV_8U || src.type()!=CV_8U || raw.type()!=CV_8U || ref.size()!=src.size() || ref.size()!=raw.size() ||
        ref.total()>16000000 || patches.empty() || patches.size()>4096 || !std::isfinite(selectionMs) || selectionMs<0 ||
        (!validity.empty() && (validity.type()!=CV_8U || validity.size()!=ref.size()))){reason="unsupported ECC workload";return false;}
    for(const auto& patch:patches)if(patch.width<32 || patch.height<32 || patch.width>321 || patch.height>321 ||
        patch.x<0 || patch.y<0 || patch.x>ref.cols-patch.width || patch.y>ref.rows-patch.height){reason="invalid ECC patch";return false;}
    auto* context=sharedContext();if(!context){reason="GPU context unavailable";return false;}
    std::lock_guard<std::mutex> lock(context->guard);
    if(!context->healthy || !context->eccReady){reason=context->healthy ? "ECC startup conformance unavailable" : context->failure;return false;}
    const int key=(patches.size()<=8 ? 0 : patches.size()<=16 ? 1 : 2)+(highlight ? 3 : 0)+sizeBucket(ref.total())*6;
    auto& policy=context->fitPolicy[key];
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
    if(!policy.eligible()){context->fitScopeSkip[highlight ? 1 : 0]=3;traceDecision(policy,"fit",key);reason="calibrated CPU fitting preference";return false;}
#endif
    fits.assign(patches.size(),cv::Vec4f(0,0,-1,0));
    try {
        for(size_t begin=0;begin<patches.size();begin+=16) {
            const size_t end=std::min(begin+16,patches.size());
            const std::vector<cv::Rect> batch(patches.begin()+begin,patches.begin()+end);
            std::vector<cv::Vec4f> output;
            const auto start=std::chrono::steady_clock::now();
            if(!context->runFits(ref,src,raw,validity,batch,output,30,nullptr,true)) {
                context->healthy=false;reason=context->failure;fits.clear();return false;
            }
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
            const bool newBounded=context->boundedFitState[key]==0 && std::any_of(output.begin(),output.end(),[](const auto& v){return v[3]==3;});
            if(policy.needsProbe() || newBounded) {
                const auto gpuEnd=std::chrono::steady_clock::now();
                std::vector<cv::Vec4f> cpu(batch.size());
                #pragma omp parallel for schedule(dynamic)
                for(int i=0;i<int(batch.size());++i)cpu[i]=cpuPatchFit(ref,src,raw,validity,batch[i]);
                const auto cpuEnd=std::chrono::steady_clock::now();
                const auto warmStart=std::chrono::steady_clock::now();
                std::vector<cv::Vec4f> warm;
                if(!context->runFits(ref,src,raw,validity,batch,warm,30,nullptr,true)){context->healthy=false;reason=context->failure;fits.clear();return false;}
                const double warmMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count();
                bool hasBounded=false,boundedMatch=true;
                for(size_t i=0;i<batch.size();++i)for(const auto& candidate:{output[i],warm[i]})if(candidate[3]==3) {
                    hasBounded=true;
                    if(!cpu[i][3] || std::hypot(cpu[i][0]-candidate[0],cpu[i][1]-candidate[1])>.05 || std::abs(cpu[i][2]-candidate[2])>.002)boundedMatch=false;
                }
                if(context->boundedFitState[key]==0 && hasBounded)context->boundedFitState[key]=boundedMatch ? 1 : 2;
                if(hasBounded) {
                    const auto line=std::string("fit-count-bound key=")+std::to_string(key)+" state="+(context->boundedFitState[key]==1 ? "admitted" : "CPU-completion")+"\n";
                    if(computeTrace.size()+line.size()<16352)computeTrace+=line;
                }
                if(context->boundedFitState[key]!=1)for(size_t i=0;i<batch.size();++i) {
                    if(output[i][3]==3)output[i]={0,0,-1,0};
                    if(warm[i][3]==3)warm[i]={0,0,-1,0};
                }
                bool match=true;int usable=0,missing=0;
                for(size_t i=0;i<batch.size();++i) {
                    if(output[i][3]) {
                        ++usable;
                        if(!cpu[i][3] || std::hypot(cpu[i][0]-output[i][0],cpu[i][1]-output[i][1])>.05 || std::abs(cpu[i][2]-output[i][2])>.002)match=false;
                    } else ++missing;
                    if(warm[i][3] && (!cpu[i][3] || std::hypot(cpu[i][0]-warm[i][0],cpu[i][1]-warm[i][1])>.05 || std::abs(cpu[i][2]-warm[i][2])>.002))match=false;
                    if(bool(output[i][3])!=bool(warm[i][3]))match=false;
#ifdef HACHICAM_GPU_TEST_CONTROL
                    __android_log_print(ANDROID_LOG_INFO,"HachiCam-GPU-Test","ECC probe i=%zu CPU=%g,%g,%g,%g GPU=%g,%g,%g,%g",
                        i,cpu[i][0],cpu[i][1],cpu[i][2],cpu[i][3],output[i][0],output[i][1],output[i][2],output[i][3]);
#endif
                }
                const double gpuMs=std::chrono::duration<double,std::milli>(gpuEnd-start).count();
                const double cpuMs=std::chrono::duration<double,std::milli>(cpuEnd-gpuEnd).count();
                // Measure the hybrid path's real CPU completion cost. The CPU
                // baseline above is reused for results, never lost on GPU misses.
                double fallbackMs=0;
                if(missing) {
                    const auto fallbackStart=std::chrono::steady_clock::now();std::vector<cv::Vec4f> fallback(batch.size());
                    #pragma omp parallel for schedule(dynamic)
                    for(int i=0;i<int(batch.size());++i)if(!output[i][3])fallback[i]=cpuPatchFit(ref,src,raw,validity,batch[i]);
                    fallbackMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-fallbackStart).count();
                }
                const double batches=double((patches.size()+15)/16);
                const double estimatedGpu=batches*warmMs+std::max(0.,gpuMs-warmMs)*.25+batches*fallbackMs+selectionMs;
                const double estimatedCpu=cpuMs*double(patches.size())/batch.size();
                // Empty usable output is a cost fallback, not numerical disagreement.
                policy.record(match,gpuMs,warmMs,cpuMs,usable ? estimatedGpu : estimatedCpu*2,estimatedCpu,
                    "selectorMs="+std::to_string(selectionMs)+" fallbackMs="+std::to_string(fallbackMs)+" missing="+std::to_string(missing));
                __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf",
                    "GPU ECC calibration v5 key=%d patches=%zu/%zu coldCallMs=%.3f warmCallMs=%.3f cpuBatchMs=%.3f fallbackMs=%.3f selectorMs=%.3f estimatedGpuMs=%.3f estimatedCpuMs=%.3f conforming=%d usable=%d missing=%d preferred=%s",
                    key,batch.size(),patches.size(),gpuMs,warmMs,cpuMs,fallbackMs,selectionMs,estimatedGpu,estimatedCpu,int(match),usable,missing,policy.state==GpuCostProfile::Gpu ? "Vulkan" : "CPU");
                traceDecision(policy,"fit-calibration",key);
                if(policy.state!=GpuCostProfile::Gpu) {
                    context->fitScopeSkip[highlight ? 1 : 0]=3;
                    for(size_t i=0;i<cpu.size();++i){if(cpu[i][3])cpu[i][3]=2;fits[begin+i]=cpu[i];}
                    reason="calibration reused CPU fits; remaining patches use CPU";return true;
                }
                for(size_t i=0;i<cpu.size();++i)if(!output[i][3] && cpu[i][3]){output[i]=cpu[i];output[i][3]=2;}
            }
#endif
            for(auto& fit:output)if(fit[3]==3) {
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
                if(context->boundedFitState[key]!=1){fit={0,0,-1,0};continue;}
#endif
                fit[3]=1;
            }
            std::copy(output.begin(),output.end(),fits.begin()+begin);
        }
        traceDecision(policy,"fit",key);reason="validated batched ECC proposals";return true;
    } catch(const std::exception&) {reason="GPU ECC host failure";fits.clear();return false;}
}
bool gpuFittingAvailable(size_t pixels,bool highlight) {
#ifdef HACHICAM_GPU_TEST_CONTROL
    if(std::getenv("HACHICAM_FORCE_CPU"))return false;
#endif
    if(pixels>16000000)return false;
    auto* context=sharedContext();if(!context)return false;
    std::lock_guard<std::mutex> lock(context->guard);
    const int base=(highlight ? 3 : 0)+sizeBucket(pixels)*6;
    if(!context->healthy || !context->eccReady) {
        if(computeTrace.size()<16280)computeTrace+="fit unavailable: "+(context->healthy ? std::string("startup conformance") : context->failure)+"\n";
        return false;
    }
    const bool admitted=context->fitPolicy[base].state==GpuCostProfile::Gpu || context->fitPolicy[base+1].state==GpuCostProfile::Gpu || context->fitPolicy[base+2].state==GpuCostProfile::Gpu;
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
    auto& skip=context->fitScopeSkip[highlight ? 1 : 0];
    if(!admitted && skip>0){--skip;for(int i=0;i<3;++i)if(context->fitPolicy[base+i].state!=GpuCostProfile::Unknown)traceDecision(context->fitPolicy[base+i],"fit-scope-cooldown",base+i);return false;}
#endif
    for(int i=0;i<3;++i)if(context->fitPolicy[base+i].state!=GpuCostProfile::NumericalFailure)return true;
    return false;
}
#ifdef HACHICAM_GPU_TEST_CONTROL
bool gpuTestQuantizedPhysical(const cv::Mat& ref,const cv::Mat& a,const cv::Mat& b,const cv::Mat& validity,
    const std::vector<PhysicalPatch>& patches,std::vector<cv::Vec2d>& got) {
    auto* c=sharedContext();if(!c)return false;std::lock_guard<std::mutex> lock(c->guard);int ambiguous=0;
    return c->healthy && c->runPhysical(ref,a,b,validity,patches,true,got,ambiguous,0);
}
bool gpuTestCountLimitedFits(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,const cv::Mat& validity,
    const std::vector<cv::Rect>& patches,int iterations,std::vector<cv::Vec4f>& fits,int& bounded) {
    if(iterations<1 || iterations>30 || patches.empty() || patches.size()>16)return false;
    auto* context=sharedContext();if(!context)return false;
    std::lock_guard<std::mutex> lock(context->guard);
    return context->healthy && context->eccReady && context->runFits(ref,src,raw,validity,patches,fits,iterations,&bounded);
}
GpuTestState gpuTestState() {
    auto* c=sharedContext();if(!c)return {};
    std::lock_guard<std::mutex> lock(c->guard);GpuTestState s;s.healthy=c->healthy;s.inFlight=c->inFlight;
    for(const auto& b:c->buffers)s.allocation+=b.allocation;return s;
}
bool gpuTestPreparedPlanes(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,
    const cv::Mat& validity,const std::vector<cv::Rect>& patches,std::vector<std::array<cv::Mat,5>>& planes) {
    auto* context=sharedContext();if(!context)return false;
    std::lock_guard<std::mutex> lock(context->guard);
    if(!context->healthy || !context->eccReady)return false;
    std::vector<cv::Vec4f> fits;
    if(!context->runFits(ref,src,raw,validity,patches,fits))return false;
    planes.resize(patches.size());size_t offset=0;
    const auto* data=static_cast<const float*>(context->buffers[0].mapped);
    for(size_t i=0;i<patches.size();++i) {
        const auto count=patches[i].area();
        for(int plane=0;plane<5;++plane) {
            planes[i][plane].create(patches[i].size(),CV_32F);
            std::memcpy(planes[i][plane].data,data+offset+plane*count,count*sizeof(float));
        }
        offset+=size_t(count)*5;
    }
    return true;
}
#endif
}
#endif
