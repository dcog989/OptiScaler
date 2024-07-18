#pragma once
#include "dllmain.h"
#include "resource.h"

#include "Logger.h"
#include "Util.h"
#include "NVNGX_Proxy.h"

#include "imgui/imgui_overlay_dx12.h"

#include <vulkan/vulkan_core.h>

#pragma warning (disable : 4996)

typedef BOOL(WINAPI* PFN_FreeLibrary)(HMODULE lpLibrary);
typedef HMODULE(WINAPI* PFN_LoadLibraryA)(LPCSTR lpLibFileName);
typedef HMODULE(WINAPI* PFN_LoadLibraryW)(LPCWSTR lpLibFileName);
typedef HMODULE(WINAPI* PFN_LoadLibraryExA)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
typedef HMODULE(WINAPI* PFN_LoadLibraryExW)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
typedef const char* (CDECL* PFN_wine_get_version)(void);

typedef struct VkDummyProps
{
    VkStructureType    sType;
    void* pNext;
} VkDummyProps;

static PFN_FreeLibrary o_FreeLibrary = nullptr;
static PFN_LoadLibraryA o_LoadLibraryA = nullptr;
static PFN_LoadLibraryW o_LoadLibraryW = nullptr;
static PFN_LoadLibraryExA o_LoadLibraryExA = nullptr;
static PFN_LoadLibraryExW o_LoadLibraryExW = nullptr;
static PFN_vkGetPhysicalDeviceProperties o_vkGetPhysicalDeviceProperties = nullptr;
static PFN_vkGetPhysicalDeviceProperties2 o_vkGetPhysicalDeviceProperties2 = nullptr;
static PFN_vkGetPhysicalDeviceProperties2KHR o_vkGetPhysicalDeviceProperties2KHR = nullptr;

static PFN_vkEnumerateInstanceExtensionProperties o_vkEnumerateInstanceExtensionProperties = nullptr;
static PFN_vkEnumerateDeviceExtensionProperties o_vkEnumerateDeviceExtensionProperties = nullptr;
static PFN_vkCreateDevice o_vkCreateDevice = nullptr;

static uint32_t vkEnumerateInstanceExtensionPropertiesCount = 0;
static uint32_t vkEnumerateDeviceExtensionPropertiesCount = 0;

static std::string nvngxA("nvngx.dll");
static std::string nvngxExA("nvngx");
static std::wstring nvngxW(L"nvngx.dll");
static std::wstring nvngxExW(L"nvngx");

static std::string nvapiA("nvapi64.dll");
static std::string nvapiExA("nvapi64");
static std::wstring nvapiW(L"nvapi64.dll");
static std::wstring nvapiExW(L"nvapi64");

static std::string dllNameA;
static std::string dllNameExA;
static std::wstring dllNameW;
static std::wstring dllNameExW;

static int loadCount = 0;
static bool dontCount = false;

void AttachHooks();
void DetachHooks();

static HMODULE LoadNvApi()
{
    HMODULE nvapi = nullptr;

    if (Config::Instance()->NvapiDllPath.has_value())
    {
        nvapi = o_LoadLibraryA(Config::Instance()->NvapiDllPath->c_str());

        if (nvapi != nullptr)
        {
            spdlog::info("LoadNvApi nvapi64.dll loaded from {0}", Config::Instance()->NvapiDllPath.value());
            return nvapi;
        }
    }

    if (nvapi == nullptr)
    {
        auto localPath = Util::DllPath().parent_path() / "nvapi64.dll";
        nvapi = o_LoadLibraryA(localPath.string().c_str());

        if (nvapi != nullptr)
        {
            spdlog::info("LoadNvApi nvapi64.dll loaded from {0}", localPath.string());
            return nvapi;
        }
    }

    if (nvapi == nullptr)
    {
        nvapi = o_LoadLibraryA("nvapi64.dll");

        if (nvapi != nullptr)
        {
            spdlog::info("LoadNvApi Fallback! nvapi64.dll loaded from system");
            return nvapi;
        }
    }

    return nullptr;
}

#pragma region Load & Free Library hooks

static BOOL hkFreeLibrary(HMODULE lpLibrary)
{
    if (lpLibrary == nullptr)
        return FALSE;

    if (lpLibrary == dllModule)
    {
        loadCount--;
        spdlog::info("hkFreeLibrary call for this module loadCount: {0}", loadCount);

        if (loadCount == 0)
            return o_FreeLibrary(lpLibrary);
        else
            return TRUE;
    }

    return o_FreeLibrary(lpLibrary);
}

static HMODULE hkLoadLibraryA(LPCSTR lpLibFileName)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::string libName(lpLibFileName);
    std::string lcaseLibName(libName);

    for (size_t i = 0; i < lcaseLibName.size(); i++)
        lcaseLibName[i] = std::tolower(lcaseLibName[i]);

    size_t pos;

#ifdef DEBUG
    spdlog::trace("hkLoadLibraryA call: {0}", lcaseLibName);
