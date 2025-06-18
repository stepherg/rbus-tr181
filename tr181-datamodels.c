#include <rbus.h>
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#else
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#endif

#define MAX_NAME_LEN 256
#define JSON_FILE "datamodels.json"

typedef enum {
   TYPE_STRING = 0,
   TYPE_INT = 1,
   TYPE_UINT = 2,
   TYPE_BOOL = 3,
   TYPE_DATETIME = 4,
   TYPE_BASE64 = 5,
   TYPE_LONG = 6,
   TYPE_ULONG = 7,
   TYPE_FLOAT = 8,
   TYPE_DOUBLE = 9,
   TYPE_BYTE = 10
} ValueType;

typedef struct {
   char name[MAX_NAME_LEN];
   ValueType type;
   union {
      char *strVal;          // TYPE_STRING, TYPE_DATETIME, TYPE_BASE64
      int32_t intVal;        // TYPE_INT
      uint32_t uintVal;      // TYPE_UINT
      bool boolVal;          // TYPE_BOOL
      int64_t longVal;       // TYPE_LONG
      uint64_t ulongVal;     // TYPE_ULONG
      float floatVal;        // TYPE_FLOAT
      double doubleVal;      // TYPE_DOUBLE
      uint8_t byteVal;       // TYPE_BYTE
   } value;
   rbusGetHandler_t getHandler;
   rbusSetHandler_t setHandler;
} DataModel;

DataModel *dataModels = NULL;
int numDataModels = 0;
int numgDataModels = 0;
int totalDataModels = 0;

static rbusError_t get_system_serial_number(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options) {

   rbusValue_t value;
   rbusValue_Init(&value);

#ifdef __APPLE__
   // macOS implementation
   io_service_t platformExpert = IOServiceGetMatchingService(kIOMainPortDefault,
      IOServiceMatching("IOPlatformExpertDevice"));

   if (!platformExpert) {
      return RBUS_ERROR_BUS_ERROR;
   }

   CFStringRef serialNumber = IORegistryEntryCreateCFProperty(platformExpert,
      CFSTR(kIOPlatformSerialNumberKey),
      kCFAllocatorDefault,
      0);

   IOObjectRelease(platformExpert);

   if (!serialNumber) {
      return RBUS_ERROR_BUS_ERROR;
   }

   // Get the length of the serial number string
   CFIndex length = CFStringGetLength(serialNumber);
   CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;

   // Allocate memory for the C string
   char *serial = (char *)malloc(maxSize);
   if (!serial) {
      CFRelease(serialNumber);
      return RBUS_ERROR_BUS_ERROR;
   }

   // Convert CFString to C string
   if (!CFStringGetCString(serialNumber, serial, maxSize, kCFStringEncodingUTF8)) {
      free(serial);
      CFRelease(serialNumber);
      return RBUS_ERROR_BUS_ERROR;
   }

   CFRelease(serialNumber);
   rbusValue_SetString(value, serial);

#else
   // Linux implementation
   FILE *fp = fopen("/sys/class/dmi/id/product_serial", "r");
   if (!fp) {
      return RBUS_ERROR_BUS_ERROR;
   }

   char *serial = NULL;
   size_t len = 0;
   ssize_t read;

   // Read the serial number (typically one line)
   if ((read = getline(&serial, &len, fp)) != -1) {
      // Remove trailing newline if present
      if (serial[read - 1] == '\n') {
         serial[read - 1] = '\0';
      }
      // Return a copy of the string to ensure proper memory management
      char *result = strdup(serial);
      free(serial);
      fclose(fp);
      return RBUS_ERROR_BUS_ERROR;
   }

   rbusValue_SetString(value, serial);

   free(serial);
   fclose(fp);
#endif

   rbusProperty_SetValue(property, value);
   rbusValue_Release(value);

   return RBUS_ERROR_SUCCESS;
}

static rbusError_t get_system_time(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options) {
   rbusValue_t value;
   rbusValue_Init(&value);

   // Get current time with microsecond precision
   struct timeval tv;
   if (gettimeofday(&tv, NULL) != 0) {
      return RBUS_ERROR_BUS_ERROR;
   }

   // Format as seconds.microseconds (e.g., 1707414628.363438553)
   char time_str[32]; // Buffer for timestamp (ample for seconds + microseconds + null)
   snprintf(time_str, sizeof(time_str), "%ld.%06ld", (long)tv.tv_sec, (long)tv.tv_usec);

   // Set the rbus value to the formatted time string
   rbusValue_SetString(value, time_str);

   rbusProperty_SetValue(property, value);
   rbusValue_Release(value);

   return RBUS_ERROR_SUCCESS;
}

