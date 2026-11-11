#!/usr/bin/env python3
"""Add the R5 view surface to Vulkan-Tools 1.4.367 cube.c, beside Metal."""

from pathlib import Path
import sys

ROOT = Path(sys.argv[1])
CUBE = ROOT / "cube.c"
FUNCS = ROOT / "cube_functions.h"
TIME = ROOT / "gettime.h"


def once(path, old, new):
    text = path.read_text(encoding="utf-8")
    if old not in text:
        if new.strip() in text:
            return
        raise SystemExit(f"anchor missing in {path.name}: {old[:80]!r}")
    if text.count(old) != 1:
        raise SystemExit(f"anchor not unique in {path.name}: {old[:80]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def main():
    once(
        FUNCS,
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
PFN_vkCreateMetalSurfaceEXT vkCreateMetalSurfaceEXT;
#endif
""",
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
PFN_vkCreateMetalSurfaceEXT vkCreateMetalSurfaceEXT;
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
#include <vulkan/vulkan_r5.h>
PFN_vkCreateViewSurfaceRFIVE vkCreateViewSurfaceRFIVE;
#endif
""",
    )
    once(
        FUNCS,
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
    LOAD_INSTANCE_FUNCTION(instance, vkCreateMetalSurfaceEXT);
#endif
""",
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
    LOAD_INSTANCE_FUNCTION(instance, vkCreateMetalSurfaceEXT);
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
    LOAD_INSTANCE_FUNCTION(instance, vkCreateViewSurfaceRFIVE);
#endif
""",
    )
    once(
        CUBE,
        """    WSI_PLATFORM_DISPLAY,
    WSI_PLATFORM_INVALID,  // Sentinel just to indicate invalid user input
""",
        """    WSI_PLATFORM_DISPLAY,
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
    WSI_PLATFORM_VIEW_RFIVE,
#endif
    WSI_PLATFORM_INVALID,  // Sentinel just to indicate invalid user input
""",
    )
    once(
        CUBE,
        """#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    if (strcmp(str, "display") == 0) return WSI_PLATFORM_DISPLAY;
#endif
    return WSI_PLATFORM_INVALID;
""",
        """#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    if (strcmp(str, "display") == 0) return WSI_PLATFORM_DISPLAY;
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
    if (strcmp(str, "r5") == 0) return WSI_PLATFORM_VIEW_RFIVE;
#endif
    return WSI_PLATFORM_INVALID;
""",
    )
    once(
        CUBE,
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
        case (WSI_PLATFORM_METAL):
            return "metal";
#endif
""",
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
        case (WSI_PLATFORM_METAL):
            return "metal";
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
        case (WSI_PLATFORM_VIEW_RFIVE):
            return "r5";
#endif
""",
    )
    once(
        CUBE,
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
    void *caMetalLayer;
#endif
""",
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
    void *caMetalLayer;
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
    void *r5_view;
#endif
""",
    )
    once(
        CUBE,
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
static void demo_run(struct demo *demo) {
    if (!demo->initialized || !demo->swapchain_ready) return;

    demo_draw(demo);
    if (!demo->is_minimized) {
        demo->curFrame++;
    }
    if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount) {
        demo->quit = TRUE;
    }
}
#endif
""",
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
static void demo_run(struct demo *demo) {
    if (!demo->initialized || !demo->swapchain_ready) return;

    demo_draw(demo);
    if (!demo->is_minimized) {
        demo->curFrame++;
    }
    if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount) {
        demo->quit = TRUE;
    }
}
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
static void demo_run(struct demo *demo) {
    if (!demo->initialized || !demo->swapchain_ready) return;

    demo_draw(demo);
    if (!demo->is_minimized) {
        demo->curFrame++;
    }
    if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount) {
        demo->quit = true;
    }
}
#endif
""",
    )
    once(
        CUBE,
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
    if (demo->wsi_platform == WSI_PLATFORM_METAL || demo->wsi_platform == WSI_PLATFORM_AUTO) {
        bool metal_extension_available = false;
        for (uint32_t i = 0; i < demo->enabled_extension_count; i++) {
            if (strcmp(demo->extension_names[i], VK_EXT_METAL_SURFACE_EXTENSION_NAME) == 0) {
                metal_extension_available = true;
                break;
            }
        }
        if (metal_extension_available) {
            demo->wsi_platform = WSI_PLATFORM_METAL;
            return;
        }
    }
#endif
""",
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
    if (demo->wsi_platform == WSI_PLATFORM_METAL || demo->wsi_platform == WSI_PLATFORM_AUTO) {
        bool metal_extension_available = false;
        for (uint32_t i = 0; i < demo->enabled_extension_count; i++) {
            if (strcmp(demo->extension_names[i], VK_EXT_METAL_SURFACE_EXTENSION_NAME) == 0) {
                metal_extension_available = true;
                break;
            }
        }
        if (metal_extension_available) {
            demo->wsi_platform = WSI_PLATFORM_METAL;
            return;
        }
    }
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
    if (demo->wsi_platform == WSI_PLATFORM_VIEW_RFIVE || demo->wsi_platform == WSI_PLATFORM_AUTO) {
        bool r5_extension_available = false;
        for (uint32_t i = 0; i < demo->enabled_extension_count; i++) {
            if (strcmp(demo->extension_names[i], VK_RFIVE_VIEW_SURFACE_EXTENSION_NAME) == 0) {
                r5_extension_available = true;
                break;
            }
        }
        if (r5_extension_available) {
            demo->wsi_platform = WSI_PLATFORM_VIEW_RFIVE;
            return;
        }
    }
#endif
""",
    )
    once(
        CUBE,
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
            if (!strcmp(VK_EXT_METAL_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName) &&
                (demo->wsi_platform == WSI_PLATFORM_AUTO || demo->wsi_platform == WSI_PLATFORM_METAL)) {
                platformSurfaceExtFound = true;
                demo->extension_names[demo->enabled_extension_count++] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
            }
#endif
""",
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
            if (!strcmp(VK_EXT_METAL_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName) &&
                (demo->wsi_platform == WSI_PLATFORM_AUTO || demo->wsi_platform == WSI_PLATFORM_METAL)) {
                platformSurfaceExtFound = true;
                demo->extension_names[demo->enabled_extension_count++] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
            }
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
            if (!strcmp(VK_RFIVE_VIEW_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName) &&
                (demo->wsi_platform == WSI_PLATFORM_AUTO || demo->wsi_platform == WSI_PLATFORM_VIEW_RFIVE)) {
                platformSurfaceExtFound = true;
                demo->extension_names[demo->enabled_extension_count++] = VK_RFIVE_VIEW_SURFACE_EXTENSION_NAME;
            }
#endif
""",
    )
    once(
        CUBE,
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
            case (WSI_PLATFORM_METAL):
                ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_EXT_METAL_SURFACE_EXTENSION_NAME
                         " instance extension.\\n\\n"
                         "The selected WSI platform metal is not available, please choose a different WSI platform\\n",
                         "vkCreateInstance Failure");
                break;