#endif // DEBUG

    // If Opti is not loading nvngx.dll
    if (!Config::Instance()->dlssDisableHook)
    {
        // exe path
        auto exePath = Util::ExePath().parent_path().string();

        for (size_t i = 0; i < exePath.size(); i++)
            exePath[i] = std::tolower(exePath[i]);

        auto pos2 = lcaseLibName.rfind(exePath);
        pos = lcaseLibName.rfind(nvngxA);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvngxA.size()) &&
            (!Config::Instance()->HookOriginalNvngxOnly.value_or(false) || pos2 == std::string::npos))
        {
            spdlog::info("hkLoadLibraryA nvngx call: {0}, returning this dll!", libName);
            return dllModule;
        }
    }

    // NvApi64.dll
    if (Config::Instance()->OverrideNvapiDll.value_or(false))
    {
        pos = lcaseLibName.rfind(nvapiA);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvapiA.size()))
        {
            spdlog::info("hkLoadLibraryA {0} call!", libName);

            auto nvapi = LoadNvApi();

            if (nvapi != nullptr)
                return nvapi;
        }
    }

    // Opti dll
    pos = lcaseLibName.rfind(dllNameA);
    if (pos != std::string::npos && pos == (lcaseLibName.size() - dllNameA.size()))
    {
        spdlog::info("hkLoadLibraryA {0} call returning this dll!", libName);

        if (!dontCount)
            loadCount++;

        return dllModule;
    }

    dontCount = true;
    auto result = o_LoadLibraryA(lpLibFileName);
    dontCount = false;
    return result;
}

static HMODULE hkLoadLibraryW(LPCWSTR lpLibFileName)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::wstring libName(lpLibFileName);
    std::wstring lcaseLibName(libName);

    for (size_t i = 0; i < lcaseLibName.size(); i++)
        lcaseLibName[i] = std::tolower(lcaseLibName[i]);

    std::string lcaseLibNameA(lcaseLibName.length(), 0);
    std::transform(lcaseLibName.begin(), lcaseLibName.end(), lcaseLibNameA.begin(), [](wchar_t c) { return (char)c; });

#ifdef DEBUG
    spdlog::trace("hkLoadLibraryW call: {0}", lcaseLibNameA);
#endif

    size_t pos;

    // If Opti is not loading nvngx.dll
    if (!Config::Instance()->dlssDisableHook)
    {
        // exe path
        auto exePathW = Util::ExePath().parent_path().wstring();

        for (size_t i = 0; i < exePathW.size(); i++)
            exePathW[i] = std::tolower(exePathW[i]);

        auto pos2 = lcaseLibName.rfind(exePathW);
        pos = lcaseLibName.rfind(nvngxW);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvngxW.size()) &&
            (!Config::Instance()->HookOriginalNvngxOnly.value_or(false) || pos2 == std::string::npos))
        {
            spdlog::info("hkLoadLibraryW nvngx call: {0}, returning this dll!", lcaseLibNameA);
            return dllModule;
        }
    }

    // NvApi64.dll
    if (Config::Instance()->OverrideNvapiDll.value_or(false))
    {
        pos = lcaseLibName.rfind(nvapiW);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvapiW.size()))
        {
            spdlog::info("hkLoadLibraryW {0} call!", lcaseLibNameA);

            auto nvapi = LoadNvApi();

            if (nvapi != nullptr)
                return nvapi;
        }
    }

    // Opti dll
    pos = lcaseLibName.rfind(dllNameW);
    if (pos != std::string::npos && pos == (lcaseLibName.size() - dllNameW.size()))
    {
        spdlog::info("hkLoadLibraryW {0} call, returning this dll!", lcaseLibNameA);

        if (!dontCount)
            loadCount++;

        return dllModule;
    }

    dontCount = true;
    auto result = o_LoadLibraryW(lpLibFileName);
    dontCount = false
        ;
    return result;
}

static HMODULE hkLoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::string libName(lpLibFileName);
    std::string lcaseLibName(libName);

    for (size_t i = 0; i < lcaseLibName.size(); i++)
        lcaseLibName[i] = std::tolower(lcaseLibName[i]);

#ifdef DEBUG
    spdlog::trace("hkLoadLibraryExA call: {0}", lcaseLibName);