static rbusError_t get_system_uptime(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options) {
   rbusValue_t value;
   rbusValue_Init(&value);


#ifdef __APPLE__
   // macOS implementation: Get boot time via sysctl and calculate uptime
   int mib[2] = {CTL_KERN, KERN_BOOTTIME};
   struct timeval boottime;
   size_t size = sizeof(boottime);

   if (sysctl(mib, 2, &boottime, &size, NULL, 0) == -1) {
      return RBUS_ERROR_BUS_ERROR;
   }

   struct timeval now;
   if (gettimeofday(&now, NULL) != 0) {
      return RBUS_ERROR_BUS_ERROR;
   }

   uint32_t uptime_seconds = (now.tv_sec - boottime.tv_sec);

   char uptime_str[32];
   snprintf(uptime_str, sizeof(uptime_str), "%u", uptime_seconds);

#else
   // Linux implementation: Read /proc/uptime
   FILE *fp = fopen("/proc/uptime", "r");
   if (!fp) {
      return RBUS_ERROR_BUS_ERROR;
   }

   char uptime_str[32];
   uint32_t uptime_seconds;

   // Read the first value from /proc/uptime (seconds since boot)
   if (fscanf(fp, "%lf", &uptime_seconds) != 1) {
      fclose(fp);
      return RBUS_ERROR_BUS_ERROR;
   }

   fclose(fp);

   snprintf(uptime_str, sizeof(uptime_str), "%u", uptime_seconds);
#endif

   // Set the rbus value to the formatted uptime string
   rbusValue_SetString(value, uptime_str);

   rbusProperty_SetValue(property, value);
   rbusValue_Release(value);

   return RBUS_ERROR_SUCCESS;
}

static rbusError_t get_mac_address(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options) {
   rbusValue_t value;
   rbusValue_Init(&value);

#ifdef __APPLE__
   // macOS implementation: Use IOKit to find the first non-loopback interface
   io_iterator_t iterator;
   kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault,
      IOServiceMatching("IOEthernetInterface"),
      &iterator);
   if (kr != KERN_SUCCESS) {
      return RBUS_ERROR_BUS_ERROR;
   }

   char mac_str[18] = {0}; // Buffer for MAC address (xx:xx:xx:xx:xx:xx + null)
   io_service_t service;
   bool found = false;

   while ((service = IOIteratorNext(iterator)) != 0) {
      // Check if this is a non-loopback interface
      CFStringRef bsdName = IORegistryEntryCreateCFProperty(service,
         CFSTR("BSD Name"),
         kCFAllocatorDefault,
         0);
      if (bsdName) {
         char bsd_name[32];
         if (CFStringGetCString(bsdName, bsd_name, sizeof(bsd_name), kCFStringEncodingUTF8)) {
            // Skip loopback (e.g., "lo0")
            if (strncmp(bsd_name, "lo", 2) != 0) {
               // Get MAC address from parent controller
               io_service_t parent;
               kr = IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent);
               if (kr == KERN_SUCCESS) {
                  CFDataRef macData = IORegistryEntryCreateCFProperty(parent,
                     CFSTR("IOMACAddress"),
                     kCFAllocatorDefault,
                     0);
                  if (macData) {
                     const UInt8 *bytes = CFDataGetBytePtr(macData);
                     snprintf(mac_str, sizeof(mac_str),
                        "%02x:%02x:%02x:%02x:%02x:%02x",
                        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
                     CFRelease(macData);
                     found = true;
                  }
                  IOObjectRelease(parent);
               }
            }
         }
         CFRelease(bsdName);
      }
      IOObjectRelease(service);
      if (found) break;
   }
   IOObjectRelease(iterator);

   if (!found) {
      return RBUS_ERROR_BUS_ERROR;
   }

#else
   // Linux implementation: Use ioctl to get the first non-loopback interface MAC
   int sock = socket(AF_INET, SOCK_DGRAM, 0);
   if (sock < 0) {
      return RBUS_ERROR_BUS_ERROR;
   }

   struct ifreq ifr;
   struct ifconf ifc;
   char buf[1024];
   char mac_str[18] = {0}; // Buffer for MAC address (xx:xx:xx:xx:xx:xx + null)
   bool found = false;

   ifc.ifc_len = sizeof(buf);
   ifc.ifc_buf = buf;
   if (ioctl(sock, SIOCGIFCONF, &ifc) < 0) {
      close(sock);
      return RBUS_ERROR_BUS_ERROR;
   }

   struct ifreq *it = ifc.ifc_req;
   const struct ifreq *const end = it + (ifc.ifc_len / sizeof(struct ifreq));

   for (; it != end; ++it) {
      strcpy(ifr.ifr_name, it->ifr_name);
      if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
         // Skip loopback interfaces
         if (!(ifr.ifr_flags & IFF_LOOPBACK)) {
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
               unsigned char *mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;
               snprintf(mac_str, sizeof(mac_str),
                  "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
               found = true;
               break;
            }
         }
      }
   }

   close(sock);

   if (!found) {
      return RBUS_ERROR_BUS_ERROR;
   }
