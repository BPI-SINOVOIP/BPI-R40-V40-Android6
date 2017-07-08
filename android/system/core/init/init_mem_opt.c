/*
 * memory optimization code
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <errno.h>
#include <stdarg.h>
#include <mtd/mtd-user.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <selinux/selinux.h>
#include <selinux/label.h>
#include <selinux/android.h>

#include <libgen.h>

#include <cutils/list.h>
#include <cutils/android_reboot.h>
#include <cutils/sockets.h>
#include <cutils/iosched_policy.h>
#include <cutils/fs.h>
#include <private/android_filesystem_config.h>
#include <termios.h>

#include "devices.h"
#include "init.h"
#include "log.h"
#include "property_service.h"
#include "bootchart.h"
#include "signal_handler.h"
#include "keychords.h"
#include "init_parser.h"
#include "util.h"
#include "ueventd.h"
#include "watchdogd.h"

#define DENSITY_LOW     120
#define DENSITY_MEDIUM  160
#define DENSITY_TV      213
#define DENSITY_HIGH    240
#define DENSITY_280     280
#define DENSITY_XHIGH   320
#define DENSITY_360     360
#define DENSITY_400     400
#define DENSITY_420     420
#define DENSITY_XXHIGH  480
#define DENSITY_560     560
#define DENSITY_XXXHIGH 640

#define SCREEN_SMALL    0
#define SCREEN_NORMAL   1
#define SCREEN_LARGE    2
#define SCREEN_XLARGE   3

int g_total_mem = 0; /* unit: MB */

static const char* s_ScreenSizeConfig[] = {
"SCREEN_SMALL",
"SCREEN_NORMAL",
"SCREEN_LARGE",
"SCREEN_XLARGE"
};

static int get_dram_size(void)
{
#define MEMINFO_NODE    "/proc/meminfo"
    FILE *fd;
    char data[128], *tmp;
    int dram_size = 1024;

    fd = fopen(MEMINFO_NODE, "r");
    if (fd == NULL) {
        ERROR("cannot open %s, return default 1G\n", MEMINFO_NODE);
        goto end;
    }

    while (fgets(data, sizeof(data), fd)) {
        if (strstr(data, "MemTotal")) {
            tmp = strchr(data, ':') + 1;
            dram_size = atoi(tmp) >> 10; /* convert to MBytes */
            break;
        }
    }

    fclose(fd);
end:
    INFO("%s: return %d\n", __func__, dram_size);
    return dram_size;
}

static bool get_lcd_resolution(int *width, int *height)
{
   char buf[PROP_VALUE_MAX] = {0};
   if (property_get("ro.boot.lcd_x", buf)) {
         *width = atoi(buf);
        if (*width <= 0) {
            ERROR("%s: ro.boot.lcd_x wrong value: %d(convert from %s) set, \
                    disable adaptive memory function!\n", __func__, *width, buf);
            return false;
        }
    } else {
        ERROR("%s: ro.boot.lcd_x not set, disable adaptive memory function!\n", __func__);
        return false;
    }

    if (property_get("ro.boot.lcd_y", buf)) {
         *height = atoi(buf);
        if (*height <= 0) {
            ERROR("%s: ro.boot.lcd_y wrong value: %d(convert from %s) set, \
                    disable adaptive memory function!\n", __func__, *height, buf);
            return false;
        }
    } else {
        ERROR("%s: ro.boot.lcd_y not set, disable adaptive memory function!\n", __func__);
        return false;
    }
    return true;
}

inline void trim(char *buf)
{
    char *temp;
    int i = 0;

    if (!buf || *buf == 0)
        return;

    /* trim tail */
    while ((temp = buf + strlen(buf) - 1) && *temp != 0) {
        if (*temp==' ' || *temp=='\t'
                || *temp=='\n' || *temp=='\r')
            *temp = 0;
        else
            break;
    }

    if (*buf == 0)
        return;

    /* trim head */
    while (i < (int)strlen(buf)) {
        if (buf[i]==' ' || buf[i]=='\t'
                || buf[i]=='\n' || buf[i]=='\r') {
            i++;
            continue;
        } else if (buf[i] != 0) {
            strcpy(buf, &buf[i]);
            break;
        } else {
            buf[0] = 0;
            break;
        }
    }
}

#define CONFIG_MEM_FILE     "/config_mem.ini"