#endif

    size_t pos;

    // If Opti is not loading nvngx.dll
    if (!Config::Instance()->dlssDisableHook)
    {
        // exe path
        auto exePath = Util::ExePath().parent_path().string();

        for (size_t i = 0; i < exePath.size(); i++)
            exePath[i] = std::tolower(exePath[i]);

        auto pos2 = lcaseLibName.rfind(exePath);
        pos = lcaseLibName.rfind(nvngxA);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvngxA.size()) &&
            (!Config::Instance()->HookOriginalNvngxOnly.value_or(false) || pos2 == std::string::npos))
        {
            spdlog::info("hkLoadLibraryExA nvngx call: {0}, returning this dll!", libName);
            return dllModule;
        }

        pos = lcaseLibName.rfind(nvngxExA);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvngxExA.size()) &&
            (!Config::Instance()->HookOriginalNvngxOnly.value_or(false) || pos2 == std::string::npos))
        {
            spdlog::info("hkLoadLibraryExA nvngx call: {0}, returning this dll!", libName);
            return dllModule;
        }
    }

    // NvApi64.dll
    if (Config::Instance()->OverrideNvapiDll.value_or(false))
    {
        pos = lcaseLibName.rfind(nvapiExA);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvapiExA.size()))
        {
            spdlog::info("hkLoadLibraryExA {0} call!", libName);

            auto nvapi = LoadNvApi();

            if (nvapi != nullptr)
                return nvapi;
        }

        pos = lcaseLibName.rfind(nvapiA);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvapiA.size()))
        {
            spdlog::info("hkLoadLibraryExA {0} call!", libName);

            auto nvapi = LoadNvApi();

            if (nvapi != nullptr)
                return nvapi;
        }
    }

    // Opti dll
    pos = lcaseLibName.rfind(dllNameA);
    if (pos != std::string::npos && pos == (lcaseLibName.size() - dllNameA.size()))
    {
        spdlog::info("hkLoadLibraryExA {0} call, returning this dll!", libName);

        if (!dontCount)
            loadCount++;

        return dllModule;
    }

    // Opti dll
    pos = lcaseLibName.rfind(dllNameExA);
    if (pos != std::string::npos && pos == (lcaseLibName.size() - dllNameExA.size()))
    {
        spdlog::info("hkLoadLibraryExA {0} call, returning this dll!", libName);

        if (!dontCount)
            loadCount++;

        return dllModule;
    }

    dontCount = true;
    auto result = o_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
    dontCount = false;

    return result;
}

static HMODULE hkLoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::wstring libName(lpLibFileName);
    std::wstring lcaseLibName(libName);

    for (size_t i = 0; i < lcaseLibName.size(); i++)
        lcaseLibName[i] = std::tolower(lcaseLibName[i]);

    std::string lcaseLibNameA(lcaseLibName.length(), 0);
    std::transform(lcaseLibName.begin(), lcaseLibName.end(), lcaseLibNameA.begin(), [](wchar_t c) { return (char)c; });

#ifdef DEBUG
    spdlog::trace("hkLoadLibraryExW call: {0}", lcaseLibNameA);
#endif

    size_t pos;

    // If Opti is not loading nvngx.dll
    if (!Config::Instance()->dlssDisableHook)
    {
        // exe path
        auto exePathW = Util::ExePath().parent_path().wstring();

        for (size_t i = 0; i < exePathW.size(); i++)
            exePathW[i] = std::tolower(exePathW[i]);

        auto pos2 = lcaseLibName.rfind(exePathW);
        pos = lcaseLibName.rfind(nvngxW);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvngxW.size()) &&
            (!Config::Instance()->HookOriginalNvngxOnly.value_or(false) || pos2 == std::string::npos))
        {
            spdlog::info("hkLoadLibraryExW nvngx call: {0}, returning this dll!", lcaseLibNameA);
            return dllModule;
        }

        pos = lcaseLibName.rfind(nvngxExW);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvngxExW.size()) &&
            (!Config::Instance()->HookOriginalNvngxOnly.value_or(false) || pos2 == std::string::npos))
        {
            spdlog::info("hkLoadLibraryExW nvngx call: {0}, returning this dll!", lcaseLibNameA);
            return dllModule;
        }
    }

    // NvApi64.dll
    if (Config::Instance()->OverrideNvapiDll.value_or(false))
    {
        pos = lcaseLibName.rfind(nvapiExW);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvapiExW.size()))
        {
            spdlog::info("hkLoadLibraryExW {0} call!", lcaseLibNameA);

            auto nvapi = LoadNvApi();

            if (nvapi != nullptr)
                return nvapi;
        }

        pos = lcaseLibName.rfind(nvapiW);

        if (pos != std::string::npos && pos == (lcaseLibName.size() - nvapiW.size()))
        {
            spdlog::info("hkLoadLibraryExW {0} call!", lcaseLibNameA);

            auto nvapi = LoadNvApi();

            if (nvapi != nullptr)
                return nvapi;
        }
    }

    // Opti dll
    pos = lcaseLibName.rfind(dllNameW);
    if (pos != std::string::npos && pos == (lcaseLibName.size() - dllNameW.size()))
    {
        spdlog::info("hkLoadLibraryExW {0} call, returning this dll!", lcaseLibNameA);

        if (!dontCount)
            loadCount++;

        return dllModule;
    }

    // Opti dll
    pos = lcaseLibName.rfind(dllNameExW);
    if (pos != std::string::npos && pos == (lcaseLibName.size() - dllNameExW.size()))
    {
        spdlog::info("hkLoadLibraryExW {0} call, returning this dll!", lcaseLibNameA);

        if (!dontCount)
            loadCount++;

        return dllModule;
    }

    dontCount = true;
    auto result = o_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
    dontCount = false;

    return result;
}