#endif

   // Set the rbus value to the formatted MAC address string
   rbusValue_SetString(value, mac_str);

   rbusProperty_SetValue(property, value);
   rbusValue_Release(value);

   return RBUS_ERROR_SUCCESS;
}

static rbusError_t get_memory_free(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options) {
   rbusValue_t value;
   rbusValue_Init(&value);

#ifdef __APPLE__
   // macOS implementation: Use sysctl and vm_statistics
   int mib[2] = {CTL_HW, HW_MEMSIZE};
   uint64_t total_mem;
   size_t len = sizeof(total_mem);
   if (sysctl(mib, 2, &total_mem, &len, NULL, 0) == -1) {
      return RBUS_ERROR_BUS_ERROR;
   }

   mach_port_t host_port = mach_host_self();
   vm_size_t page_size;
   vm_statistics64_data_t vm_stat;
   unsigned int count = HOST_VM_INFO64_COUNT;

   if (host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stat, &count) != KERN_SUCCESS ||
      host_page_size(host_port, &page_size) != KERN_SUCCESS) {
      mach_port_deallocate(mach_task_self(), host_port);
      return RBUS_ERROR_BUS_ERROR;
   }
   mach_port_deallocate(mach_task_self(), host_port);

   // Calculate free memory: free + inactive pages
   uint64_t free_mem = (vm_stat.free_count + vm_stat.inactive_count) * page_size / 1024;

#else
   // Linux implementation: Parse /proc/meminfo
   FILE *fp = fopen("/proc/meminfo", "r");
   if (!fp) {
      return RBUS_ERROR_BUS_ERROR;
   }

   char line[256];
   unsigned long mem_free = 0, buffers = 0, cached = 0, sreclaimable = 0;
   while (fgets(line, sizeof(line), fp)) {
      if (sscanf(line, "MemFree: %lu kB", &mem_free) == 1 ||
         sscanf(line, "Buffers: %lu kB", &buffers) == 1 ||
         sscanf(line, "Cached: %lu kB", &cached) == 1 ||
         sscanf(line, "SReclaimable: %lu kB", &sreclaimable) == 1) {
         continue;
      }
   }
   fclose(fp);

   // Free memory: MemFree + Buffers + Cached + SReclaimable
   if (mem_free == 0) {
      return RBUS_ERROR_BUS_ERROR;
   }
   uint64_t free_mem = mem_free + buffers + cached + sreclaimable;
#endif

   // Set the rbus value as an unsigned int
   rbusValue_SetUInt32(value, (unsigned int)free_mem);
   rbusProperty_SetValue(property, value);
   rbusValue_Release(value);

   return RBUS_ERROR_SUCCESS;
}

static rbusError_t get_memory_used(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options) {
   rbusValue_t value;
   rbusValue_Init(&value);

#ifdef __APPLE__
   // macOS implementation: Use sysctl and vm_statistics
   int mib[2] = {CTL_HW, HW_MEMSIZE};
   uint64_t total_mem;
   size_t len = sizeof(total_mem);
   if (sysctl(mib, 2, &total_mem, &len, NULL, 0) == -1) {
      return RBUS_ERROR_BUS_ERROR;
   }

   mach_port_t host_port = mach_host_self();
   vm_size_t page_size;
   vm_statistics64_data_t vm_stat;
   unsigned int count = HOST_VM_INFO64_COUNT;

   if (host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stat, &count) != KERN_SUCCESS ||
      host_page_size(host_port, &page_size) != KERN_SUCCESS) {
      mach_port_deallocate(mach_task_self(), host_port);
      return RBUS_ERROR_BUS_ERROR;
   }
   mach_port_deallocate(mach_task_self(), host_port);

   // Calculate used memory: active + wired pages
   uint64_t used_mem = (vm_stat.active_count + vm_stat.wire_count) * page_size / 1024;

#else
   // Linux implementation: Parse /proc/meminfo
   FILE *fp = fopen("/proc/meminfo", "r");
   if (!fp) {
      return RBUS_ERROR_BUS_ERROR;
   }

   char line[256];
   unsigned long mem_total = 0, mem_free = 0, buffers = 0, cached = 0, sreclaimable = 0;
   while (fgets(line, sizeof(line), fp)) {
      if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1 ||
         sscanf(line, "MemFree: %lu kB", &mem_free) == 1 ||
         sscanf(line, "Buffers: %lu kB", &buffers) == 1 ||
         sscanf(line, "Cached: %lu kB", &cached) == 1 ||
         sscanf(line, "SReclaimable: %lu kB", &sreclaimable) == 1) {
         continue;
      }
   }
   fclose(fp);

   // Used memory: MemTotal - MemFree - Buffers - Cached - SReclaimable
   if (mem_total == 0 || mem_free == 0) {
      return RBUS_ERROR_BUS_ERROR;
   }
   uint64_t used_mem = mem_total - (mem_free + buffers + cached + sreclaimable);
