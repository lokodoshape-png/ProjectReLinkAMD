// Minimal Vulkan compute harness: compiles a SPIR-V compute shader, prints the driver's pipeline executable statistics
// (RADV null device: ACO's real RDNA ISA stats), and optionally runs it on given images (lavapipe), timing it.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#define CK(x) do{VkResult r_=(x); if(r_){fprintf(stderr,"%s = %d (line %d)\n",#x,r_,__LINE__);exit(1);}}while(0)
struct Img{uint32_t w,h;VkFormat f;size_t bpp;std::string file;bool out;bool linear;VkImage im;VkDeviceMemory mem;VkImageView v;};
static VkFormat fmt(const char*s,size_t&b){if(!strcmp(s,"r16f")){b=2;return VK_FORMAT_R16_SFLOAT;}if(!strcmp(s,"rgba16f")){b=8;return VK_FORMAT_R16G16B16A16_SFLOAT;}
 if(!strcmp(s,"rgba8")){b=4;return VK_FORMAT_R8G8B8A8_UNORM;}if(!strcmp(s,"rgba32f")){b=16;return VK_FORMAT_R32G32B32A32_SFLOAT;}fprintf(stderr,"fmt?\n");exit(1);}
static std::vector<char> rd(const std::string&f){FILE*p=fopen(f.c_str(),"rb");if(!p){perror(f.c_str());exit(1);}std::vector<char> v;char b[65536];size_t n;while((n=fread(b,1,sizeof b,p))>0)v.insert(v.end(),b,b+n);fclose(p);return v;}
VkDevice dev;VkPhysicalDevice pd;
static uint32_t memtype(uint32_t bits,VkMemoryPropertyFlags want){VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(pd,&mp);
 for(uint32_t i=0;i<mp.memoryTypeCount;i++)if((bits>>i&1)&&(mp.memoryTypes[i].propertyFlags&want)==want)return i;fprintf(stderr,"mem?\n");exit(1);}