#pragma endregion

#pragma region Vulkan Hooks

static void hkvkGetPhysicalDeviceProperties(VkPhysicalDevice physical_device, VkPhysicalDeviceProperties* properties)
{
    o_vkGetPhysicalDeviceProperties(physical_device, properties);

    std::strcpy(properties->deviceName, "NVIDIA GeForce RTX 4090");
    properties->vendorID = 0x10de;
    properties->deviceID = 0x2684;
    properties->driverVersion = VK_MAKE_API_VERSION(559, 0, 0, 0);
}

static void hkvkGetPhysicalDeviceProperties2(VkPhysicalDevice phys_dev, VkPhysicalDeviceProperties2* properties2)
{
    o_vkGetPhysicalDeviceProperties2(phys_dev, properties2);

    std::strcpy(properties2->properties.deviceName, "NVIDIA GeForce RTX 4090");
    properties2->properties.vendorID = 0x10de;
    properties2->properties.deviceID = 0x2684;
    properties2->properties.driverVersion = VK_MAKE_API_VERSION(559, 0, 0, 0);

    auto next = (VkDummyProps*)properties2->pNext;

    while (next != nullptr)
    {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES)
        {
            auto ddp = (VkPhysicalDeviceDriverProperties*)(void*)next;
            ddp->driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY;
            std::strcpy(ddp->driverName, "NVIDIA");
            std::strcpy(ddp->driverInfo, "559.0");
        }

        next = (VkDummyProps*)next->pNext;
    }
}

static void hkvkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice phys_dev, VkPhysicalDeviceProperties2* properties2)
{
    o_vkGetPhysicalDeviceProperties2KHR(phys_dev, properties2);

    std::strcpy(properties2->properties.deviceName, "NVIDIA GeForce RTX 4090");
    properties2->properties.vendorID = 0x10de;
    properties2->properties.deviceID = 0x2684;
    properties2->properties.driverVersion = VK_MAKE_API_VERSION(559, 0, 0, 0);

    auto next = (VkDummyProps*)properties2->pNext;

    while (next != nullptr)
    {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES)
        {
            auto ddp = (VkPhysicalDeviceDriverProperties*)(void*)next;
            ddp->driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY;
            std::strcpy(ddp->driverName, "NVIDIA");
            std::strcpy(ddp->driverInfo, "559.0");
        }

        next = (VkDummyProps*)next->pNext;
    }
}

static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    spdlog::debug("hkvkCreateDevice");

    if (!Config::Instance()->VulkanExtensionSpoofing.value_or(false))
    {
        spdlog::debug("hkvkCreateDevice extension spoofing is disabled");
        return o_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    }

    std::vector<const char*> newExtensionList;


    auto bVK_KHR_get_memory_requirements2 = false;
    //auto GL_EXT_shader_image_load_formatted = false;

    spdlog::debug("hkvkCreateDevice checking extensions and removing VK_NVX_BINARY_IMPORT & VK_NVX_IMAGE_VIEW_HANDLE from list");
    for (size_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
    {
        if (std::strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_NVX_BINARY_IMPORT_EXTENSION_NAME) == 0 || std::strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME) == 0)
        {
            spdlog::debug("hkvkCreateDevice removing {0}", pCreateInfo->ppEnabledExtensionNames[i]);
        }
        else
        {
            spdlog::debug("hkvkCreateDevice adding {0}", pCreateInfo->ppEnabledExtensionNames[i]);
            newExtensionList.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
        }

        if (std::strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) == 0)
            bVK_KHR_get_memory_requirements2 = true;
        //else if (std::strcmp(pCreateInfo->ppEnabledExtensionNames[i], "GL_EXT_shader_image_load_formatted") == 0)
        //    GL_EXT_shader_image_load_formatted = true;
    }

    if(!bVK_KHR_get_memory_requirements2)
        newExtensionList.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);

    //if(!GL_EXT_shader_image_load_formatted)
    //    newExtensionList.push_back("GL_EXT_shader_image_load_formatted");

    pCreateInfo->enabledExtensionCount = static_cast<uint32_t>(newExtensionList.size());
    pCreateInfo->ppEnabledExtensionNames = newExtensionList.data();
    
    spdlog::debug("hkvkCreateDevice final extension count: {0}", pCreateInfo->enabledExtensionCount);
    spdlog::debug("hkvkCreateDevice final extensions:");
    for (size_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
        spdlog::debug("hkvkCreateDevice   {0}", pCreateInfo->ppEnabledExtensionNames[i]);

    auto result = o_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    spdlog::debug("hkvkCreateDevice o_vkCreateDevice result: {0:X}", (UINT)result);

    return result;
}