#endif

   // Set the rbus value as an unsigned int
   rbusValue_SetUInt32(value, (unsigned int)used_mem);
   rbusProperty_SetValue(property, value);
   rbusValue_Release(value);

   return RBUS_ERROR_SUCCESS;
}

static rbusError_t get_memory_total(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options) {
   rbusValue_t value;
   rbusValue_Init(&value);

#ifdef __APPLE__
   // macOS implementation: Use sysctl
   int mib[2] = {CTL_HW, HW_MEMSIZE};
   uint64_t total_mem;
   size_t len = sizeof(total_mem);
   if (sysctl(mib, 2, &total_mem, &len, NULL, 0) == -1) {
      return RBUS_ERROR_BUS_ERROR;
   }

   // Convert to kilobytes
   uint64_t total_mem_kb = total_mem / 1024;

#else
   // Linux implementation: Parse /proc/meminfo
   FILE *fp = fopen("/proc/meminfo", "r");
   if (!fp) {
      return RBUS_ERROR_BUS_ERROR;
   }

   char line[256];
   unsigned long mem_total = 0;
   while (fgets(line, sizeof(line), fp)) {
      if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) {
         break;
      }
   }
   fclose(fp);

   if (mem_total == 0) {
      return RBUS_ERROR_BUS_ERROR;
   }
   uint64_t total_mem_kb = mem_total;
#endif

   // Set the rbus value as an unsigned int
   rbusValue_SetUInt32(value, (unsigned int)total_mem_kb);
   rbusProperty_SetValue(property, value);
   rbusValue_Release(value);

   return RBUS_ERROR_SUCCESS;
}

static rbusError_t get_local_time(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options) {
   rbusValue_t value;
   rbusValue_Init(&value);

   // Get current time
   time_t rawtime;
   if (time(&rawtime) == (time_t)-1) {
      return RBUS_ERROR_BUS_ERROR;
   }

   // Convert to local time
   struct tm time_struct;
   struct tm *timeinfo;
#ifdef __APPLE__
   // macOS: Use localtime_r for local time
   timeinfo = localtime_r(&rawtime, &time_struct);
#else
   // Linux: Use localtime_r for local time
   timeinfo = localtime_r(&rawtime, &time_struct);
#endif

   if (!timeinfo) {
      return RBUS_ERROR_BUS_ERROR;
   }

   // Format time as YYYY-MM-DDThh:mm:ss (e.g., 2024-02-07T23:52:32)
   char time_str[20]; // Buffer for formatted time (19 chars + null)
   if (strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", timeinfo) == 0) {
      return RBUS_ERROR_BUS_ERROR;
   }

   // Set the rbus value to the formatted time string
   rbusValue_SetString(value, time_str);

   rbusProperty_SetValue(property, value);
   rbusValue_Release(value);

   return RBUS_ERROR_SUCCESS;
}

DataModel gDataModels[] = {
   {
      .name = "Device.DeviceInfo.SerialNumber",
      .type = TYPE_STRING,
      .value.strVal = "unknown",
      .getHandler = get_system_serial_number,
      .setHandler = NULL,
   },
   {
      .name = "Device.DeviceInfo.X_RDKCENTRAL-COM_SystemTime",
      .type = TYPE_STRING,
      .value.strVal = "unknown",
      .getHandler = get_system_time,
      .setHandler = NULL,
   },
   {
      .name = "Device.DeviceInfo.UpTime",
      .type = TYPE_STRING,
      .value.strVal = "unknown",
      .getHandler = get_system_uptime,
      .setHandler = NULL,
   },
   {
      .name = "Device.DeviceInfo.X_COMCAST-COM_CM_MAC",
      .type = TYPE_STRING,
      .value.strVal = "unknown",
      .getHandler = get_mac_address,
      .setHandler = NULL,
   },
   {
      .name = "Device.DeviceInfo.MemoryStatus.Total",
      .type = TYPE_UINT,
      .value.uintVal = 0,
      .getHandler = get_memory_total,
      .setHandler = NULL,
   },
   {
      .name = "Device.DeviceInfo.MemoryStatus.Used",
      .type = TYPE_UINT,
      .value.uintVal = 0,
      .getHandler = get_memory_used,
      .setHandler = NULL,
   },
   {
      .name = "Device.DeviceInfo.MemoryStatus.Free",
      .type = TYPE_UINT,
      .value.uintVal = 0,
      .getHandler = get_memory_free,
      .setHandler = NULL,
   },
   {
      .name = "Device.Time.CurrentLocalTime",
      .type = TYPE_DATETIME,
      .value.strVal = "unknown",
      .getHandler = get_local_time,
      .setHandler = NULL,
   }
};