int main(int argc,char**argv){
 if(argc<3){fprintf(stderr,"harness spv stats|run [spec=N] [pc=hex] [iters=N] [in:WxH:fmt:n|l:file] [out:WxH:fmt:file]\n");return 1;}
 std::string spvf=argv[1],mode=argv[2];int spec=-1,iters=1;std::vector<uint8_t> pc;std::vector<Img> imgs;
 for(int i=3;i<argc;i++){std::string a=argv[i];
  if(a.rfind("spec=",0)==0)spec=atoi(a.c_str()+5);
  else if(a.rfind("iters=",0)==0)iters=atoi(a.c_str()+6);
  else if(a.rfind("pc=",0)==0){for(size_t k=3;k+1<a.size();k+=2)pc.push_back((uint8_t)strtoul(a.substr(k,2).c_str(),0,16));}
  else{Img m{};char kind[8],f[16],s[4],file[512];
   if(a.rfind("in:",0)==0){if(sscanf(a.c_str(),"in:%ux%u:%15[^:]:%3[^:]:%511s",&m.w,&m.h,f,s,file)!=5){fprintf(stderr,"bad %s\n",a.c_str());return 1;}m.linear=s[0]=='l';}
   else{if(sscanf(a.c_str(),"out:%ux%u:%15[^:]:%511s",&m.w,&m.h,f,file)!=4){fprintf(stderr,"bad %s\n",a.c_str());return 1;}m.out=true;}
   (void)kind;m.f=fmt(f,m.bpp);m.file=file;imgs.push_back(m);}}
 VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};ai.apiVersion=VK_API_VERSION_1_3;
 VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&ai;VkInstance inst;CK(vkCreateInstance(&ici,0,&inst));
 uint32_t n=1;vkEnumeratePhysicalDevices(inst,&n,&pd);VkPhysicalDeviceProperties pr;vkGetPhysicalDeviceProperties(pd,&pr);
 float prio=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueCount=1;q.pQueuePriorities=&prio;
 VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR pe{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};pe.pipelineExecutableInfo=1;
 VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};f12.pNext=&pe;
 VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f2.pNext=&f12;vkGetPhysicalDeviceFeatures2(pd,&f2);
 const char* ext[]={"VK_KHR_pipeline_executable_properties"};
 VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.pNext=&f2;dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&q;
 dci.enabledExtensionCount=pe.pipelineExecutableInfo?1:0;dci.ppEnabledExtensionNames=ext;CK(vkCreateDevice(pd,&dci,0,&dev));
 auto code=rd(spvf);VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=code.size();mi.pCode=(const uint32_t*)code.data();VkShaderModule sm;CK(vkCreateShaderModule(dev,&mi,0,&sm));
 std::vector<VkDescriptorSetLayoutBinding> lb;for(size_t i=0;i<imgs.size();i++)lb.push_back({(uint32_t)i,imgs[i].out?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,0});
 if(mode=="stats"&&imgs.empty()){fprintf(stderr,"stats needs the bindings too\n");return 1;}
 VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=lb.size();dl.pBindings=lb.data();VkDescriptorSetLayout dsl;CK(vkCreateDescriptorSetLayout(dev,&dl,0,&dsl));
 VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT,0,(uint32_t)(pc.size()?pc.size():4)};
 VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&dsl;pl.pushConstantRangeCount=pc.size()?1:0;pl.pPushConstantRanges=&pcr;VkPipelineLayout lay;CK(vkCreatePipelineLayout(dev,&pl,0,&lay));
 VkSpecializationMapEntry me{0,0,4};VkSpecializationInfo si{1,&me,4,&spec};
 VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.flags=pe.pipelineExecutableInfo?VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR:0;
 cp.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,0,0,VK_SHADER_STAGE_COMPUTE_BIT,sm,"main",spec>=0?&si:nullptr};cp.layout=lay;
 VkPipeline pipe;auto t0=std::chrono::steady_clock::now();CK(vkCreateComputePipelines(dev,0,1,&cp,0,&pipe));
 double ctime=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
 printf("device: %s  compile_ms=%.1f\n",pr.deviceName,ctime);
 if(pe.pipelineExecutableInfo){auto gp=(PFN_vkGetPipelineExecutableStatisticsKHR)vkGetDeviceProcAddr(dev,"vkGetPipelineExecutableStatisticsKHR");
  VkPipelineExecutableInfoKHR ei{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};ei.pipeline=pipe;uint32_t c=0;gp(dev,&ei,&c,0);
  std::vector<VkPipelineExecutableStatisticKHR> st(c,{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});gp(dev,&ei,&c,st.data());
  for(auto&s:st){printf("  %s=",s.name);if(s.format==VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR)printf("%llu",(unsigned long long)s.value.u64);
   else if(s.format==VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR)printf("%lld",(long long)s.value.i64);else if(s.format==VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR)printf("%g",s.value.f64);else printf("%d",s.value.b32);printf("\n");}}
 if(mode!="run")return 0;
 VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};VkCommandPool pool;CK(vkCreateCommandPool(dev,&cpi,0,&pool));VkQueue qu;vkGetDeviceQueue(dev,0,0,&qu);
 auto onecmd=[&](auto fn){VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.commandBufferCount=1;VkCommandBuffer cb;CK(vkAllocateCommandBuffers(dev,&ca,&cb));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkBeginCommandBuffer(cb,&bi);fn(cb);vkEndCommandBuffer(cb);
  VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&cb;CK(vkQueueSubmit(qu,1,&s,0));vkQueueWaitIdle(qu);vkFreeCommandBuffers(dev,pool,1,&cb);};
 auto mkbuf=[&](size_t sz,VkBuffer&b,VkDeviceMemory&m){VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=sz;bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;CK(vkCreateBuffer(dev,&bc,0,&b));
  VkMemoryRequirements r;vkGetBufferMemoryRequirements(dev,b,&r);VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=memtype(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);CK(vkAllocateMemory(dev,&a,0,&m));vkBindBufferMemory(dev,b,m,0);};
 VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;VkSampler snear,slin;CK(vkCreateSampler(dev,&sc,0,&snear));sc.magFilter=sc.minFilter=VK_FILTER_LINEAR;CK(vkCreateSampler(dev,&sc,0,&slin));
 std::vector<VkDescriptorPoolSize> ps={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,16}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=2;dp.pPoolSizes=ps.data();VkDescriptorPool dpool;CK(vkCreateDescriptorPool(dev,&dp,0,&dpool));
 VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=dpool;da.descriptorSetCount=1;da.pSetLayouts=&dsl;VkDescriptorSet ds;CK(vkAllocateDescriptorSets(dev,&da,&ds));
 uint32_t gw=0,gh=0;
 for(size_t i=0;i<imgs.size();i++){Img&m=imgs[i];VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ic.imageType=VK_IMAGE_TYPE_2D;ic.format=m.f;ic.extent={m.w,m.h,1};ic.mipLevels=1;ic.arrayLayers=1;ic.samples=VK_SAMPLE_COUNT_1_BIT;
  ic.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;CK(vkCreateImage(dev,&ic,0,&m.im));
  VkMemoryRequirements r;vkGetImageMemoryRequirements(dev,m.im,&r);VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=memtype(r.memoryTypeBits,0);CK(vkAllocateMemory(dev,&a,0,&m.mem));vkBindImageMemory(dev,m.im,m.mem,0);
  VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vc.image=m.im;vc.viewType=VK_IMAGE_VIEW_TYPE_2D;vc.format=m.f;vc.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};CK(vkCreateImageView(dev,&vc,0,&m.v));
  size_t sz=(size_t)m.w*m.h*m.bpp;VkBuffer b;VkDeviceMemory bm;mkbuf(sz,b,bm);
  if(!m.out){auto d=rd(m.file);if(d.size()!=sz){fprintf(stderr,"%s: %zu bytes, want %zu\n",m.file.c_str(),d.size(),sz);return 1;}void*p;vkMapMemory(dev,bm,0,sz,0,&p);memcpy(p,d.data(),sz);vkUnmapMemory(dev,bm);}
  else{gw=m.w;gh=m.h;}
  onecmd([&](VkCommandBuffer cb){VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};ib.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.newLayout=VK_IMAGE_LAYOUT_GENERAL;ib.image=m.im;ib.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};ib.srcQueueFamilyIndex=ib.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
   vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,0,0,0,1,&ib);
   if(!m.out){VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={m.w,m.h,1};vkCmdCopyBufferToImage(cb,b,m.im,VK_IMAGE_LAYOUT_GENERAL,1,&c);}});
  VkDescriptorImageInfo ii{m.out?VK_NULL_HANDLE:(m.linear?slin:snear),m.v,VK_IMAGE_LAYOUT_GENERAL};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=ds;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=lb[i].descriptorType;w.pImageInfo=&ii;vkUpdateDescriptorSets(dev,1,&w,0,0);}
 VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qp.queryType=VK_QUERY_TYPE_TIMESTAMP;qp.queryCount=2;VkQueryPool qpool;CK(vkCreateQueryPool(dev,&qp,0,&qpool));
 double best=1e30;
 for(int it=0;it<iters;it++){onecmd([&](VkCommandBuffer cb){vkCmdResetQueryPool(cb,qpool,0,2);vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipe);vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,lay,0,1,&ds,0,0);
   if(pc.size())vkCmdPushConstants(cb,lay,VK_SHADER_STAGE_COMPUTE_BIT,0,pc.size(),pc.data());vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,qpool,0);vkCmdDispatch(cb,(gw+7)/8,(gh+7)/8,1);vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,qpool,1);});
  uint64_t ts[2];vkGetQueryPoolResults(dev,qpool,0,2,sizeof ts,ts,8,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT);double ms=(ts[1]-ts[0])*pr.limits.timestampPeriod/1e6;if(ms<best)best=ms;}
 printf("dispatch_ms_best=%.3f (of %d)\n",best,iters);
 for(auto&m:imgs)if(m.out){size_t sz=(size_t)m.w*m.h*m.bpp;VkBuffer b;VkDeviceMemory bm;mkbuf(sz,b,bm);
  onecmd([&](VkCommandBuffer cb){VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&mb,0,0,0,0);
   VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={m.w,m.h,1};vkCmdCopyImageToBuffer(cb,m.im,VK_IMAGE_LAYOUT_GENERAL,b,1,&c);});
  void*p;vkMapMemory(dev,bm,0,sz,0,&p);FILE*f=fopen(m.file.c_str(),"wb");fwrite(p,1,sz,f);fclose(f);}
 return 0;}