void config_item(char *buf)
{
    char data[1024], key[256], value[256];
    bool find = false;
    FILE *fd;
    int len;

    fd = fopen(CONFIG_MEM_FILE, "r");
    if (fd == NULL) {
        ERROR("cannot open %s\n", CONFIG_MEM_FILE);
        return;
    }

    while (!feof(fd)) {
        if (!fgets(data, sizeof(data), fd)) /* eof or read error */
            continue;

        if (strlen(data) >= sizeof(data) - 1) {
            ERROR("%s err: line too long!\n", __func__);
            goto end;
        }

        trim(data);

        if (data[0]=='#' || data[0]==0) /* comment or blank line */
            continue;

        if (!find) {
            if (data[0]=='[' && strstr(data, buf)) {
                find = true;
                continue;
            }
        } else {
            if (data[0]=='[')
                break; /* NEXT item, so break */
            else if (!strstr(data, "=") || data[strlen(data)-1] == '=')
                continue; /* not key=value style, or has no value field */

            len = strlen(data) - strlen(strstr(data, "="));
            strncpy(key, data, len);
            key[len] = '\0';
            trim(key);

            strcpy(value, strstr(data, "=") + 1);
            trim(value);

            INFO("%s: get key->value %s %s\n", __func__, key, value);
            if (key[0] == '/')  { /* file node, as: /sys/class/adj=12 */
                sprintf(data, "echo %s > %s", value, key);
                system(data);
            } else /* property node, as: dalvik.vm.heapsize=184m */
                property_set(key, value);
        }
    }

end:
    fclose(fd);
}

bool get_value_for_key(char *main_key, char *sub_key, char ret_value[], int len)
{
    char data[1024], tmp[256];
    bool find_mainkey = false, ret = false;
    FILE *fd = NULL;

    fd = fopen(CONFIG_MEM_FILE, "r");
    if (fd == NULL) {
        ERROR("cannot open %s\n", CONFIG_MEM_FILE);
        return false;
    }

    while (!feof(fd)) {
        if (!fgets(data, sizeof(data), fd)) /* eof or read error */
            continue;

        if (strlen(data) >= sizeof(data) - 1) {
            ERROR("%s err: line too long!\n", __func__);
            goto end;
        }

        trim(data);

        if (data[0]=='#' || data[0]==0) /* comment or blank line */
            continue;

        if (!find_mainkey) {
            if (data[0]=='[' && !strncmp(data+1, main_key,
                strlen(main_key))) { /* +1 means omit '[' */
                find_mainkey = true;
                continue;
            }
        } else {
            if (data[0]=='[')
                goto end; /* NEXT item, so break */
            else if (!strstr(data, "=") || data[strlen(data)-1] == '=')
                continue; /* not 'key = value' style, or has no value field */

            len = strlen(data) - strlen(strstr(data, "="));
            strncpy(tmp, data, len);
            tmp[len] = '\0';
            trim(tmp);

            if (strcmp(tmp, sub_key))
                continue; /* not subkey */

            strcpy(tmp, strstr(data, "=") + 1);
            trim(tmp);
            if ((int)strlen(tmp) >= len) {
                ERROR("%s err: %s->%s value too long!\n", __func__, main_key, sub_key);
                goto end;
            }

            NOTICE("%s: get %s->%s: %s\n", __func__, main_key, sub_key, tmp);
            strcpy(ret_value, tmp);
            ret = true;
            break;
        }
    }

end:
    fclose(fd);
    return ret;
}

int getScreenSize(int width, int height) {
    int max = width >= height ? width : height;
    int min = width <= height ? width : height;

    if ((min >= 720) && (max > 960)) {
        return SCREEN_XLARGE;
    } else if ((min >= 480) && (max > 640)) {
        return SCREEN_LARGE;
    } else if ((min >= 320) && (max > 480)) {
        return SCREEN_NORMAL;
    } else {
        return SCREEN_SMALL;
    }
}