// Callback for handling value change events
void valueChangeHandler(rbusHandle_t handle, rbusEvent_t const *event, rbusEventSubscription_t *subscription) {
   rbusValue_t newValue = NULL;
   rbusObject_t data = event->data;

   // Extract new value from event data
   newValue = rbusObject_GetValue(data, "value");
   if (!newValue) {
      printf("Value change event for %s: No new value provided\n", event->name);
      return;
   }

   // Print the new value based on its type
   switch (rbusValue_GetType(newValue)) {
   case RBUS_STRING: {
      char *str = rbusValue_ToString(newValue, NULL, 0);
      printf("Value changed for %s: %s\n", event->name, str);
      free(str);
      break;
   }
   case RBUS_INT32:
      printf("Value changed for %s: %d\n", event->name, rbusValue_GetInt32(newValue));
      break;
   case RBUS_UINT32:
      printf("Value changed for %s: %u\n", event->name, rbusValue_GetUInt32(newValue));
      break;
   case RBUS_BOOLEAN:
      printf("Value changed for %s: %s\n", event->name, rbusValue_GetBoolean(newValue) ? "true" : "false");
      break;
   case RBUS_INT64:
      printf("Value changed for %s: %lld\n", event->name, (long long)rbusValue_GetInt64(newValue));
      break;
   case RBUS_UINT64:
      printf("Value changed for %s: %llu\n", event->name, (unsigned long long)rbusValue_GetUInt64(newValue));
      break;
   case RBUS_SINGLE:
      printf("Value changed for %s: %f\n", event->name, rbusValue_GetSingle(newValue));
      break;
   case RBUS_DOUBLE:
      printf("Value changed for %s: %lf\n", event->name, rbusValue_GetDouble(newValue));
      break;
   case RBUS_BYTE:
      printf("Value changed for %s: %u\n", event->name, rbusValue_GetByte(newValue));
      break;
   default:
      printf("Value changed for %s: Unsupported type\n", event->name);
      break;
   }
}

