/*
 * Minimal UEFI definitions for the SurfaceRT_OS loader.
 *
 * Only the pieces we actually call are typed; every other table slot is kept
 * as a placeholder so that the struct offsets match the UEFI 2.x spec exactly.
 */
#ifndef SRTOS_EFI_H
#define SRTOS_EFI_H

#include <stdint.h>
#include <stddef.h>

#define EFIAPI

#define EFI_ERROR(err) ((int64_t)(err) < 0)
#define EFI_SUCCESS 0
#define EFI_LOAD_ERROR 1
#define EFI_INVALID_PARAMETER 2
#define EFI_UNSUPPORTED 3
#define EFI_BUFFER_TOO_SMALL 5
#define EFI_NOT_FOUND 14
#define EFI_ABORTED 21

#define EFI_PAGE_SIZE 4096u
#define EFI_SIZE_TO_PAGES(sz) (((sz) + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE)

#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_MAX_ADDRESS 1
#define EFI_ALLOCATE_ADDRESS 2

#define EFI_LOADER_DATA 2
#define EFI_CONVENTIONAL_MEMORY 7
#define EFI_ACPI_RECLAIM_MEMORY 9

#define EFI_OPEN_MODE_READ 0x1
#define EFI_OPEN_MODE_WRITE 0x2
#define EFI_OPEN_MODE_CREATE 0x8000000000000000ull

typedef uint64_t efi_status;
typedef void *efi_handle;
typedef uint64_t efi_tpl;

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} efi_guid;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header;

typedef struct {
    uint16_t year, month, day, hour, minute, second, pad1, nanosecond;
    int16_t timezone;
    uint8_t daylight;
    uint8_t pad2[3];
} efi_time;

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor;

typedef struct {
    uint32_t revision;
    void *parent_handle;
    void *system_table;
    void *device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    uint32_t image_code_type;
    uint32_t image_data_type;
    void *unload;
} efi_loaded_image;

typedef struct efi_simple_text_output efi_simple_text_output;
struct efi_simple_text_output {
    efi_status (*reset)(efi_simple_text_output *, uint8_t);
    efi_status (*output_string)(efi_simple_text_output *, const uint16_t *);
    efi_status (*test_string)(efi_simple_text_output *, const uint16_t *);
    efi_status (*query_mode)(efi_simple_text_output *, size_t, size_t *, size_t *);
    efi_status (*set_mode)(efi_simple_text_output *, size_t);
    efi_status (*set_attribute)(efi_simple_text_output *, size_t);
    efi_status (*clear_screen)(efi_simple_text_output *);
    efi_status (*set_cursor_position)(efi_simple_text_output *, size_t, size_t);
    efi_status (*enable_cursor)(efi_simple_text_output *, uint8_t);
    void *mode;
};

typedef struct {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    efi_time create_time, last_access_time, modification_time;
    uint64_t attribute;
    uint16_t file_name[1];
} efi_file_info;

typedef struct efi_file efi_file;
struct efi_file {
    uint64_t revision;
    efi_status (*open)(efi_file *, efi_file **, const uint16_t *, uint64_t, uint64_t);
    efi_status (*close)(efi_file *);
    efi_status (*delete)(efi_file *);
    efi_status (*read)(efi_file *, size_t *, void *);
    efi_status (*write)(efi_file *, size_t *, void *);
    efi_status (*get_position)(efi_file *, uint64_t *);
    efi_status (*set_position)(efi_file *, uint64_t);
    efi_status (*get_info)(efi_file *, const efi_guid *, size_t *, void *);
    efi_status (*set_info)(efi_file *, const efi_guid *, size_t, void *);
    efi_status (*flush)(efi_file *);
};

typedef struct {
    uint64_t revision;
    efi_status (*open_volume)(void *self, efi_file **root);
} efi_simple_file_system;

typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;      /* 0 = RGBX, 1 = BGRX, 2 = bitmask, 3 = blt only */
    uint32_t pixel_information[4];
    uint32_t pixels_per_scan_line;
} efi_gop_mode_info;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    efi_gop_mode_info *info;
    uint64_t size_of_info;
    uint64_t frame_buffer_base;
    uint64_t frame_buffer_size;
} efi_gop_mode;

typedef struct {
    efi_status (*query_mode)(void *, uint32_t, size_t *, efi_gop_mode_info **);
    efi_status (*set_mode)(void *, uint32_t);
    efi_status (*blt)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t,
                      uint32_t, uint32_t, uint32_t, void *);
    efi_gop_mode *mode;
} efi_graphics_output;

typedef struct efi_boot_services efi_boot_services;
struct efi_boot_services {
    efi_table_header hdr;
    efi_tpl (*raise_tpl)(efi_tpl);
    void (*restore_tpl)(efi_tpl);
    efi_status (*allocate_pages)(uint32_t, uint32_t, size_t, uint64_t *);
    efi_status (*free_pages)(uint64_t, size_t);
    efi_status (*get_memory_map)(size_t *, efi_memory_descriptor *, size_t *,
                                 size_t *, uint32_t *);
    efi_status (*allocate_pool)(uint32_t, size_t, void **);
    efi_status (*free_pool)(void *);
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    efi_status (*handle_protocol)(efi_handle, const efi_guid *, void **);
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    efi_status (*exit_boot_services)(efi_handle, size_t);
    void *get_next_monotonic_count;
    efi_status (*stall)(size_t);
    efi_status (*set_watchdog_timer)(size_t, uint64_t, size_t, uint16_t *);
    void *connect_controller;
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    efi_status (*locate_handle_buffer)(uint32_t, const efi_guid *, void *, size_t *,
                                       efi_handle **);
    efi_status (*locate_protocol)(const efi_guid *, void *, void **);
    void *install_multiple_protocol_interfaces;
    void *uninstall_multiple_protocol_interfaces;
    void *calculate_crc32;
    void (*copy_mem)(void *, const void *, size_t);
    void (*set_mem)(void *, size_t, uint8_t);
    void *create_event_ex;
};

typedef struct efi_system_table efi_system_table;
struct efi_system_table {
    efi_table_header hdr;
    uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    efi_handle console_in_handle;
    void *con_in;
    efi_handle console_out_handle;
    efi_simple_text_output *con_out;
    efi_handle standard_error_handle;
    efi_simple_text_output *std_err;
    void *runtime_services;
    efi_boot_services *boot_services;
    size_t number_of_table_entries;
    void *configuration_table;
};

/* {9042a9de-23dc-4a38-96fb-7aded080516a} */
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }
/* {964e5b22-6459-11d2-8e39-00a0c969723b} */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
/* {964e5b22-6459-11d2-8e39-00a0c969723b} */
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
/* {09576e92-6d3f-11d2-8e39-00a0c969723b} */
#define EFI_FILE_INFO_GUID \
    { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#endif /* SRTOS_EFI_H */