static VkResult hkvkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    auto count = *pPropertyCount;
    auto result = o_vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);

    if (result != VK_SUCCESS)
        return result;

    if (pLayerName == nullptr && pProperties == nullptr && count == 0)
    {
        *pPropertyCount += 2;
        vkEnumerateDeviceExtensionPropertiesCount = *pPropertyCount;
        return result;
    }

    if (pLayerName == nullptr && pProperties != nullptr && *pPropertyCount > 0)
    {
        if (count == vkEnumerateDeviceExtensionPropertiesCount)
            *pPropertyCount = count;

        VkExtensionProperties bi{ VK_NVX_BINARY_IMPORT_EXTENSION_NAME, VK_NVX_BINARY_IMPORT_SPEC_VERSION };
        memcpy(&pProperties[*pPropertyCount - 1], &bi, sizeof(VkExtensionProperties));

        VkExtensionProperties ivh{ VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME, VK_NVX_IMAGE_VIEW_HANDLE_SPEC_VERSION };
        memcpy(&pProperties[*pPropertyCount - 2], &ivh, sizeof(VkExtensionProperties));
    }

    return result;
}

static VkResult hkvkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
    auto count = *pPropertyCount;
    auto result = o_vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);

    if (result != VK_SUCCESS)
        return result;

    if (pLayerName == nullptr && pProperties == nullptr && count == 0)
    {
        *pPropertyCount += 2;
        vkEnumerateInstanceExtensionPropertiesCount = *pPropertyCount;
        return result;
    }

    if (pLayerName == nullptr && pProperties != nullptr && *pPropertyCount > 0)
    {
        if (vkEnumerateInstanceExtensionPropertiesCount == count)
            *pPropertyCount = count;

        VkExtensionProperties bi{ VK_NVX_BINARY_IMPORT_EXTENSION_NAME, VK_NVX_BINARY_IMPORT_SPEC_VERSION };
        memcpy(&pProperties[*pPropertyCount - 1], &bi, sizeof(VkExtensionProperties));

        VkExtensionProperties ivh{ VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME, VK_NVX_IMAGE_VIEW_HANDLE_SPEC_VERSION };
        memcpy(&pProperties[*pPropertyCount - 2], &ivh, sizeof(VkExtensionProperties));
    }

    return result;
}

#pragma endregion

static void DetachHooks()
{
    if (o_LoadLibraryA != nullptr || o_LoadLibraryW != nullptr || o_LoadLibraryExA != nullptr || o_LoadLibraryExW != nullptr)
    {
        DetourTransactionBegin();

        DetourUpdateThread(GetCurrentThread());

        if (o_FreeLibrary)
        {
            DetourDetach(&(PVOID&)o_FreeLibrary, hkFreeLibrary);
            o_FreeLibrary = nullptr;
        }

        if (o_LoadLibraryA)
        {
            DetourDetach(&(PVOID&)o_LoadLibraryA, hkLoadLibraryA);
            o_LoadLibraryA = nullptr;
        }

        if (o_LoadLibraryW)
        {
            DetourDetach(&(PVOID&)o_LoadLibraryW, hkLoadLibraryW);
            o_LoadLibraryW = nullptr;
        }

        if (o_LoadLibraryExA)
        {
            DetourDetach(&(PVOID&)o_LoadLibraryExA, hkLoadLibraryExA);
            o_LoadLibraryExA = nullptr;
        }

        if (o_LoadLibraryExW)
        {
            DetourDetach(&(PVOID&)o_LoadLibraryExW, hkLoadLibraryExW);
            o_LoadLibraryExW = nullptr;
        }

        if (o_vkGetPhysicalDeviceProperties)
        {
            DetourDetach(&(PVOID&)o_vkGetPhysicalDeviceProperties, hkvkGetPhysicalDeviceProperties);
            o_vkGetPhysicalDeviceProperties = nullptr;
        }

        if (o_vkGetPhysicalDeviceProperties2)
        {
            DetourDetach(&(PVOID&)o_vkGetPhysicalDeviceProperties2, hkvkGetPhysicalDeviceProperties2);
            o_vkGetPhysicalDeviceProperties2 = nullptr;
        }

        if (o_vkGetPhysicalDeviceProperties2KHR)
        {
            DetourDetach(&(PVOID&)o_vkGetPhysicalDeviceProperties2KHR, hkvkGetPhysicalDeviceProperties2KHR);
            o_vkGetPhysicalDeviceProperties2KHR = nullptr;
        }

        DetourTransactionCommit();

        FreeLibrary(shared.dll);
    }
}