// Load data models from JSON file
bool loadDataModelsFromJson() {
   FILE *file = fopen(JSON_FILE, "r");
   if (!file) {
      fprintf(stderr, "Failed to open JSON file: %s\n", JSON_FILE);
      return false;
   }

   // Read file contents
   fseek(file, 0, SEEK_END);
   long file_size = ftell(file);
   fseek(file, 0, SEEK_SET);
   char *json_str = (char *)malloc(file_size + 1);
   if (!json_str) {
      fprintf(stderr, "Failed to allocate memory for JSON string\n");
      fclose(file);
      return false;
   }
   size_t read_size = fread(json_str, 1, file_size, file);
   json_str[read_size] = '\0';
   fclose(file);

   // Parse JSON
   cJSON *root = cJSON_Parse(json_str);
   free(json_str);
   if (!root) {
      fprintf(stderr, "Failed to parse JSON: %s\n", cJSON_GetErrorPtr());
      return false;
   }

   if (!cJSON_IsArray(root)) {
      fprintf(stderr, "JSON root is not an array\n");
      cJSON_Delete(root);
      return false;
   }

   numDataModels = cJSON_GetArraySize(root);
   if (numDataModels == 0) {
      fprintf(stderr, "No data models found in JSON\n");
      cJSON_Delete(root);
      return false;
   }

   numgDataModels = (sizeof(gDataModels) / sizeof(DataModel));
   totalDataModels = numDataModels + numgDataModels;

   // Dynamically allocate memory for dataModels
   dataModels = (DataModel *)malloc(totalDataModels * sizeof(DataModel));
   if (!dataModels) {
      fprintf(stderr, "Failed to allocate memory for data models\n");
      cJSON_Delete(root);
      return false;
   }

   memset(dataModels, 0, numDataModels * sizeof(DataModel));
   int i = 0;
   for (i = 0; i < numDataModels; i++) {
      cJSON *item = cJSON_GetArrayItem(root, i);
      if (!cJSON_IsObject(item)) {
         fprintf(stderr, "Item %d is not an object\n", i);
         free(dataModels);
         dataModels = NULL;
         cJSON_Delete(root);
         return false;
      }

      cJSON *name_obj = cJSON_GetObjectItem(item, "name");
      cJSON *type_obj = cJSON_GetObjectItem(item, "type");
      cJSON *value_obj = cJSON_GetObjectItem(item, "value");

      if (!cJSON_IsString(name_obj) || !cJSON_IsNumber(type_obj) || type_obj->valuedouble < 0 || type_obj->valuedouble > 10) {
         fprintf(stderr, "Invalid name or type for item %d\n", i);
         free(dataModels);
         dataModels = NULL;
         cJSON_Delete(root);
         return false;
      }

      const char *name = cJSON_GetStringValue(name_obj);
      int type = (int)cJSON_GetNumberValue(type_obj);
      strncpy(dataModels[i].name, name, MAX_NAME_LEN - 1);
      dataModels[i].name[MAX_NAME_LEN - 1] = '\0';
      dataModels[i].type = (ValueType)type;

      switch (type) {
      case TYPE_STRING:
      case TYPE_DATETIME:
      case TYPE_BASE64:
         dataModels[i].value.strVal = value_obj && cJSON_IsString(value_obj) ? strdup(cJSON_GetStringValue(value_obj)) : strdup("");
         break;
      case TYPE_INT:
         if (value_obj && cJSON_IsNumber(value_obj)) {
            double val = cJSON_GetNumberValue(value_obj);
            if (val >= INT32_MIN && val <= INT32_MAX) {
               dataModels[i].value.intVal = (int32_t)val;
            } else {
               fprintf(stderr, "Value out of range for TYPE_INT at item %d\n", i);
               free(dataModels);
               dataModels = NULL;
               cJSON_Delete(root);
               return false;
            }
         } else {
            dataModels[i].value.intVal = 0;
         }
         break;
      case TYPE_UINT:
         if (value_obj && cJSON_IsNumber(value_obj)) {
            double val = cJSON_GetNumberValue(value_obj);
            if (val >= 0 && val <= UINT32_MAX) {
               dataModels[i].value.uintVal = (uint32_t)val;
            } else {
               fprintf(stderr, "Value out of range for TYPE_UINT at item %d\n", i);
               free(dataModels);
               dataModels = NULL;
               cJSON_Delete(root);
               return false;
            }
         } else {
            dataModels[i].value.uintVal = 0;
         }
         break;
      case TYPE_BOOL:
         dataModels[i].value.boolVal = value_obj && (cJSON_IsTrue(value_obj) || cJSON_IsFalse(value_obj)) ? cJSON_IsTrue(value_obj) : false;
         break;
      case TYPE_LONG:
         if (value_obj && cJSON_IsNumber(value_obj)) {
            double val = cJSON_GetNumberValue(value_obj);
            if (val >= INT64_MIN && val <= INT64_MAX) {
               dataModels[i].value.longVal = (int64_t)val;
            } else {
               fprintf(stderr, "Value out of range for TYPE_LONG at item %d\n", i);
               free(dataModels);
               dataModels = NULL;
               cJSON_Delete(root);
               return false;
            }
         } else {
            dataModels[i].value.longVal = 0;
         }
         break;
      case TYPE_ULONG:
         if (value_obj && cJSON_IsNumber(value_obj)) {
            double val = cJSON_GetNumberValue(value_obj);
            if (val >= 0 && val <= UINT64_MAX) {
               dataModels[i].value.ulongVal = (uint64_t)val;
            } else {
               fprintf(stderr, "Value out of range for TYPE_ULONG at item %d\n", i);
               free(dataModels);
               dataModels = NULL;
               cJSON_Delete(root);
               return false;
            }
         } else {
            dataModels[i].value.ulongVal = 0;
         }
         break;
      case TYPE_FLOAT:
         dataModels[i].value.floatVal = value_obj && cJSON_IsNumber(value_obj) ? (float)cJSON_GetNumberValue(value_obj) : 0.0f;
         break;
      case TYPE_DOUBLE:
         dataModels[i].value.doubleVal = value_obj && cJSON_IsNumber(value_obj) ? cJSON_GetNumberValue(value_obj) : 0.0;
         break;
      case TYPE_BYTE:
         if (value_obj && cJSON_IsNumber(value_obj)) {
            double val = cJSON_GetNumberValue(value_obj);
            if (val >= 0 && val <= UINT8_MAX) {
               dataModels[i].value.byteVal = (uint8_t)val;
            } else {
               fprintf(stderr, "Value out of range for TYPE_BYTE at item %d\n", i);
               free(dataModels);
               dataModels = NULL;
               cJSON_Delete(root);
               return false;
            }
         } else {
            dataModels[i].value.byteVal = 0;
         }
         break;
      }
   }

   for (int j = 0; i < totalDataModels; i++, j++) {
      int type = gDataModels[j].type;
      strncpy(dataModels[i].name, gDataModels[j].name, MAX_NAME_LEN - 1);
      dataModels[i].name[MAX_NAME_LEN - 1] = '\0';
      dataModels[i].type = (ValueType)type;
      dataModels[i].getHandler = gDataModels[j].getHandler;
      dataModels[i].setHandler = gDataModels[j].setHandler;

      switch (type) {
      case TYPE_STRING:
      case TYPE_DATETIME:
      case TYPE_BASE64:
         dataModels[i].value.strVal = strdup(gDataModels[j].value.strVal);
         break;

      default:
         dataModels[i].value = gDataModels[j].value;
         break;
      }
   }

   cJSON_Delete(root);
   return true;
}