#endif
""",
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
            case (WSI_PLATFORM_METAL):
                ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_EXT_METAL_SURFACE_EXTENSION_NAME
                         " instance extension.\\n\\n"
                         "The selected WSI platform metal is not available, please choose a different WSI platform\\n",
                         "vkCreateInstance Failure");
                break;
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
            case (WSI_PLATFORM_VIEW_RFIVE):
                ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_RFIVE_VIEW_SURFACE_EXTENSION_NAME
                         " instance extension.\\n\\n"
                         "The selected WSI platform r5 is not available, please choose a different WSI platform\\n",
                         "vkCreateInstance Failure");
                break;
#endif
""",
    )
    once(
        CUBE,
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
    if (demo->wsi_platform == WSI_PLATFORM_METAL) {
        VkMetalSurfaceCreateInfoEXT metal_createInfo;
        metal_createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        metal_createInfo.pNext = NULL;
        metal_createInfo.flags = 0;
        metal_createInfo.pLayer = demo->caMetalLayer;

        err = vkCreateMetalSurfaceEXT(demo->inst, &metal_createInfo, NULL, &demo->surface);
    }
#endif
""",
        """#if defined(VK_USE_PLATFORM_METAL_EXT)
    if (demo->wsi_platform == WSI_PLATFORM_METAL) {
        VkMetalSurfaceCreateInfoEXT metal_createInfo;
        metal_createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        metal_createInfo.pNext = NULL;
        metal_createInfo.flags = 0;
        metal_createInfo.pLayer = demo->caMetalLayer;

        err = vkCreateMetalSurfaceEXT(demo->inst, &metal_createInfo, NULL, &demo->surface);
    }
#endif
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
    if (demo->wsi_platform == WSI_PLATFORM_VIEW_RFIVE) {
        VkViewSurfaceCreateInfoRFIVE r5_createInfo;
        r5_createInfo.sType = VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE;
        r5_createInfo.pNext = NULL;
        r5_createInfo.flags = 0;
        r5_createInfo.view = (BView *)demo->r5_view;

        err = vkCreateViewSurfaceRFIVE(demo->inst, &r5_createInfo, NULL, &demo->surface);
    }
#endif
""",
    )
    once(
        CUBE,
        "#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__QNX__) || defined(__GNU__)\n",
        "#elif (defined(__linux__) && !defined(__HAIKU__)) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__QNX__) || defined(__GNU__)\n",
    )
    once(
        TIME,
        "#elif defined(__unix__) || defined(__linux) || defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__ANDROID__) || defined(__EPOC32__) || defined(__QNX__) || defined(__APPLE__)\n",
        "#elif defined(__unix__) || defined(__linux) || defined(__linux__) || defined(__HAIKU__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__ANDROID__) || defined(__EPOC32__) || defined(__QNX__) || defined(__APPLE__)\n",
    )
    once(
        TIME,
        "#elif defined(__unix__) || defined(__linux) || defined(__linux__) || defined(__ANDROID__) || defined(__QNX__) || defined(__APPLE__)\n",
        "#elif defined(__unix__) || defined(__linux) || defined(__linux__) || defined(__HAIKU__) || defined(__ANDROID__) || defined(__QNX__) || defined(__APPLE__)\n",
    )
    once(
        CUBE,
        """        int length = snprintf(NULL, 0, message, APP_SHORT_NAME, available_wsi_platforms, VK_PRESENT_MODE_IMMEDIATE_KHR,
                              VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR);
        char *usage = (char *)malloc(length + 1);
        if (!usage) {
            exit(1);
        }
        snprintf(usage, length + 1, message, APP_SHORT_NAME, available_wsi_platforms, VK_PRESENT_MODE_IMMEDIATE_KHR,
                 VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR);
""",
        """        char usage[2048];
        snprintf(usage, sizeof(usage), message, APP_SHORT_NAME, available_wsi_platforms, VK_PRESENT_MODE_IMMEDIATE_KHR,
                 VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR);
""",
    )
    once(
        CUBE,
        """        free(usage);
        exit(1);
""",
        """        exit(1);
""",
    )
    once(
        CUBE,
        """    if (!demo->initialized) {
        return;
    }

    // Don't do anything if the surface has zero size""",
        """    if (!demo->initialized) {
        return;
    }
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
    /* Quitting, or a view of size 0, does not build another swapchain. */
    if (demo->quit)
        return;
    {
        VkSurfaceCapabilitiesKHR r5Caps;
        VkResult r5Cap = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            demo->gpu, demo->surface, &r5Caps);
        if (r5Cap != VK_SUCCESS || r5Caps.currentExtent.width == 0
            || r5Caps.currentExtent.height == 0)
            return;
    }
#endif

    // Don't do anything if the surface has zero size""",
    )
    text = CUBE.read_text(encoding="utf-8")
    if "demo_r5_create" not in text:
        text += r"""
#if defined(VK_USE_PLATFORM_VIEW_RFIVE)
struct demo *demo_r5_create(void) {
    return (struct demo *)calloc(1, sizeof(struct demo));
}

void demo_r5_start(struct demo *demo, void *view, int argc, char **argv) {
    demo_init(demo, argc, argv);
    demo->r5_view = view;
    demo_create_surface(demo);
    demo_select_physical_device(demo);
    demo_init_vk_swapchain(demo);
    demo_prepare(demo);
}

int demo_r5_step(struct demo *demo) {
    if (demo->quit)
        return 1;
    demo_run(demo);
    return demo->quit ? 1 : 0;
}

void demo_r5_request_quit(struct demo *demo) {
    demo->quit = true;
}

void demo_r5_cleanup(struct demo *demo) {
    if (demo->initialized)
        demo_cleanup(demo);
}

void demo_r5_free(struct demo *demo) {
    free(demo);
}
#endif
"""
        CUBE.write_text(text, encoding="utf-8", newline="\n")
    print("patched", ROOT)


if __name__ == "__main__":
    main()