int getDensityFromString2Int(char *sDensity) {
    if ((NULL == sDensity) || 0 == *sDensity) {
        return 0;
    }

    if (!strcmp(sDensity, "DENSITY_LOW")) {
        return DENSITY_LOW;
    } else if (!strcmp(sDensity, "DENSITY_MEDIUM")) {
        return DENSITY_MEDIUM;
    } else if (!strcmp(sDensity, "DENSITY_TV")) {
        return DENSITY_TV;
    } else if (!strcmp(sDensity, "DENSITY_HIGH")) {
        return DENSITY_HIGH;
    } else if (!strcmp(sDensity, "DENSITY_280")) {
        return DENSITY_280;
    } else if (!strcmp(sDensity, "DENSITY_XHIGH")) {
        return DENSITY_XHIGH;
    } else if (!strcmp(sDensity, "DENSITY_360")) {
        return DENSITY_360;
    } else if (!strcmp(sDensity, "DENSITY_400")) {
        return DENSITY_400;
    } else if (!strcmp(sDensity, "DENSITY_420")) {
        return DENSITY_420;
    } else if (!strcmp(sDensity, "DENSITY_XXHIGH")) {
        return DENSITY_XXHIGH;
    } else if (!strcmp(sDensity, "DENSITY_560")) {
        return DENSITY_560;
    } else if (!strcmp(sDensity, "DENSITY_XXXHIGH")) {
        return DENSITY_XXXHIGH;
    } else {
        return 0;
    }
}

int getMinimumMemory(int screen_size, int density, bool is64Bit) {
    char data[1024];
    bool find = false;
    FILE *fd;
    char *sScreenSizeRead    = NULL;
    char *sDensityRead = NULL;
    char *sCmpRead     = NULL;
    char *sMemRead     = NULL;
    int iDensityRead = 0;
    int iMemRead      = 0;

    fd = fopen(CONFIG_MEM_FILE, "r");
    if (fd == NULL) {
        ERROR("cannot open %s\n", CONFIG_MEM_FILE);
        return 0;
    }

    while (!feof(fd)) {
        if (!fgets(data, sizeof(data), fd)) /* eof or read error */
            continue;

        if (strlen(data) >= sizeof(data) - 1) {
            ERROR("%s err: line too long!\n", __func__);
            goto end;
        }

        trim(data);

        if (data[0]=='#' || data[0]==0) /* comment or blank line */
            continue;

        if (!find) {
            if (data[0]=='[' && strstr(data, "least_memory")) {
                find = true;
                continue;
            }
        } else {
            if (data[0]=='[')
                break; /* NEXT item, so break */

            sScreenSizeRead = strtok(data , " ");
            if ((NULL == sScreenSizeRead) || (0 == *sScreenSizeRead) || \
                (strcmp(sScreenSizeRead, s_ScreenSizeConfig[screen_size]))) {
                continue;
            }

            sDensityRead = strtok(NULL , " ");
            if ((NULL == sDensityRead) || (0 == *sDensityRead)) {
                continue;
            }
            iDensityRead = getDensityFromString2Int(sDensityRead);
            if (iDensityRead <= 0) {
                continue;
            }

            sCmpRead = strtok(NULL , " ");
            if ((NULL == sCmpRead) || (0 == *sCmpRead)) {
                continue;
            }
            if ((!strcmp(sCmpRead, "<=") && (density > iDensityRead)) || \
                (!strcmp(sCmpRead, ">=") && (density < iDensityRead))) {
                continue;
            }

            sMemRead = strtok(NULL , " ");
            if ((NULL == sMemRead) || (0 == *sMemRead)) {
                break;
            }
            if (!is64Bit) {
                iMemRead = atoi(sMemRead);
                break;
            } 

            sMemRead = strtok(NULL , " ");
            if ((NULL == sMemRead) || (0 == *sMemRead)) {
                break;
            }
            iMemRead = atoi(sMemRead);
            break;
        }
    }

end:
    fclose(fd);
    return iMemRead;
}

void config_low_ram_property(int screen_size, int density, bool is64Bit, int totalmem) {
    int minMem = getMinimumMemory(screen_size, density, is64Bit);
    /*if total memory is less than 1.5 * least memory requested by cdd,
       then it's a low memory device */
    int lowMemoryThreathHold = minMem + (minMem >> 1);

    if (totalmem <= lowMemoryThreathHold) {
        property_set("ro.config.low_ram", "true");
    } else {
        property_set("ro.config.low_ram", "false");
    }

	if (totalmem <= 512) {
        property_set("ro.config.512m", "true");
	}
}