// Callback for handling get requests
rbusError_t getHandler(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options) {
   char const *name = rbusProperty_GetName(property);
   for (int i = 0; i < numDataModels; i++) {
      if (strcmp(name, dataModels[i].name) == 0) {
         rbusValue_t value;
         rbusValue_Init(&value);
         switch (dataModels[i].type) {
         case TYPE_STRING:
         case TYPE_DATETIME:
         case TYPE_BASE64:
            rbusValue_SetString(value, dataModels[i].value.strVal);
            break;
         case TYPE_INT:
            rbusValue_SetInt32(value, dataModels[i].value.intVal);
            break;
         case TYPE_UINT:
            rbusValue_SetUInt32(value, dataModels[i].value.uintVal);
            break;
         case TYPE_BOOL:
            rbusValue_SetBoolean(value, dataModels[i].value.boolVal);
            break;
         case TYPE_LONG:
            rbusValue_SetInt64(value, dataModels[i].value.longVal);
            break;
         case TYPE_ULONG:
            rbusValue_SetUInt64(value, dataModels[i].value.ulongVal);
            break;
         case TYPE_FLOAT:
            rbusValue_SetSingle(value, dataModels[i].value.floatVal);
            break;
         case TYPE_DOUBLE:
            rbusValue_SetDouble(value, dataModels[i].value.doubleVal);
            break;
         case TYPE_BYTE:
            rbusValue_SetByte(value, dataModels[i].value.byteVal);
            break;
         }
         rbusProperty_SetValue(property, value);
         rbusValue_Release(value);
         return RBUS_ERROR_SUCCESS;
      }
   }
   return RBUS_ERROR_INVALID_INPUT;
}

// Callback for handling set requests
rbusError_t setHandler(rbusHandle_t handle, rbusProperty_t property, rbusSetHandlerOptions_t *options) {
   char const *name = rbusProperty_GetName(property);
   rbusValue_t value = rbusProperty_GetValue(property);
   for (int i = 0; i < totalDataModels; i++) {
      if (strcmp(name, dataModels[i].name) == 0) {
         switch (dataModels[i].type) {
         case TYPE_STRING:
         case TYPE_DATETIME:
         case TYPE_BASE64: {
            char *str = rbusValue_ToString(value, NULL, 0);
            free(dataModels[i].value.strVal);
            dataModels[i].value.strVal = strdup(str);
            free(str);
            break;
         }
         case TYPE_INT: {
            int32_t val = rbusValue_GetInt32(value);
            dataModels[i].value.intVal = val;
            break;
         }
         case TYPE_UINT: {
            uint32_t val = rbusValue_GetUInt32(value);
            dataModels[i].value.uintVal = val;
            break;
         }
         case TYPE_BOOL: {
            bool val = rbusValue_GetBoolean(value);
            dataModels[i].value.boolVal = val;
            break;
         }
         case TYPE_LONG: {
            int64_t val = rbusValue_GetInt64(value);
            dataModels[i].value.longVal = val;
            break;
         }
         case TYPE_ULONG: {
            uint64_t val = rbusValue_GetUInt64(value);
            dataModels[i].value.ulongVal = val;
            break;
         }
         case TYPE_FLOAT: {
            float val = rbusValue_GetSingle(value);
            dataModels[i].value.floatVal = val;
            break;
         }
         case TYPE_DOUBLE: {
            double val = rbusValue_GetDouble(value);
            dataModels[i].value.doubleVal = val;
            break;
         }
         case TYPE_BYTE: {
            uint8_t val = rbusValue_GetByte(value);
            dataModels[i].value.byteVal = val;
            break;
         }
         }
         return RBUS_ERROR_SUCCESS;
      }
   }
   return RBUS_ERROR_INVALID_INPUT;
}

rbusError_t eventSubHandler(rbusHandle_t handle, rbusEventSubAction_t action, const char *eventName, rbusFilter_t filter, int32_t interval, bool *autoPublish) {
   (void)handle;
   (void)filter;
   (void)autoPublish;
   (void)interval;
   printf("Subscribe handler called for %s, action: %s\r\n", eventName,
      action == RBUS_EVENT_ACTION_SUBSCRIBE ? "subscribe" : "unsubscribe");
   return RBUS_ERROR_SUCCESS;
}

