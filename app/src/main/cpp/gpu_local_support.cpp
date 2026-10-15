#include "local_support.h"
#ifndef HACHICAM_VULKAN_SUPPORT
namespace fusion {
bool gpuLocalSupport(const cv::Mat&,const cv::Mat&,const LocalRegistration&,cv::Mat&,std::string& reason) {
    reason="Vulkan backend not built"; return false;
}
bool gpuPatchFits(const cv::Mat&,const cv::Mat&,const cv::Mat&,const cv::Mat&,const std::vector<cv::Rect>&,bool,
    std::vector<cv::Vec4f>&,std::string& reason,double){reason="Vulkan backend not built";return false;}
bool gpuFittingAvailable(size_t,bool){return false;}
}
#else
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "shaders/local_support_spv.h"
#include "shaders/local_ecc_spv.h"
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
    int bucketPolicy[3]{0,0,0}; // 0 unknown, 1 GPU admitted, -1 measured CPU preference
    int fitPolicy[6]{0,0,0,0,0,0};
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
        return gateReady && healthy;
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
        if(selected==eccPipeline) {
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
    bool run(const cv::Mat& ref,const cv::Mat& src,const LocalRegistration& model,cv::Mat& output) {
        const size_t count=ref.total(), words=(count+3)/4, plane=words*4;
        const VkDeviceSize sizes[]{2*plane,model.field.total()*16,plane};
        if(sizes[0]>properties.limits.maxStorageBufferRange || (words+127)/128>properties.limits.maxComputeWorkGroupCount[0]){failure="storage/dispatch limit";return false;}
        for(int i=0;i<3;++i)if(!reserve(i,sizes[i]))return false;
        auto* input=static_cast<unsigned char*>(buffers[0].mapped);
        for(int y=0;y<ref.rows;++y){std::memcpy(input+size_t(y)*ref.cols,ref.ptr(y),ref.cols);std::memcpy(input+plane+size_t(y)*ref.cols,src.ptr(y),src.cols);}
        if(plane>count){std::memset(input+count,0,plane-count);std::memset(input+plane+count,0,plane-count);}
        auto* fields=static_cast<float*>(buffers[1].mapped);
        for(int y=0;y<model.field.rows;++y)for(int x=0;x<model.field.cols;++x){const auto p=model.field.at<cv::Vec3f>(y,x);const size_t i=size_t(y*model.field.cols+x)*4;fields[i]=p[0];fields[i+1]=p[1];fields[i+2]=p[2];fields[i+3]=0;}
        const std::array<int,8> shape{ref.cols,ref.rows,int(words),model.field.cols,model.field.rows,model.spacing,int(count),0};
        if(!dispatch(pipeline,sizes,shape,uint32_t((words+127)/128)))return false;
        output.create(ref.size(),CV_8U);const auto* bytes=static_cast<const uchar*>(buffers[2].mapped);
        bool invalid=false;
        #pragma omp parallel for reduction(||:invalid)
        for(int y=0;y<ref.rows;++y)for(int x=0;x<ref.cols;++x){const auto value=bytes[size_t(y)*ref.cols+x];
            if(value!=0 && value!=255 && value!=128){invalid=true;continue;}
            output.at<uchar>(y,x)=value==128 ? supportPixel(ref,src,model,x,y,false) : value;
        }
        if(invalid){failure="invalid shader output";return false;}
        return true;
    }
    bool runFits(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,const cv::Mat& validity,
                 const std::vector<cv::Rect>& patches,std::vector<cv::Vec4f>& results) {
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
        for(int i=0;i<3;++i)if(!reserve(i,sizes[i]))return false;
        std::memcpy(buffers[1].mapped,packed.data(),sizes[1]);
        std::array<int,8> shape{int(patches.size()),30,0,0,int(chunks),0,0,0};const float epsilon=.0001f;
        std::memcpy(&shape[2],&epsilon,sizeof(float));
        if(!dispatch(eccPipeline,sizes,shape,uint32_t(patches.size())))return false;
        results.resize(patches.size());static_assert(sizeof(cv::Vec4f)==16);
        std::memcpy(results.data(),buffers[2].mapped,results.size()*sizeof(cv::Vec4f));
        for(auto& fit:results)if(fit[3]!=1 || !std::isfinite(fit[0]) || !std::isfinite(fit[1]) || !std::isfinite(fit[2]) || fit[2]<.65f || std::hypot(fit[0],fit[1])>8)
            fit={0,0,-1,0};
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
bool valid(const cv::Mat& ref,const cv::Mat& src,const LocalRegistration& m) {
    if(ref.empty() || ref.dims!=2 || src.dims!=2 || ref.type()!=CV_8U || src.type()!=CV_8U || ref.size()!=src.size() ||
        ref.total()>16000000 || ref.cols>8192 || ref.rows>8192 || m.patchSupport || m.spacing<=0 ||
        (m.spacing&(m.spacing-1)) || m.field.empty() || m.field.dims!=2 || m.field.type()!=CV_32FC3 || m.field.total()>4096)return false;
    for(int y=0;y<m.field.rows;++y)for(int x=0;x<m.field.cols;++x){const auto p=m.field.at<cv::Vec3f>(y,x);
        if(!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || std::abs(p[0])>8 || std::abs(p[1])>8 || p[2]<0 || p[2]>1)return false;
    }
    return true;
}
Context* sharedContext() {
    static Context* value=[]() noexcept {try{return new Context();}catch(...){return static_cast<Context*>(nullptr);}}();
    return value;
}
}
bool gpuLocalSupport(const cv::Mat& ref,const cv::Mat& src,const LocalRegistration& m,cv::Mat& output,std::string& reason) {
#ifdef HACHICAM_GPU_TEST_CONTROL
    if(std::getenv("HACHICAM_FORCE_CPU")){reason="forced CPU benchmark";return false;}
#endif
    if(!valid(ref,src,m)){reason="unsupported gate workload";return false;}
    auto* context=sharedContext();if(!context){reason="GPU context unavailable";return false;}
    std::lock_guard<std::mutex> lock(context->guard);
    if(!context->healthy){reason=context->failure;return false;}
    try {
        const int bucket=ref.total()<=1000000 ? 0 : ref.total()<=4000000 ? 1 : 2;
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
        if(context->bucketPolicy[bucket]<0){reason="calibrated CPU preference";return false;}
#endif
        const auto start=std::chrono::steady_clock::now();
        if(context->run(ref,src,m,output)) {
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
            if(context->bucketPolicy[bucket]==0) {
                const auto gpuEnd=std::chrono::steady_clock::now();
                cv::Mat baseline(ref.size(),CV_8U);
                #pragma omp parallel for schedule(static)
                for(int y=0;y<ref.rows;++y)for(int x=0;x<ref.cols;++x)
                    baseline.at<uchar>(y,x)=supportPixel(ref,src,m,x,y,false);
                const auto cpuEnd=std::chrono::steady_clock::now();
                const double gpuMs=std::chrono::duration<double,std::milli>(gpuEnd-start).count();
                const double cpuMs=std::chrono::duration<double,std::milli>(cpuEnd-gpuEnd).count();
                const bool equal=cv::countNonZero(output!=baseline)==0;
                context->bucketPolicy[bucket]=equal && gpuMs<=cpuMs*.90 ? 1 : -1;
                __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf",
                    "GPU gate calibration bucket=%d pixels=%zu gpuCallMs=%.3f cpuMs=%.3f exact=%d preferred=%s",
                    bucket,ref.total(),gpuMs,cpuMs,int(equal),context->bucketPolicy[bucket]>0 ? "Vulkan" : "CPU");
                if(!equal){context->healthy=false;context->failure="real-workload gate mismatch";}
                if(context->bucketPolicy[bucket]<0){baseline.copyTo(output);reason=equal ? "calibration selected CPU" : context->failure;return true;}
            }
#endif
            reason="validated packed-byte support kernel";return true;
        }
    } catch(const std::exception&){context->failure="GPU gate host failure";}
    context->healthy=false;reason=context->failure;output.release();return false;
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
    const int key=(patches.size()<=8 ? 0 : patches.size()<=16 ? 1 : 2)+(highlight ? 3 : 0);
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
    if(context->fitPolicy[key]<0){reason="calibrated CPU fitting preference";return false;}
#endif
    fits.assign(patches.size(),cv::Vec4f(0,0,-1,0));
    try {
        for(size_t begin=0;begin<patches.size();begin+=16) {
            const size_t end=std::min(begin+16,patches.size());
            const std::vector<cv::Rect> batch(patches.begin()+begin,patches.begin()+end);
            std::vector<cv::Vec4f> output;
            const auto start=std::chrono::steady_clock::now();
            if(!context->runFits(ref,src,raw,validity,batch,output)) {
                context->healthy=false;reason=context->failure;fits.clear();return false;
            }
#if !defined(HACHICAM_GPU_TEST_CONTROL) || defined(HACHICAM_GPU_BENCH_AUTO)
            if(context->fitPolicy[key]==0) {
                const auto gpuEnd=std::chrono::steady_clock::now();
                std::vector<cv::Vec4f> cpu(batch.size());
                #pragma omp parallel for schedule(dynamic)
                for(int i=0;i<int(batch.size());++i)cpu[i]=cpuPatchFit(ref,src,raw,validity,batch[i]);
                const auto cpuEnd=std::chrono::steady_clock::now();
                const auto warmStart=std::chrono::steady_clock::now();
                std::vector<cv::Vec4f> warm;
                if(!context->runFits(ref,src,raw,validity,batch,warm)){context->healthy=false;reason=context->failure;fits.clear();return false;}
                const double warmMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count();
                bool match=true;int usable=0,missing=0;
                for(size_t i=0;i<batch.size();++i) {
                    if(output[i][3]) {
                        ++usable;
                        if(!cpu[i][3] || std::hypot(cpu[i][0]-output[i][0],cpu[i][1]-output[i][1])>.05 || std::abs(cpu[i][2]-output[i][2])>.002)match=false;
                    } else ++missing;
                    if(warm[i][3] && (!cpu[i][3] || std::hypot(cpu[i][0]-warm[i][0],cpu[i][1]-warm[i][1])>.05 || std::abs(cpu[i][2]-warm[i][2])>.002))match=false;
                    if(output[i][3]!=warm[i][3])match=false;
#ifdef HACHICAM_GPU_TEST_CONTROL
                    __android_log_print(ANDROID_LOG_INFO,"HachiCam-GPU-Test","ECC probe i=%zu CPU=%g,%g,%g,%g GPU=%g,%g,%g,%g",
                        i,cpu[i][0],cpu[i][1],cpu[i][2],cpu[i][3],output[i][0],output[i][1],output[i][2],output[i][3]);
#endif
                }
                const double gpuMs=std::chrono::duration<double,std::milli>(gpuEnd-start).count();
                const double cpuMs=std::chrono::duration<double,std::milli>(cpuEnd-gpuEnd).count();
                // Measure the hybrid path's real CPU completion cost. The CPU
                // baseline above is reused for results, never lost on GPU misses.
                const auto fallbackStart=std::chrono::steady_clock::now();
                std::vector<cv::Vec4f> fallback(batch.size());
                #pragma omp parallel for schedule(dynamic)
                for(int i=0;i<int(batch.size());++i)if(!output[i][3])fallback[i]=cpuPatchFit(ref,src,raw,validity,batch[i]);
                const double fallbackMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-fallbackStart).count();
                const double batches=double((patches.size()+15)/16);
                const double estimatedGpu=gpuMs+(batches-1)*warmMs+batches*fallbackMs+selectionMs;
                const double estimatedCpu=cpuMs*double(patches.size())/batch.size();
                context->fitPolicy[key]=match && usable && estimatedGpu<=estimatedCpu*.90 ? 1 : -1;
                __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf",
                    "GPU ECC calibration v4 key=%d patches=%zu/%zu coldCallMs=%.3f warmCallMs=%.3f cpuBatchMs=%.3f fallbackMs=%.3f selectorMs=%.3f estimatedGpuMs=%.3f estimatedCpuMs=%.3f conforming=%d usable=%d missing=%d preferred=%s",
                    key,batch.size(),patches.size(),gpuMs,warmMs,cpuMs,fallbackMs,selectionMs,estimatedGpu,estimatedCpu,int(match),usable,missing,context->fitPolicy[key]>0 ? "Vulkan" : "CPU");
                if(context->fitPolicy[key]<0) {
                    for(size_t i=0;i<cpu.size();++i){if(cpu[i][3])cpu[i][3]=2;fits[begin+i]=cpu[i];}
                    reason="calibration reused CPU fits; remaining patches use CPU";return true;
                }
                for(size_t i=0;i<cpu.size();++i)if(!output[i][3] && cpu[i][3]){output[i]=cpu[i];output[i][3]=2;}
            }
#endif
            std::copy(output.begin(),output.end(),fits.begin()+begin);
        }
        reason="validated batched ECC proposals";return true;
    } catch(const std::exception&) {reason="GPU ECC host failure";fits.clear();return false;}
}
bool gpuFittingAvailable(size_t pixels,bool highlight) {
#ifdef HACHICAM_GPU_TEST_CONTROL
    if(std::getenv("HACHICAM_FORCE_CPU"))return false;
#endif
    if(pixels>16000000)return false;
    auto* context=sharedContext();if(!context)return false;
    std::lock_guard<std::mutex> lock(context->guard);
    const int base=highlight ? 3 : 0;
    return context->healthy && context->eccReady && (context->fitPolicy[base]>=0 || context->fitPolicy[base+1]>=0 || context->fitPolicy[base+2]>=0);
}
#ifdef HACHICAM_GPU_TEST_CONTROL
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