void config_heap_growth_limit_property(int screen_size, int density) {
    char data[1024];
    bool find = false;
    FILE *fd;
    char *sDensityRead = NULL;
    char *sMemRead     = NULL;
    int iDensityRead   = 0;

    fd = fopen(CONFIG_MEM_FILE, "r");
    if (fd == NULL) {
        ERROR("cannot open %s\n", CONFIG_MEM_FILE);
        return;
    }

    while (!feof(fd)) {
        if (!fgets(data, sizeof(data), fd)) /* eof or read error */
            continue;

        if (strlen(data) >= sizeof(data) - 1) {
            ERROR("%s err: line too long!\n", __func__);
            goto end;
        }

        trim(data);

        if (data[0]=='#' || data[0]==0) /* comment or blank line */
            continue;

        if (!find) {
            if (data[0]=='[' && strstr(data, "heap_growth_limit")) {
                find = true;
                continue;
            }
        } else {
            if (data[0]=='[')
                break; /* NEXT item, so break */

            sDensityRead = strtok(data , " ");
            if ((NULL == sDensityRead) || (0 == *sDensityRead)) {
                continue;
            }
            iDensityRead = getDensityFromString2Int(sDensityRead);
            if ((iDensityRead <= 0) || (density > iDensityRead)) {
                continue;
            }

            switch (screen_size) {
                case SCREEN_XLARGE:
                    sMemRead = strtok(NULL , " ");
                case SCREEN_LARGE:
                    sMemRead = strtok(NULL , " ");
                case SCREEN_NORMAL:
                    sMemRead = strtok(NULL , " ");
                case SCREEN_SMALL:
                    sMemRead = strtok(NULL , " ");
                    if ((NULL != sMemRead) || (0 != *sMemRead)) {
                        property_set("dalvik.vm.heapgrowthlimit", sMemRead);
                    }
                    break;
                default :
                    break;
            }

            break;

        }
    }

end:
    fclose(fd);
    return;
}

void property_opt_for_mem(void)
{
    char buf[PROP_VALUE_MAX] = {0};
    static int width = 0, height = 0;
    int densityDPI = 0, width_dp = 0, height_dp=0, screenSize = 0;
    bool bSupport64Bit = false;

    ERROR("%s: start!\n", __func__);
    if(property_get("ro.memopt.disable", buf) && !strcmp(buf,"true")) {
        ERROR("%s: disable adaptive memory function!\n", __func__);
        return;
    }

    if (!g_total_mem)
        g_total_mem = get_dram_size();

    if (property_get("ro.sf.lcd_density", buf)) {
        densityDPI = atoi(buf);
        if (densityDPI <= 0) {
            ERROR("%s: ro.sf.lcd_density wrong value: %d(convert from %s) set, \
                    disable adaptive memory function!\n", __func__, densityDPI, buf);
            return;
        }
    } else {
        ERROR("%s: ro.sf.lcd_density not set, disable adaptive memory function!\n", __func__);
        return;
    }

    if (property_get("ro.product.cpu.abilist64", buf)) {
        bSupport64Bit = true;
    } else  {
        bSupport64Bit = false;
    }

    /* dalvik heap para */
    if (g_total_mem <= 512)
        strcpy(buf, "dalvik_512m");
    else if (g_total_mem > 512 && g_total_mem <= 1024)
        strcpy(buf, "dalvik_1024m");
    else if (g_total_mem > 1024 && g_total_mem <= 2048)
        strcpy(buf, "dalvik_2048m");
    else
        strcpy(buf, "dalvik_1024m");
    config_item(buf);
    /* hwui para */
    if (!width || !height) {
        if (!get_lcd_resolution(&width, &height)) {
            ERROR("%s: get lcd resolution failed!\n", __func__);
            return;
        }
    }

    sprintf(buf, "hwui_%d", (width > height ? width : height));
    config_item(buf);

    width_dp = width * DENSITY_MEDIUM / densityDPI;
    height_dp = height * DENSITY_MEDIUM / densityDPI;

    screenSize = getScreenSize(width_dp, height_dp);

    ERROR("%s: width_dp = %d; height_dp = %d; screenSize = %d; bSupport64Bit = %d, g_total_mem = %d \n",\
            __func__, width_dp, height_dp, screenSize, bSupport64Bit, g_total_mem);

    config_low_ram_property(screenSize, densityDPI, bSupport64Bit, g_total_mem);

    if (!property_get("dalvik.vm.heapgrowthlimit", buf)) {
        config_heap_growth_limit_property(screenSize, densityDPI);
    }

    system("echo 12000 > /sys/module/lowmemorykiller/parameters/minfree");
    NOTICE("%s: end!\n", __func__);
}