int main(int argc, char *argv[]) {
   rbusHandle_t rbusHandle;
   rbusError_t rc;

   // Load data models from JSON
   if (!loadDataModelsFromJson()) {
      fprintf(stderr, "Failed to load data models from %s\n", JSON_FILE);
      return 1;
   }

   // Initialize rbus
   rc = rbus_open(&rbusHandle, "tr181-datamodels");
   if (rc != RBUS_ERROR_SUCCESS) {
      fprintf(stderr, "Failed to open rbus: %d\n", rc);
      free(dataModels);
      dataModels = NULL;
      return 1;
   }

   // Dynamically allocate memory for dataElements
   rbusDataElement_t *dataElements = (rbusDataElement_t *)malloc(totalDataModels * sizeof(rbusDataElement_t));
   if (!dataElements) {
      fprintf(stderr, "Failed to allocate memory for data elements\n");
      free(dataModels);
      dataModels = NULL;
      rbus_close(rbusHandle);
      return 1;
   }

   int i = 0;
   for (i = 0; i < totalDataModels; i++) {
      dataElements[i].name = strdup(dataModels[i].name);
      dataElements[i].type = RBUS_ELEMENT_TYPE_PROPERTY;
      dataElements[i].cbTable.getHandler = (dataModels[i].getHandler != NULL) ? dataModels[i].getHandler : getHandler;
      dataElements[i].cbTable.setHandler = (dataModels[i].setHandler != NULL) ? dataModels[i].setHandler : setHandler; ;
      dataElements[i].cbTable.eventSubHandler = eventSubHandler;
   }

   rc = rbus_regDataElements(rbusHandle, totalDataModels, dataElements);
   if (rc != RBUS_ERROR_SUCCESS) {
      fprintf(stderr, "Failed to register data elements: %d\n", rc);
      for (int i = 0; i < numDataModels; i++) {
         free(dataElements[i].name);
      }
      free(dataElements);
      free(dataModels);
      dataModels = NULL;
      rbus_close(rbusHandle);
      return 1;
   }

   printf("Successfully registered %d data models\n", totalDataModels);

   // Set each data model's value
   for (i = 0; i < totalDataModels; i++) {
      // Set value
      rbusValue_t value;
      rbusValue_Init(&value);
      switch (dataModels[i].type) {
      case TYPE_STRING:
      case TYPE_DATETIME:
      case TYPE_BASE64:
         rbusValue_SetString(value, dataModels[i].value.strVal);
         break;
      case TYPE_INT:
         rbusValue_SetInt32(value, dataModels[i].value.intVal);
         break;
      case TYPE_UINT:
         rbusValue_SetUInt32(value, dataModels[i].value.uintVal);
         break;
      case TYPE_BOOL:
         rbusValue_SetBoolean(value, dataModels[i].value.boolVal);
         break;
      case TYPE_LONG:
         rbusValue_SetInt64(value, dataModels[i].value.longVal);
         break;
      case TYPE_ULONG:
         rbusValue_SetUInt64(value, dataModels[i].value.ulongVal);
         break;
      case TYPE_FLOAT:
         rbusValue_SetSingle(value, dataModels[i].value.floatVal);
         break;
      case TYPE_DOUBLE:
         rbusValue_SetDouble(value, dataModels[i].value.doubleVal);
         break;
      case TYPE_BYTE:
         rbusValue_SetByte(value, dataModels[i].value.byteVal);
         break;
      }

      rbusSetOptions_t opts = {.commit = true};
      rc = rbus_set(rbusHandle, dataModels[i].name, value, &opts);
      if (rc != RBUS_ERROR_SUCCESS) {
         fprintf(stderr, "Failed to set %s: %d\n", dataModels[i].name, rc);
      }
      rbusValue_Release(value);
   }

   // Keep the program running to handle rbus events
   printf("Press Ctrl+C to exit\n");
   while (1) {
      sleep(1);
   }

   // Cleanup
   for (int i = 0; i < numDataModels; i++) {
      // Unsubscribe from events
      rbusEvent_Unsubscribe(rbusHandle, dataModels[i].name);
      // Free string values and data element names
      if (dataModels[i].type == TYPE_STRING || dataModels[i].type == TYPE_DATETIME || dataModels[i].type == TYPE_BASE64) {
         free(dataModels[i].value.strVal);
      }
      free(dataElements[i].name);
   }
   free(dataElements);
   free(dataModels);
   dataModels = NULL;
   rbus_unregDataElements(rbusHandle, numDataModels, dataElements);
   rbus_close(rbusHandle);
   return 0;
}