static void AttachHooks()
{
    if (o_LoadLibraryA == nullptr || o_LoadLibraryW == nullptr)
    {
        // Detour the functions
        o_FreeLibrary = reinterpret_cast<PFN_FreeLibrary>(DetourFindFunction("kernel32.dll", "FreeLibrary"));
        o_LoadLibraryA = reinterpret_cast<PFN_LoadLibraryA>(DetourFindFunction("kernel32.dll", "LoadLibraryA"));
        o_LoadLibraryW = reinterpret_cast<PFN_LoadLibraryW>(DetourFindFunction("kernel32.dll", "LoadLibraryW"));
        o_LoadLibraryExA = reinterpret_cast<PFN_LoadLibraryExA>(DetourFindFunction("kernel32.dll", "LoadLibraryExA"));
        o_LoadLibraryExW = reinterpret_cast<PFN_LoadLibraryExW>(DetourFindFunction("kernel32.dll", "LoadLibraryExW"));

        if (o_LoadLibraryA != nullptr || o_LoadLibraryW != nullptr || o_LoadLibraryExA != nullptr || o_LoadLibraryExW != nullptr)
        {
            spdlog::info("Attaching LoadLibrary hooks");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (o_FreeLibrary)
                DetourAttach(&(PVOID&)o_FreeLibrary, hkFreeLibrary);

            if (o_LoadLibraryA)
                DetourAttach(&(PVOID&)o_LoadLibraryA, hkLoadLibraryA);

            if (o_LoadLibraryW)
                DetourAttach(&(PVOID&)o_LoadLibraryW, hkLoadLibraryW);

            if (o_LoadLibraryExA)
                DetourAttach(&(PVOID&)o_LoadLibraryExA, hkLoadLibraryExA);

            if (o_LoadLibraryExW)
                DetourAttach(&(PVOID&)o_LoadLibraryExW, hkLoadLibraryExW);

            DetourTransactionCommit();
        }
    }

    if (Config::Instance()->VulkanSpoofing.value_or(false) && o_vkGetPhysicalDeviceProperties == nullptr)
    {
        o_vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(DetourFindFunction("vulkan-1.dll", "vkGetPhysicalDeviceProperties"));
        o_vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(DetourFindFunction("vulkan-1.dll", "vkGetPhysicalDeviceProperties2"));
        o_vkGetPhysicalDeviceProperties2KHR = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(DetourFindFunction("vulkan-1.dll", "vkGetPhysicalDeviceProperties2KHR"));

        if (o_vkGetPhysicalDeviceProperties != nullptr)
        {
            spdlog::info("Attaching Vulkan device spoofing hooks");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (o_vkGetPhysicalDeviceProperties)
                DetourAttach(&(PVOID&)o_vkGetPhysicalDeviceProperties, hkvkGetPhysicalDeviceProperties);

            if (o_vkGetPhysicalDeviceProperties2)
                DetourAttach(&(PVOID&)o_vkGetPhysicalDeviceProperties2, hkvkGetPhysicalDeviceProperties2);

            if (o_vkGetPhysicalDeviceProperties2KHR)
                DetourAttach(&(PVOID&)o_vkGetPhysicalDeviceProperties2KHR, hkvkGetPhysicalDeviceProperties2KHR);

            DetourTransactionCommit();
        }
    }

    if (Config::Instance()->VulkanExtensionSpoofing.value_or(false) && o_vkEnumerateInstanceExtensionProperties == nullptr)
    {
        o_vkEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(DetourFindFunction("vulkan-1.dll", "vkEnumerateInstanceExtensionProperties"));
        o_vkEnumerateDeviceExtensionProperties = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(DetourFindFunction("vulkan-1.dll", "vkEnumerateDeviceExtensionProperties"));
        o_vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(DetourFindFunction("vulkan-1.dll", "vkCreateDevice"));

        if (o_vkEnumerateInstanceExtensionProperties != nullptr || o_vkEnumerateDeviceExtensionProperties != nullptr)
        {
            spdlog::info("Attaching Vulkan extensions spoofing hooks");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (o_vkEnumerateInstanceExtensionProperties)
                DetourAttach(&(PVOID&)o_vkEnumerateInstanceExtensionProperties, hkvkEnumerateInstanceExtensionProperties);

            if (o_vkEnumerateDeviceExtensionProperties)
                DetourAttach(&(PVOID&)o_vkEnumerateDeviceExtensionProperties, hkvkEnumerateDeviceExtensionProperties);

            if (o_vkCreateDevice)
                DetourAttach(&(PVOID&)o_vkCreateDevice, hkvkCreateDevice);

            DetourTransactionCommit();
        }
    }
}

static bool IsRunningOnWine()
{
    HMODULE ntdll = GetModuleHandle(L"ntdll.dll");

    if (!ntdll)
    {
        spdlog::warn("IsRunningOnWine Not running on NT!?!");
        return true;
    }

    auto pWineGetVersion = (PFN_wine_get_version)GetProcAddress(ntdll, "wine_get_version");

    if (pWineGetVersion)
    {
        spdlog::info("IsRunningOnWine Running on Wine {0}!", pWineGetVersion());
        return true;
    }

    spdlog::warn("IsRunningOnWine Wine not detected");
    return false;
}

static void CheckWorkingMode()
{
    std::string filename = Util::DllPath().filename().string();
    std::string lCaseFilename(filename);
    wchar_t sysFolder[MAX_PATH];
    GetSystemDirectory(sysFolder, MAX_PATH);
    std::filesystem::path sysPath(sysFolder);
    std::filesystem::path pluginPath(Config::Instance()->PluginPath.value_or((Util::DllPath().parent_path() / "plugins").string()));

    for (size_t i = 0; i < lCaseFilename.size(); i++)
        lCaseFilename[i] = std::tolower(lCaseFilename[i]);

    if (lCaseFilename == "nvngx.dll" || lCaseFilename == "_nvngx.dll" || lCaseFilename == "dlss-enabler-upscaler.dll")
    {
        spdlog::info("OptiScaler working as native upscaler: {0}", filename);
        return;
    }

    HMODULE dll = nullptr;

    // version.dll
    if (lCaseFilename == "version.dll")
    {
        do
        {
            auto pluginFilePath = pluginPath / L"version.dll";
            dll = LoadLibrary(pluginFilePath.wstring().c_str());

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as version.dll, original dll loaded from plugin folder");
                break;
            }

            dll = LoadLibrary(L"version-original.dll");

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as version.dll, version-original.dll loaded");
                break;
            }

            auto sysFilePath = sysPath / L"version.dll";
            dll = LoadLibrary(sysFilePath.wstring().c_str());

            if (dll != nullptr)
                spdlog::info("OptiScaler working as version.dll, system dll loaded");

        } while (false);

        if (dll != nullptr)
        {
            dllNameA = "version.dll";
            dllNameExA = "version";
            dllNameW = L"version.dll";
            dllNameExW = L"version";

            shared.LoadOriginalLibrary(dll);
            version.LoadOriginalLibrary(dll);
        }
        else
        {
            spdlog::error("OptiScaler can't find original version.dll!");
        }
    }

    // winmm.dll
    if (lCaseFilename == "winmm.dll")
    {
        do
        {
            auto pluginFilePath = pluginPath / L"winmm.dll";
            dll = LoadLibrary(pluginFilePath.wstring().c_str());

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as winmm.dll, original dll loaded from plugin folder");
                break;
            }

            dll = LoadLibrary(L"winmm-original.dll");

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as winmm.dll, winmm-original.dll loaded");
                break;
            }

            auto sysFilePath = sysPath / L"winmm.dll";
            dll = LoadLibrary(sysFilePath.wstring().c_str());

            if (dll != nullptr)
                spdlog::info("OptiScaler working as winmm.dll, system dll loaded");

        } while (false);

        if (dll != nullptr)
        {
            dllNameA = "winmm.dll";
            dllNameExA = "winmm";
            dllNameW = L"winmm.dll";
            dllNameExW = L"winmm";

            shared.LoadOriginalLibrary(dll);
            winmm.LoadOriginalLibrary(dll);
        }
        else
        {
            spdlog::error("OptiScaler can't find original winmm.dll!");
        }
    }

    // wininet.dll
    if (lCaseFilename == "wininet.dll")
    {
        do
        {
            auto pluginFilePath = pluginPath / L"wininet.dll";
            dll = LoadLibrary(pluginFilePath.wstring().c_str());

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as wininet.dll, original dll loaded from plugin folder");
                break;
            }

            dll = LoadLibrary(L"wininet-original.dll");

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as wininet.dll, wininet-original.dll loaded");
                break;
            }

            auto sysFilePath = sysPath / L"wininet.dll";
            dll = LoadLibrary(sysFilePath.wstring().c_str());

            if (dll != nullptr)
                spdlog::info("OptiScaler working as wininet.dll, system dll loaded");

        } while (false);

        if (dll != nullptr)
        {
            dllNameA = "wininet.dll";
            dllNameExA = "wininet";
            dllNameW = L"wininet.dll";
            dllNameExW = L"wininet";

            shared.LoadOriginalLibrary(dll);
            wininet.LoadOriginalLibrary(dll);
        }
        else
        {
            spdlog::error("OptiScaler can't find original wininet.dll!");
        }
    }

    // optiscaler.dll
    if (lCaseFilename == "optiscaler.asi")
    {
        spdlog::info("OptiScaler working as OptiScaler.asi");

        // quick hack for testing
        dll = dllModule;

        dllNameA = "optiscaler.asi";
        dllNameExA = "optiscaler";
        dllNameW = L"optiscaler.asi";
        dllNameExW = L"optiscaler";
    }

    // winhttp.dll
    if (lCaseFilename == "winhttp.dll")
    {
        do
        {
            auto pluginFilePath = pluginPath / L"winhttp.dll";
            dll = LoadLibrary(pluginFilePath.wstring().c_str());

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as winhttp.dll, original dll loaded from plugin folder");
                break;
            }

            dll = LoadLibrary(L"winhttp-original.dll");

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as winhttp.dll, winhttp-original.dll loaded");
                break;
            }

            auto sysFilePath = sysPath / L"winhttp.dll";
            dll = LoadLibrary(sysFilePath.wstring().c_str());

            if (dll != nullptr)
                spdlog::info("OptiScaler working as winhttp.dll, system dll loaded");

        } while (false);

        if (dll != nullptr)
        {
            dllNameA = "winhttp.dll";
            dllNameExA = "winhttp";
            dllNameW = L"winhttp.dll";
            dllNameExW = L"winhttp";

            shared.LoadOriginalLibrary(dll);
            winhttp.LoadOriginalLibrary(dll);
        }
        else
        {
            spdlog::error("OptiScaler can't find original winhttp.dll!");
        }
    }

    // dxgi.dll
    if (lCaseFilename == "dxgi.dll")
    {
        do
        {
            auto pluginFilePath = pluginPath / L"dxgi.dll";
            dll = LoadLibrary(pluginFilePath.wstring().c_str());

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as dxgi.dll, original dll loaded from plugin folder");
                break;
            }

            dll = LoadLibrary(L"dxgi-original.dll");

            if (dll != nullptr)
            {
                spdlog::info("OptiScaler working as dxgi.dll, dxgi-original.dll loaded");
                break;
            }

            auto sysFilePath = sysPath / L"dxgi.dll";
            dll = LoadLibrary(sysFilePath.wstring().c_str());

            if (dll != nullptr)
                spdlog::info("OptiScaler working as dxgi.dll, system dll loaded");

        } while (false);

        if (dll != nullptr)
        {
            dllNameA = "dxgi.dll";
            dllNameExA = "dxgi";
            dllNameW = L"dxgi.dll";
            dllNameExW = L"dxgi";

            shared.LoadOriginalLibrary(dll);
            dxgi.LoadOriginalLibrary(dll);

            Config::Instance()->IsDxgiMode = true;
        }
        else
        {
            spdlog::error("OptiScaler can't find original dxgi.dll!");
        }
    }

    if (dll != nullptr)
    {
        AttachHooks();

        if (!Config::Instance()->DisableEarlyHooking.value_or(false) && Config::Instance()->OverlayMenu.value_or(true))
        {
            ImGuiOverlayDx12::Dx12Bind();
            ImGuiOverlayDx12::FSR3Bind();
        }

        return;
    }

    spdlog::error("Unsupported dll name: {0}", filename);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            loadCount++;

            DisableThreadLibraryCalls(hModule);

            dllModule = hModule;
            processId = GetCurrentProcessId();

#ifdef VER_PRE_RELEASE
            // Enable file logging for pre builds
            Config::Instance()->LogToFile = true;

            // Set log level to debug
            if (Config::Instance()->LogLevel.value_or(2) > 1)
                Config::Instance()->LogLevel = 1;

            //Config::Instance()->LogSingleFile = false;
#endif

            PrepareLogger();
            spdlog::warn("{0} loaded", VER_PRODUCT_NAME);
            spdlog::warn("---------------------------------");
            spdlog::warn("OptiScaler is freely downloadable from https://github.com/cdozdil/OptiScaler/releases");
            spdlog::warn("If you paid for these files, you've been scammed!");
            spdlog::warn("DO NOT USE IN MULTIPLAYER GAMES");
            spdlog::warn("");
            spdlog::warn("LogLevel: {0}", Config::Instance()->LogLevel.value_or(2));

            // Check for Linux
            Config::Instance()->IsRunningOnLinux = IsRunningOnWine();

            // Check if real DLSS available
            if (Config::Instance()->DLSSEnabled.value_or(true))
            {
                spdlog::info("");
                NVNGXProxy::InitNVNGX();

                if (NVNGXProxy::NVNGXModule() == nullptr)
                {
                    spdlog::info("Can't load nvngx.dll, disabling DLSS");
                    Config::Instance()->DLSSEnabled = false;
                }
                else
                {
                    spdlog::info("nvngx.dll loaded, setting DLSS as default upscaler and disabling spoofing.");

                    Config::Instance()->DLSSEnabled = true;

                    if (Config::Instance()->IsDxgiMode)
                    {
                        Config::Instance()->DxgiSpoofing = false;
                        Config::Instance()->DxgiSkipSpoofForUpscalers = true;
                        Config::Instance()->DxgiBlacklist.reset();
                    }

                    Config::Instance()->VulkanSpoofing = false;
                    Config::Instance()->VulkanExtensionSpoofing = false;

                    // Disable Overlay Menu because of crashes on Linux with Nvidia GPUs
                    if (Config::Instance()->IsRunningOnLinux && !Config::Instance()->OverlayMenu.has_value())
                        Config::Instance()->OverlayMenu = false;
                }
            }

            // Check for working mode and attach hooks
            spdlog::info("");
            CheckWorkingMode();
            spdlog::info("");

            break;

        case DLL_PROCESS_DETACH:
            CloseLogger();
            DetachHooks();
            break;

        default:
            break;
    }

    return TRUE;
}
