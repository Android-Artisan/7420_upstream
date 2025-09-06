/******************************************************************************
 *
 * Name: actbl1.h - Additional ACPI table definitions
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
 * Copyright (C) 2000 - 2015, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACTBL1_H__
#define __ACTBL1_H__

/*******************************************************************************
 *
 * Additional ACPI Tables
 * Additional ACPI Tables (1)
 *
 * These tables are not consumed directly by the ACPICA subsystem, but are
 * included here to support device drivers and the AML disassembler.
 *
 ******************************************************************************/

/*
 * Values for description table header signatures. Useful because they make
 * it more difficult to inadvertently type in the wrong signature.
 */
#define ACPI_SIG_ASF            "ASF!"	/* Alert Standard Format table */
#define ACPI_SIG_BERT           "BERT"	/* Boot Error Record Table */
#define ACPI_SIG_BOOT           "BOOT"	/* Simple Boot Flag Table */
#define ACPI_SIG_CPEP           "CPEP"	/* Corrected Platform Error Polling table */
#define ACPI_SIG_DBGP           "DBGP"	/* Debug Port table */
#define ACPI_SIG_DMAR           "DMAR"	/* DMA Remapping table */
 * The tables in this file are fully defined within the ACPI specification.
 *
 ******************************************************************************/

/*
 * Values for description table header signatures for tables defined in this
 * file. Useful because they make it more difficult to inadvertently type in
 * the wrong signature.
 */
#define ACPI_SIG_BERT           "BERT"	/* Boot Error Record Table */
#define ACPI_SIG_CPEP           "CPEP"	/* Corrected Platform Error Polling table */
#define ACPI_SIG_ECDT           "ECDT"	/* Embedded Controller Boot Resources Table */
#define ACPI_SIG_EINJ           "EINJ"	/* Error Injection table */
#define ACPI_SIG_ERST           "ERST"	/* Error Record Serialization Table */
#define ACPI_SIG_HEST           "HEST"	/* Hardware Error Source Table */
#define ACPI_SIG_HPET           "HPET"	/* High Precision Event Timer table */
#define ACPI_SIG_IBFT           "IBFT"	/* i_sCSI Boot Firmware Table */
#define ACPI_SIG_MADT           "APIC"	/* Multiple APIC Description Table */
#define ACPI_SIG_MCFG           "MCFG"	/* PCI Memory Mapped Configuration table */
#define ACPI_SIG_SBST           "SBST"	/* Smart Battery Specification Table */
#define ACPI_SIG_SLIC           "SLIC"	/* Software Licensing Description Table */
#define ACPI_SIG_SLIT           "SLIT"	/* System Locality Distance Information Table */
#define ACPI_SIG_SPCR           "SPCR"	/* Serial Port Console Redirection table */
#define ACPI_SIG_SPMI           "SPMI"	/* Server Platform Management Interface table */
#define ACPI_SIG_SRAT           "SRAT"	/* System Resource Affinity Table */
#define ACPI_SIG_TCPA           "TCPA"	/* Trusted Computing Platform Alliance table */
#define ACPI_SIG_UEFI           "UEFI"	/* Uefi Boot Optimization Table */
#define ACPI_SIG_WDAT           "WDAT"	/* Watchdog Action Table */
#define ACPI_SIG_WDRT           "WDRT"	/* Watchdog Resource Table */
#define ACPI_SIG_MADT           "APIC"	/* Multiple APIC Description Table */
#define ACPI_SIG_MSCT           "MSCT"	/* Maximum System Characteristics Table */
#define ACPI_SIG_SBST           "SBST"	/* Smart Battery Specification Table */
#define ACPI_SIG_SLIT           "SLIT"	/* System Locality Distance Information Table */
#define ACPI_SIG_SRAT           "SRAT"	/* System Resource Affinity Table */
#define ACPI_SIG_NFIT           "NFIT"	/* NVDIMM Firmware Interface Table */

/*
 * All tables must be byte-packed to match the ACPI specification, since
 * the tables are provided by the system BIOS.
 */
#pragma pack(1)

/*
 * Note about bitfields: The u8 type is used for bitfields in ACPI tables.
 * This is the only type that is even remotely portable. Anything else is not
 * portable, so do not use any other bitfield types.
 */

/* Common Subtable header (used in MADT, SRAT, etc.) */
 * Note: C bitfields are not used for this reason:
 *
 * "Bitfields are great and easy to read, but unfortunately the C language
 * does not specify the layout of bitfields in memory, which means they are
 * essentially useless for dealing with packed data in on-disk formats or
 * binary wire protocols." (Or ACPI tables and buffers.) "If you ask me,
 * this decision was a design error in C. Ritchie could have picked an order
 * and stuck with it." Norman Ramsey.
 * See http://stackoverflow.com/a/1053662/41661
 */

/*******************************************************************************
 *
 * Common subtable headers
 *
 ******************************************************************************/

/* Generic subtable header (used in MADT, SRAT, etc.) */

struct acpi_subtable_header {
	u8 type;
	u8 length;
};

/* Common Subtable header for WHEA tables (EINJ, ERST, WDAT) */
/* Subtable header for WHEA tables (EINJ, ERST, WDAT) */

struct acpi_whea_header {
	u8 action;
	u8 instruction;
	u8 flags;
	u8 reserved;
	struct acpi_generic_address register_region;
	u64 value;		/* Value used with Read/Write register */
	u64 mask;		/* Bitmask required for this register instruction */
};

/*******************************************************************************
 *
 * ASF - Alert Standard Format table (Signature "ASF!")
 *
 * Conforms to the Alert Standard Format Specification V2.0, 23 April 2003
 *
 ******************************************************************************/

struct acpi_table_asf {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/* ASF subtable header */

struct acpi_asf_header {
	u8 type;
	u8 reserved;
	u16 length;
};

/* Values for Type field above */

enum acpi_asf_type {
	ACPI_ASF_TYPE_INFO = 0,
	ACPI_ASF_TYPE_ALERT = 1,
	ACPI_ASF_TYPE_CONTROL = 2,
	ACPI_ASF_TYPE_BOOT = 3,
	ACPI_ASF_TYPE_ADDRESS = 4,
	ACPI_ASF_TYPE_RESERVED = 5
};

/*
 * ASF subtables
 */

/* 0: ASF Information */

struct acpi_asf_info {
	struct acpi_asf_header header;
	u8 min_reset_value;
	u8 min_poll_interval;
	u16 system_id;
	u32 mfg_id;
	u8 flags;
	u8 reserved2[3];
};

/* 1: ASF Alerts */

struct acpi_asf_alert {
	struct acpi_asf_header header;
	u8 assert_mask;
	u8 deassert_mask;
	u8 alerts;
	u8 data_length;
};

struct acpi_asf_alert_data {
	u8 address;
	u8 command;
	u8 mask;
	u8 value;
	u8 sensor_type;
	u8 type;
	u8 offset;
	u8 source_type;
	u8 severity;
	u8 sensor_number;
	u8 entity;
	u8 instance;
};

/* 2: ASF Remote Control */

struct acpi_asf_remote {
	struct acpi_asf_header header;
	u8 controls;
	u8 data_length;
	u16 reserved2;
};

struct acpi_asf_control_data {
	u8 function;
	u8 address;
	u8 command;
	u8 value;
};

/* 3: ASF RMCP Boot Options */

struct acpi_asf_rmcp {
	struct acpi_asf_header header;
	u8 capabilities[7];
	u8 completion_code;
	u32 enterprise_id;
	u8 command;
	u16 parameter;
	u16 boot_options;
	u16 oem_parameters;
};

/* 4: ASF Address */

struct acpi_asf_address {
	struct acpi_asf_header header;
	u8 eprom_address;
	u8 devices;
};

/*******************************************************************************
 *
 * BERT - Boot Error Record Table
 * BERT - Boot Error Record Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_bert {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 region_length;	/* Length of the boot error region */
	u64 address;		/* Physical addresss of the error region */
};

/* Boot Error Region */

struct acpi_bert_region {
	u32 block_status;
	u32 raw_data_offset;
	u32 raw_data_length;
	u32 data_length;
	u32 error_severity;
};

/* block_status Flags */

#define ACPI_BERT_UNCORRECTABLE             (1)
#define ACPI_BERT_CORRECTABLE               (2)
#define ACPI_BERT_MULTIPLE_UNCORRECTABLE    (4)
#define ACPI_BERT_MULTIPLE_CORRECTABLE      (8)

/*******************************************************************************
 *
 * BOOT - Simple Boot Flag Table
 *
 ******************************************************************************/

struct acpi_table_boot {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 cmos_index;		/* Index in CMOS RAM for the boot register */
	u8 reserved[3];
};

/*******************************************************************************
 *
 * CPEP - Corrected Platform Error Polling table
	u64 address;		/* Physical address of the error region */
};

/* Boot Error Region (not a subtable, pointed to by Address field above) */

struct acpi_bert_region {
	u32 block_status;	/* Type of error information */
	u32 raw_data_offset;	/* Offset to raw error data */
	u32 raw_data_length;	/* Length of raw error data */
	u32 data_length;	/* Length of generic error data */
	u32 error_severity;	/* Severity code */
};

/* Values for block_status flags above */

#define ACPI_BERT_UNCORRECTABLE             (1)
#define ACPI_BERT_CORRECTABLE               (1<<1)
#define ACPI_BERT_MULTIPLE_UNCORRECTABLE    (1<<2)
#define ACPI_BERT_MULTIPLE_CORRECTABLE      (1<<3)
#define ACPI_BERT_ERROR_ENTRY_COUNT         (0xFF<<4)	/* 8 bits, error count */

/* Values for error_severity above */

enum acpi_bert_error_severity {
	ACPI_BERT_ERROR_CORRECTABLE = 0,
	ACPI_BERT_ERROR_FATAL = 1,
	ACPI_BERT_ERROR_CORRECTED = 2,
	ACPI_BERT_ERROR_NONE = 3,
	ACPI_BERT_ERROR_RESERVED = 4	/* 4 and greater are reserved */
};

/*
 * Note: The generic error data that follows the error_severity field above
 * uses the struct acpi_hest_generic_data defined under the HEST table below
 */

/*******************************************************************************
 *
 * CPEP - Corrected Platform Error Polling table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_cpep {
	struct acpi_table_header header;	/* Common ACPI table header */
	u64 reserved;
};

/* Subtable */

struct acpi_cpep_polling {
	u8 type;
	u8 length;
	struct acpi_subtable_header header;
	u8 id;			/* Processor ID */
	u8 eid;			/* Processor EID */
	u32 interval;		/* Polling interval (msec) */
};

/*******************************************************************************
 *
 * DBGP - Debug Port table
 *
 ******************************************************************************/

struct acpi_table_dbgp {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 type;		/* 0=full 16550, 1=subset of 16550 */
	u8 reserved[3];
	struct acpi_generic_address debug_port;
};

/*******************************************************************************
 *
 * DMAR - DMA Remapping table
 *	  From "Intel Virtualization Technology for Directed I/O", Sept. 2007
 *
 ******************************************************************************/

struct acpi_table_dmar {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 width;		/* Host Address Width */
	u8 flags;
	u8 reserved[10];
};

/* Flags */

#define ACPI_DMAR_INTR_REMAP	    (1)

/* DMAR subtable header */

struct acpi_dmar_header {
	u16 type;
	u16 length;
};

/* Values for subtable type in struct acpi_dmar_header */

enum acpi_dmar_type {
	ACPI_DMAR_TYPE_HARDWARE_UNIT = 0,
	ACPI_DMAR_TYPE_RESERVED_MEMORY = 1,
	ACPI_DMAR_TYPE_ATSR = 2,
	ACPI_DMAR_TYPE_RESERVED = 3	/* 3 and greater are reserved */
};

struct acpi_dmar_device_scope {
	u8 entry_type;
	u8 length;
	u16 reserved;
	u8 enumeration_id;
	u8 bus;
};

/* Values for entry_type in struct acpi_dmar_device_scope */

enum acpi_dmar_scope_type {
	ACPI_DMAR_SCOPE_TYPE_NOT_USED = 0,
	ACPI_DMAR_SCOPE_TYPE_ENDPOINT = 1,
	ACPI_DMAR_SCOPE_TYPE_BRIDGE = 2,
	ACPI_DMAR_SCOPE_TYPE_IOAPIC = 3,
	ACPI_DMAR_SCOPE_TYPE_HPET = 4,
	ACPI_DMAR_SCOPE_TYPE_RESERVED = 5	/* 5 and greater are reserved */
};

struct acpi_dmar_pci_path {
	u8 dev;
	u8 fn;
};

/*
 * DMAR Sub-tables, correspond to Type in struct acpi_dmar_header
 */

/* 0: Hardware Unit Definition */

struct acpi_dmar_hardware_unit {
	struct acpi_dmar_header header;
	u8 flags;
	u8 reserved;
	u16 segment;
	u64 address;		/* Register Base Address */
};

/* Flags */

#define ACPI_DMAR_INCLUDE_ALL       (1)

/* 1: Reserved Memory Defininition */

struct acpi_dmar_reserved_memory {
	struct acpi_dmar_header header;
	u16 reserved;
	u16 segment;
	u64 base_address;		/* 4_k aligned base address */
	u64 end_address;	/* 4_k aligned limit address */
};

/* Flags */

#define ACPI_DMAR_ALLOW_ALL         (1)


/* 2: Root Port ATS Capability Reporting Structure */

struct acpi_dmar_atsr {
       struct acpi_dmar_header header;
       u8 flags;
       u8 reserved;
       u16 segment;
};

/* Flags */

#define ACPI_DMAR_ALL_PORTS	    (1)

/*******************************************************************************
 *
 * ECDT - Embedded Controller Boot Resources Table
 * ECDT - Embedded Controller Boot Resources Table
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_ecdt {
	struct acpi_table_header header;	/* Common ACPI table header */
	struct acpi_generic_address control;	/* Address of EC command/status register */
	struct acpi_generic_address data;	/* Address of EC data register */
	u32 uid;		/* Unique ID - must be same as the EC _UID method */
	u8 gpe;			/* The GPE for the EC */
	u8 id[1];		/* Full namepath of the EC in the ACPI namespace */
};

/*******************************************************************************
 *
 * EINJ - Error Injection Table
 * EINJ - Error Injection Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_einj {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 header_length;
	u32 reserved;
	u8 flags;
	u8 reserved[3];
	u32 entries;
};

/* EINJ Injection Instruction Entries (actions) */

struct acpi_einj_entry {
	struct acpi_whea_header whea_header;	/* Common header for WHEA tables */
};

/* Masks for Flags field above */

#define ACPI_EINJ_PRESERVE          (1)

/* Values for Action field above */

enum acpi_einj_actions {
	ACPI_EINJ_BEGIN_OPERATION = 0,
	ACPI_EINJ_GET_TRIGGER_TABLE = 1,
	ACPI_EINJ_SET_ERROR_TYPE = 2,
	ACPI_EINJ_GET_ERROR_TYPE = 3,
	ACPI_EINJ_END_OPERATION = 4,
	ACPI_EINJ_EXECUTE_OPERATION = 5,
	ACPI_EINJ_CHECK_BUSY_STATUS = 6,
	ACPI_EINJ_GET_COMMAND_STATUS = 7,
	ACPI_EINJ_ACTION_RESERVED = 8,	/* 8 and greater are reserved */
	ACPI_EINJ_SET_ERROR_TYPE_WITH_ADDRESS = 8,
	ACPI_EINJ_ACTION_RESERVED = 9,	/* 9 and greater are reserved */
	ACPI_EINJ_TRIGGER_ERROR = 0xFF	/* Except for this value */
};

/* Values for Instruction field above */

enum acpi_einj_instructions {
	ACPI_EINJ_READ_REGISTER = 0,
	ACPI_EINJ_READ_REGISTER_VALUE = 1,
	ACPI_EINJ_WRITE_REGISTER = 2,
	ACPI_EINJ_WRITE_REGISTER_VALUE = 3,
	ACPI_EINJ_NOOP = 4,
	ACPI_EINJ_INSTRUCTION_RESERVED = 5	/* 5 and greater are reserved */
	ACPI_EINJ_FLUSH_CACHELINE = 5,
	ACPI_EINJ_INSTRUCTION_RESERVED = 6	/* 6 and greater are reserved */
};

struct acpi_einj_error_type_with_addr {
	u32 error_type;
	u32 vendor_struct_offset;
	u32 flags;
	u32 apic_id;
	u64 address;
	u64 range;
	u32 pcie_id;
};

struct acpi_einj_vendor {
	u32 length;
	u32 pcie_id;
	u16 vendor_id;
	u16 device_id;
	u8 revision_id;
	u8 reserved[3];
};

/* EINJ Trigger Error Action Table */

struct acpi_einj_trigger {
	u32 header_size;
	u32 revision;
	u32 table_size;
	u32 entry_count;
};

/*******************************************************************************
 *
 * ERST - Error Record Serialization Table
/* Command status return values */

enum acpi_einj_command_status {
	ACPI_EINJ_SUCCESS = 0,
	ACPI_EINJ_FAILURE = 1,
	ACPI_EINJ_INVALID_ACCESS = 2,
	ACPI_EINJ_STATUS_RESERVED = 3	/* 3 and greater are reserved */
};

/* Error types returned from ACPI_EINJ_GET_ERROR_TYPE (bitfield) */

#define ACPI_EINJ_PROCESSOR_CORRECTABLE     (1)
#define ACPI_EINJ_PROCESSOR_UNCORRECTABLE   (1<<1)
#define ACPI_EINJ_PROCESSOR_FATAL           (1<<2)
#define ACPI_EINJ_MEMORY_CORRECTABLE        (1<<3)
#define ACPI_EINJ_MEMORY_UNCORRECTABLE      (1<<4)
#define ACPI_EINJ_MEMORY_FATAL              (1<<5)
#define ACPI_EINJ_PCIX_CORRECTABLE          (1<<6)
#define ACPI_EINJ_PCIX_UNCORRECTABLE        (1<<7)
#define ACPI_EINJ_PCIX_FATAL                (1<<8)
#define ACPI_EINJ_PLATFORM_CORRECTABLE      (1<<9)
#define ACPI_EINJ_PLATFORM_UNCORRECTABLE    (1<<10)
#define ACPI_EINJ_PLATFORM_FATAL            (1<<11)
#define ACPI_EINJ_VENDOR_DEFINED            (1<<31)

/*******************************************************************************
 *
 * ERST - Error Record Serialization Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_erst {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 header_length;
	u32 reserved;
	u32 entries;
};

/* ERST Serialization Entries (actions) */

struct acpi_erst_entry {
	struct acpi_whea_header whea_header;	/* Common header for WHEA tables */
};

/* Values for Action field above */

enum acpi_erst_actions {
	ACPI_ERST_BEGIN_WRITE_OPERATION = 0,
	ACPI_ERST_BEGIN_READ_OPERATION = 1,
	ACPI_ERST_BETGIN_CLEAR_OPERATION = 2,
	ACPI_ERST_END_OPERATION = 3,
/* Masks for Flags field above */

#define ACPI_ERST_PRESERVE          (1)

/* Values for Action field above */

enum acpi_erst_actions {
	ACPI_ERST_BEGIN_WRITE = 0,
	ACPI_ERST_BEGIN_READ = 1,
	ACPI_ERST_BEGIN_CLEAR = 2,
	ACPI_ERST_END = 3,
	ACPI_ERST_SET_RECORD_OFFSET = 4,
	ACPI_ERST_EXECUTE_OPERATION = 5,
	ACPI_ERST_CHECK_BUSY_STATUS = 6,
	ACPI_ERST_GET_COMMAND_STATUS = 7,
	ACPI_ERST_GET_RECORD_IDENTIFIER = 8,
	ACPI_ERST_SET_RECORD_IDENTIFIER = 9,
	ACPI_ERST_GET_RECORD_ID = 8,
	ACPI_ERST_SET_RECORD_ID = 9,
	ACPI_ERST_GET_RECORD_COUNT = 10,
	ACPI_ERST_BEGIN_DUMMY_WRIITE = 11,
	ACPI_ERST_NOT_USED = 12,
	ACPI_ERST_GET_ERROR_RANGE = 13,
	ACPI_ERST_GET_ERROR_LENGTH = 14,
	ACPI_ERST_GET_ERROR_ATTRIBUTES = 15,
	ACPI_ERST_ACTION_RESERVED = 16	/* 16 and greater are reserved */
};

/* Values for Instruction field above */

enum acpi_erst_instructions {
	ACPI_ERST_READ_REGISTER = 0,
	ACPI_ERST_READ_REGISTER_VALUE = 1,
	ACPI_ERST_WRITE_REGISTER = 2,
	ACPI_ERST_WRITE_REGISTER_VALUE = 3,
	ACPI_ERST_NOOP = 4,
	ACPI_ERST_LOAD_VAR1 = 5,
	ACPI_ERST_LOAD_VAR2 = 6,
	ACPI_ERST_STORE_VAR1 = 7,
	ACPI_ERST_ADD = 8,
	ACPI_ERST_SUBTRACT = 9,
	ACPI_ERST_ADD_VALUE = 10,
	ACPI_ERST_SUBTRACT_VALUE = 11,
	ACPI_ERST_STALL = 12,
	ACPI_ERST_STALL_WHILE_TRUE = 13,
	ACPI_ERST_SKIP_NEXT_IF_TRUE = 14,
	ACPI_ERST_GOTO = 15,
	ACPI_ERST_SET_SRC_ADDRESS_BASE = 16,
	ACPI_ERST_SET_DST_ADDRESS_BASE = 17,
	ACPI_ERST_MOVE_DATA = 18,
	ACPI_ERST_INSTRUCTION_RESERVED = 19	/* 19 and greater are reserved */
};

/*******************************************************************************
 *
 * HEST - Hardware Error Source Table
/* Command status return values */

enum acpi_erst_command_status {
	ACPI_ERST_SUCESS = 0,
	ACPI_ERST_NO_SPACE = 1,
	ACPI_ERST_NOT_AVAILABLE = 2,
	ACPI_ERST_FAILURE = 3,
	ACPI_ERST_RECORD_EMPTY = 4,
	ACPI_ERST_NOT_FOUND = 5,
	ACPI_ERST_STATUS_RESERVED = 6	/* 6 and greater are reserved */
};

/* Error Record Serialization Information */

struct acpi_erst_info {
	u16 signature;		/* Should be "ER" */
	u8 data[48];
};

/*******************************************************************************
 *
 * HEST - Hardware Error Source Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_hest {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 error_source_count;
};

/* HEST subtable header */

struct acpi_hest_header {
	u16 type;
	u16 source_id;
};

/* Values for Type field above for subtables */

enum acpi_hest_types {
	ACPI_HEST_TYPE_XPF_MACHINE_CHECK = 0,
	ACPI_HEST_TYPE_XPF_CORRECTED_MACHINE_CHECK = 1,
	ACPI_HEST_TYPE_XPF_UNUSED = 2,
	ACPI_HEST_TYPE_XPF_NON_MASKABLE_INTERRUPT = 3,
	ACPI_HEST_TYPE_IPF_CORRECTED_MACHINE_CHECK = 4,
	ACPI_HEST_TYPE_IPF_CORRECTED_PLATFORM_ERROR = 5,
	ACPI_HEST_TYPE_AER_ROOT_PORT = 6,
	ACPI_HEST_TYPE_AER_ENDPOINT = 7,
	ACPI_HEST_TYPE_AER_BRIDGE = 8,
	ACPI_HEST_TYPE_GENERIC_HARDWARE_ERROR_SOURCE = 9,
	ACPI_HEST_TYPE_IA32_CHECK = 0,
	ACPI_HEST_TYPE_IA32_CORRECTED_CHECK = 1,
	ACPI_HEST_TYPE_IA32_NMI = 2,
	ACPI_HEST_TYPE_NOT_USED3 = 3,
	ACPI_HEST_TYPE_NOT_USED4 = 4,
	ACPI_HEST_TYPE_NOT_USED5 = 5,
	ACPI_HEST_TYPE_AER_ROOT_PORT = 6,
	ACPI_HEST_TYPE_AER_ENDPOINT = 7,
	ACPI_HEST_TYPE_AER_BRIDGE = 8,
	ACPI_HEST_TYPE_GENERIC_ERROR = 9,
	ACPI_HEST_TYPE_RESERVED = 10	/* 10 and greater are reserved */
};

/*
 * HEST Sub-subtables
 */

/* XPF Machine Check Error Bank */

struct acpi_hest_xpf_error_bank {
	u8 bank_number;
	u8 clear_status_on_init;
	u8 status_format;
	u8 config_write_enable;
	u32 control_register;
	u64 control_init_data;
 * HEST substructures contained in subtables
 */

/*
 * IA32 Error Bank(s) - Follows the struct acpi_hest_ia_machine_check and
 * struct acpi_hest_ia_corrected structures.
 */
struct acpi_hest_ia_error_bank {
	u8 bank_number;
	u8 clear_status_on_init;
	u8 status_format;
	u8 reserved;
	u32 control_register;
	u64 control_data;
	u32 status_register;
	u32 address_register;
	u32 misc_register;
};

/* Generic Error Status */

struct acpi_hest_generic_status {
	u32 block_status;
	u32 raw_data_offset;
	u32 raw_data_length;
	u32 data_length;
	u32 error_severity;
};

/* Generic Error Data */

struct acpi_hest_generic_data {
	u8 section_type[16];
	u32 error_severity;
	u16 revision;
	u8 validation_bits;
	u8 flags;
	u32 error_data_length;
	u8 fru_id[16];
	u8 fru_text[20];
};

/* Common HEST structure for PCI/AER types below (6,7,8) */

struct acpi_hest_aer_common {
	u16 source_id;
	u16 config_write_enable;
	u8 flags;
	u8 enabled;
	u32 records_to_pre_allocate;
	u32 max_sections_per_record;
	u32 bus;
	u16 device;
	u16 function;
	u16 device_control;
	u16 reserved;
	u32 uncorrectable_error_mask;
	u32 uncorrectable_error_severity;
	u32 correctable_error_mask;
	u32 advanced_error_cababilities;
};

/* Common HEST sub-structure for PCI/AER structures below (6,7,8) */

struct acpi_hest_aer_common {
	u16 reserved1;
	u8 flags;
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	u32 bus;		/* Bus and Segment numbers */
	u16 device;
	u16 function;
	u16 device_control;
	u16 reserved2;
	u32 uncorrectable_mask;
	u32 uncorrectable_severity;
	u32 correctable_mask;
	u32 advanced_capabilities;
};

/* Masks for HEST Flags fields */

#define ACPI_HEST_FIRMWARE_FIRST        (1)
#define ACPI_HEST_GLOBAL                (1<<1)

/*
 * Macros to access the bus/segment numbers in Bus field above:
 *  Bus number is encoded in bits 7:0
 *  Segment number is encoded in bits 23:8
 */
#define ACPI_HEST_BUS(bus)              ((bus) & 0xFF)
#define ACPI_HEST_SEGMENT(bus)          (((bus) >> 8) & 0xFFFF)

/* Hardware Error Notification */

struct acpi_hest_notify {
	u8 type;
	u8 length;
	u16 config_write_enable;
	u32 poll_interval;
	u32 vector;
	u32 polling_threshold_value;
	u32 polling_threshold_window;
	u32 error_threshold_value;
	u32 error_threshold_window;
};

/* Values for Notify Type field above */

enum acpi_hest_notify_types {
	ACPI_HEST_NOTIFY_POLLED = 0,
	ACPI_HEST_NOTIFY_EXTERNAL = 1,
	ACPI_HEST_NOTIFY_LOCAL = 2,
	ACPI_HEST_NOTIFY_SCI = 3,
	ACPI_HEST_NOTIFY_NMI = 4,
	ACPI_HEST_NOTIFY_RESERVED = 5	/* 5 and greater are reserved */
};

/*
 * HEST subtables
 *
 * From WHEA Design Document, 16 May 2007.
 * Note: There is no subtable type 2 in this version of the document,
 * and there are two different subtable type 3s.
 */

 /* 0: XPF Machine Check Exception */

struct acpi_hest_xpf_machine_check {
	struct acpi_hest_header header;
	u16 source_id;
	u16 config_write_enable;
	u8 flags;
	u8 reserved1;
	u32 records_to_pre_allocate;
	ACPI_HEST_NOTIFY_CMCI = 5,	/* ACPI 5.0 */
	ACPI_HEST_NOTIFY_MCE = 6,	/* ACPI 5.0 */
	ACPI_HEST_NOTIFY_RESERVED = 7	/* 7 and greater are reserved */
};

/* Values for config_write_enable bitfield above */

#define ACPI_HEST_TYPE                  (1)
#define ACPI_HEST_POLL_INTERVAL         (1<<1)
#define ACPI_HEST_POLL_THRESHOLD_VALUE  (1<<2)
#define ACPI_HEST_POLL_THRESHOLD_WINDOW (1<<3)
#define ACPI_HEST_ERR_THRESHOLD_VALUE   (1<<4)
#define ACPI_HEST_ERR_THRESHOLD_WINDOW  (1<<5)

/*
 * HEST subtables
 */

/* 0: IA32 Machine Check Exception */

struct acpi_hest_ia_machine_check {
	struct acpi_hest_header header;
	u16 reserved1;
	u8 flags;
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	u64 global_capability_data;
	u64 global_control_data;
	u8 num_hardware_banks;
	u8 reserved2[7];
};

/* 1: XPF Corrected Machine Check */

struct acpi_table_hest_xpf_corrected {
	struct acpi_hest_header header;
	u16 source_id;
	u16 config_write_enable;
	u8 flags;
	u8 enabled;
	u32 records_to_pre_allocate;
	u32 max_sections_per_record;
	struct acpi_hest_notify notify;
	u8 num_hardware_banks;
	u8 reserved[3];
};

/* 3: XPF Non-Maskable Interrupt */

struct acpi_hest_xpf_nmi {
	struct acpi_hest_header header;
	u16 source_id;
	u32 reserved;
	u32 records_to_pre_allocate;
	u8 reserved3[7];
};

/* 1: IA32 Corrected Machine Check */

struct acpi_hest_ia_corrected {
	struct acpi_hest_header header;
	u16 reserved1;
	u8 flags;
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	struct acpi_hest_notify notify;
	u8 num_hardware_banks;
	u8 reserved2[3];
};

/* 2: IA32 Non-Maskable Interrupt */

struct acpi_hest_ia_nmi {
	struct acpi_hest_header header;
	u32 reserved;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	u32 max_raw_data_length;
};

/* 4: IPF Corrected Machine Check */

struct acpi_hest_ipf_corrected {
	struct acpi_hest_header header;
	u8 enabled;
	u8 reserved;
};

/* 5: IPF Corrected Platform Error */

struct acpi_hest_ipf_corrected_platform {
	struct acpi_hest_header header;
	u8 enabled;
	u8 reserved;
};
/* 3,4,5: Not used */

/* 6: PCI Express Root Port AER */

struct acpi_hest_aer_root {
	struct acpi_hest_header header;
	struct acpi_hest_aer_common aer;
	u32 root_error_command;
};

/* 7: PCI Express AER (AER Endpoint) */

struct acpi_hest_aer {
	struct acpi_hest_header header;
	struct acpi_hest_aer_common aer;
};

/* 8: PCI Express/PCI-X Bridge AER */

struct acpi_hest_aer_bridge {
	struct acpi_hest_header header;
	struct acpi_hest_aer_common aer;
	u32 secondary_uncorrectable_error_mask;
	u32 secondary_uncorrectable_error_severity;
	u32 secondary_advanced_capabilities;
	u32 uncorrectable_mask2;
	u32 uncorrectable_severity2;
	u32 advanced_capabilities2;
};

/* 9: Generic Hardware Error Source */

struct acpi_hest_generic {
	struct acpi_hest_header header;
	u16 source_id;
	u16 related_source_id;
	u8 config_write_enable;
	u8 enabled;
	u32 records_to_pre_allocate;
	u16 related_source_id;
	u8 reserved;
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	u32 max_raw_data_length;
	struct acpi_generic_address error_status_address;
	struct acpi_hest_notify notify;
	u32 error_status_block_length;
};

/*******************************************************************************
 *
 * HPET - High Precision Event Timer table
 *
 ******************************************************************************/

struct acpi_table_hpet {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 id;			/* Hardware ID of event timer block */
	struct acpi_generic_address address;	/* Address of event timer block */
	u8 sequence;		/* HPET sequence number */
	u16 minimum_tick;	/* Main counter min tick, periodic mode */
	u8 flags;
};

/*! Flags */

#define ACPI_HPET_PAGE_PROTECT      (1)	/* 00: No page protection */
#define ACPI_HPET_PAGE_PROTECT_4    (1<<1)	/* 01: 4KB page protected */
#define ACPI_HPET_PAGE_PROTECT_64   (1<<2)	/* 02: 64KB page protected */

/*! [End] no source code translation !*/

/*******************************************************************************
 *
 * IBFT - Boot Firmware Table
 *
 ******************************************************************************/

struct acpi_table_ibft {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 reserved[12];
};

/* IBFT common subtable header */

struct acpi_ibft_header {
	u8 type;
	u8 version;
	u16 length;
	u8 index;
	u8 flags;
};

/* Values for Type field above */

enum acpi_ibft_type {
	ACPI_IBFT_TYPE_NOT_USED = 0,
	ACPI_IBFT_TYPE_CONTROL = 1,
	ACPI_IBFT_TYPE_INITIATOR = 2,
	ACPI_IBFT_TYPE_NIC = 3,
	ACPI_IBFT_TYPE_TARGET = 4,
	ACPI_IBFT_TYPE_EXTENSIONS = 5,
	ACPI_IBFT_TYPE_RESERVED = 6	/* 6 and greater are reserved */
};

/* IBFT subtables */

struct acpi_ibft_control {
	struct acpi_ibft_header header;
	u16 extensions;
	u16 initiator_offset;
	u16 nic0_offset;
	u16 target0_offset;
	u16 nic1_offset;
	u16 target1_offset;
};

struct acpi_ibft_initiator {
	struct acpi_ibft_header header;
	u8 sns_server[16];
	u8 slp_server[16];
	u8 primary_server[16];
	u8 secondary_server[16];
	u16 name_length;
	u16 name_offset;
};

struct acpi_ibft_nic {
	struct acpi_ibft_header header;
	u8 ip_address[16];
	u8 subnet_mask_prefix;
	u8 origin;
	u8 gateway[16];
	u8 primary_dns[16];
	u8 secondary_dns[16];
	u8 dhcp[16];
	u16 vlan;
	u8 mac_address[6];
	u16 pci_address;
	u16 name_length;
	u16 name_offset;
};

struct acpi_ibft_target {
	struct acpi_ibft_header header;
	u8 target_ip_address[16];
	u16 target_ip_socket;
	u8 target_boot_lun[8];
	u8 chap_type;
	u8 nic_association;
	u16 target_name_length;
	u16 target_name_offset;
	u16 chap_name_length;
	u16 chap_name_offset;
	u16 chap_secret_length;
	u16 chap_secret_offset;
	u16 reverse_chap_name_length;
	u16 reverse_chap_name_offset;
	u16 reverse_chap_secret_length;
	u16 reverse_chap_secret_offset;
	u32 error_block_length;
};

/* Generic Error Status block */

struct acpi_hest_generic_status {
	u32 block_status;
	u32 raw_data_offset;
	u32 raw_data_length;
	u32 data_length;
	u32 error_severity;
};

/* Values for block_status flags above */

#define ACPI_HEST_UNCORRECTABLE             (1)
#define ACPI_HEST_CORRECTABLE               (1<<1)
#define ACPI_HEST_MULTIPLE_UNCORRECTABLE    (1<<2)
#define ACPI_HEST_MULTIPLE_CORRECTABLE      (1<<3)
#define ACPI_HEST_ERROR_ENTRY_COUNT         (0xFF<<4)	/* 8 bits, error count */

/* Generic Error Data entry */

struct acpi_hest_generic_data {
	u8 section_type[16];
	u32 error_severity;
	u16 revision;
	u8 validation_bits;
	u8 flags;
	u32 error_data_length;
	u8 fru_id[16];
	u8 fru_text[20];
};

/*******************************************************************************
 *
 * MADT - Multiple APIC Description Table
 *        Version 3
 *
 ******************************************************************************/

struct acpi_table_madt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 address;		/* Physical address of local APIC */
	u32 flags;
};

/* Flags */

#define ACPI_MADT_PCAT_COMPAT       (1)	/* 00:    System also has dual 8259s */
/* Masks for Flags field above */

#define ACPI_MADT_PCAT_COMPAT       (1)	/* 00: System also has dual 8259s */

/* Values for PCATCompat flag */

#define ACPI_MADT_DUAL_PIC          0
#define ACPI_MADT_MULTIPLE_APIC     1

/* Values for subtable type in struct acpi_subtable_header */
/* Values for MADT subtable type in struct acpi_subtable_header */

enum acpi_madt_type {
	ACPI_MADT_TYPE_LOCAL_APIC = 0,
	ACPI_MADT_TYPE_IO_APIC = 1,
	ACPI_MADT_TYPE_INTERRUPT_OVERRIDE = 2,
	ACPI_MADT_TYPE_NMI_SOURCE = 3,
	ACPI_MADT_TYPE_LOCAL_APIC_NMI = 4,
	ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE = 5,
	ACPI_MADT_TYPE_IO_SAPIC = 6,
	ACPI_MADT_TYPE_LOCAL_SAPIC = 7,
	ACPI_MADT_TYPE_INTERRUPT_SOURCE = 8,
	ACPI_MADT_TYPE_RESERVED = 9	/* 9 and greater are reserved */
};

/*
 * MADT Sub-tables, correspond to Type in struct acpi_subtable_header
	ACPI_MADT_TYPE_LOCAL_X2APIC = 9,
	ACPI_MADT_TYPE_LOCAL_X2APIC_NMI = 10,
	ACPI_MADT_TYPE_GENERIC_INTERRUPT = 11,
	ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR = 12,
	ACPI_MADT_TYPE_GENERIC_MSI_FRAME = 13,
	ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR = 14,
	ACPI_MADT_TYPE_GENERIC_TRANSLATOR = 15,
	ACPI_MADT_TYPE_RESERVED = 16	/* 16 and greater are reserved */
};

/*
 * MADT Subtables, correspond to Type in struct acpi_subtable_header
 */

/* 0: Processor Local APIC */

struct acpi_madt_local_apic {
	struct acpi_subtable_header header;
	u8 processor_id;	/* ACPI processor id */
	u8 id;			/* Processor's local APIC id */
	u32 lapic_flags;
};

/* 1: IO APIC */

struct acpi_madt_io_apic {
	struct acpi_subtable_header header;
	u8 id;			/* I/O APIC ID */
	u8 reserved;		/* Reserved - must be zero */
	u8 reserved;		/* reserved - must be zero */
	u32 address;		/* APIC physical address */
	u32 global_irq_base;	/* Global system interrupt where INTI lines start */
};

/* 2: Interrupt Override */

struct acpi_madt_interrupt_override {
	struct acpi_subtable_header header;
	u8 bus;			/* 0 - ISA */
	u8 source_irq;		/* Interrupt source (IRQ) */
	u32 global_irq;		/* Global system interrupt */
	u16 inti_flags;
};

/* 3: NMI Source */

struct acpi_madt_nmi_source {
	struct acpi_subtable_header header;
	u16 inti_flags;
	u32 global_irq;		/* Global system interrupt */
};

/* 4: Local APIC NMI */

struct acpi_madt_local_apic_nmi {
	struct acpi_subtable_header header;
	u8 processor_id;	/* ACPI processor id */
	u16 inti_flags;
	u8 lint;		/* LINTn to which NMI is connected */
};

/* 5: Address Override */

struct acpi_madt_local_apic_override {
	struct acpi_subtable_header header;
	u16 reserved;		/* Reserved, must be zero */
	u64 address;		/* APIC physical address */
};

/* 6: I/O Sapic */

struct acpi_madt_io_sapic {
	struct acpi_subtable_header header;
	u8 id;			/* I/O SAPIC ID */
	u8 reserved;		/* Reserved, must be zero */
	u32 global_irq_base;	/* Global interrupt for SAPIC start */
	u64 address;		/* SAPIC physical address */
};

/* 7: Local Sapic */

struct acpi_madt_local_sapic {
	struct acpi_subtable_header header;
	u8 processor_id;	/* ACPI processor id */
	u8 id;			/* SAPIC ID */
	u8 eid;			/* SAPIC EID */
	u8 reserved[3];		/* Reserved, must be zero */
	u32 lapic_flags;
	u32 uid;		/* Numeric UID - ACPI 3.0 */
	char uid_string[1];	/* String UID  - ACPI 3.0 */
};

/* 8: Platform Interrupt Source */

struct acpi_madt_interrupt_source {
	struct acpi_subtable_header header;
	u16 inti_flags;
	u8 type;		/* 1=PMI, 2=INIT, 3=corrected */
	u8 id;			/* Processor ID */
	u8 eid;			/* Processor EID */
	u8 io_sapic_vector;	/* Vector value for PMI interrupts */
	u32 global_irq;		/* Global system interrupt */
	u32 flags;		/* Interrupt Source Flags */
};

/* Flags field above */

#define ACPI_MADT_CPEI_OVERRIDE     (1)

/* Masks for Flags field above */

#define ACPI_MADT_CPEI_OVERRIDE     (1)

/* 9: Processor Local X2APIC (ACPI 4.0) */

struct acpi_madt_local_x2apic {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u32 local_apic_id;	/* Processor x2APIC ID  */
	u32 lapic_flags;
	u32 uid;		/* ACPI processor UID */
};

/* 10: Local X2APIC NMI (ACPI 4.0) */

struct acpi_madt_local_x2apic_nmi {
	struct acpi_subtable_header header;
	u16 inti_flags;
	u32 uid;		/* ACPI processor UID */
	u8 lint;		/* LINTn to which NMI is connected */
	u8 reserved[3];		/* reserved - must be zero */
};

/* 11: Generic Interrupt (ACPI 5.0 + ACPI 6.0 changes) */

struct acpi_madt_generic_interrupt {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u32 cpu_interface_number;
	u32 uid;
	u32 flags;
	u32 parking_version;
	u32 performance_interrupt;
	u64 parked_address;
	u64 base_address;
	u64 gicv_base_address;
	u64 gich_base_address;
	u32 vgic_interrupt;
	u64 gicr_base_address;
	u64 arm_mpidr;
	u8 efficiency_class;
	u8 reserved2[3];
};

/* Masks for Flags field above */

/* ACPI_MADT_ENABLED                    (1)      Processor is usable if set */
#define ACPI_MADT_PERFORMANCE_IRQ_MODE  (1<<1)	/* 01: Performance Interrupt Mode */
#define ACPI_MADT_VGIC_IRQ_MODE         (1<<2)	/* 02: VGIC Maintenance Interrupt mode */

/* 12: Generic Distributor (ACPI 5.0 + ACPI 6.0 changes) */

struct acpi_madt_generic_distributor {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u32 gic_id;
	u64 base_address;
	u32 global_irq_base;
	u8 version;
	u8 reserved2[3];	/* reserved - must be zero */
};

/* Values for Version field above */

enum acpi_madt_gic_version {
	ACPI_MADT_GIC_VERSION_NONE = 0,
	ACPI_MADT_GIC_VERSION_V1 = 1,
	ACPI_MADT_GIC_VERSION_V2 = 2,
	ACPI_MADT_GIC_VERSION_V3 = 3,
	ACPI_MADT_GIC_VERSION_V4 = 4,
	ACPI_MADT_GIC_VERSION_RESERVED = 5	/* 5 and greater are reserved */
};

/* 13: Generic MSI Frame (ACPI 5.1) */

struct acpi_madt_generic_msi_frame {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u32 msi_frame_id;
	u64 base_address;
	u32 flags;
	u16 spi_count;
	u16 spi_base;
};

/* Masks for Flags field above */

#define ACPI_MADT_OVERRIDE_SPI_VALUES   (1)

/* 14: Generic Redistributor (ACPI 5.1) */

struct acpi_madt_generic_redistributor {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u64 base_address;
	u32 length;
};

/* 15: Generic Translator (ACPI 6.0) */

struct acpi_madt_generic_translator {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u32 translation_id;
	u64 base_address;
	u32 reserved2;
};

/*
 * Common flags fields for MADT subtables
 */

/* MADT Local APIC flags (lapic_flags) */
/* MADT Local APIC flags */

#define ACPI_MADT_ENABLED           (1)	/* 00: Processor is usable if set */

/* MADT MPS INTI flags (inti_flags) */

#define ACPI_MADT_POLARITY_MASK     (3)	/* 00-01: Polarity of APIC I/O input signals */
#define ACPI_MADT_TRIGGER_MASK      (3<<2)	/* 02-03: Trigger mode of APIC input signals */

/* Values for MPS INTI flags */

#define ACPI_MADT_POLARITY_CONFORMS       0
#define ACPI_MADT_POLARITY_ACTIVE_HIGH    1
#define ACPI_MADT_POLARITY_RESERVED       2
#define ACPI_MADT_POLARITY_ACTIVE_LOW     3

#define ACPI_MADT_TRIGGER_CONFORMS        (0)
#define ACPI_MADT_TRIGGER_EDGE            (1<<2)
#define ACPI_MADT_TRIGGER_RESERVED        (2<<2)
#define ACPI_MADT_TRIGGER_LEVEL           (3<<2)

/*******************************************************************************
 *
 * MCFG - PCI Memory Mapped Configuration table and sub-table
 *
 ******************************************************************************/

struct acpi_table_mcfg {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 reserved[8];
};

/* Subtable */

struct acpi_mcfg_allocation {
	u64 address;		/* Base address, processor-relative */
	u16 pci_segment;	/* PCI segment group number */
	u8 start_bus_number;	/* Starting PCI Bus number */
	u8 end_bus_number;	/* Final PCI Bus number */
	u32 reserved;
 * MSCT - Maximum System Characteristics Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_msct {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 proximity_offset;	/* Location of proximity info struct(s) */
	u32 max_proximity_domains;	/* Max number of proximity domains */
	u32 max_clock_domains;	/* Max number of clock domains */
	u64 max_address;	/* Max physical address in system */
};

/* subtable - Maximum Proximity Domain Information. Version 1 */

struct acpi_msct_proximity {
	u8 revision;
	u8 length;
	u32 range_start;	/* Start of domain range */
	u32 range_end;		/* End of domain range */
	u32 processor_capacity;
	u64 memory_capacity;	/* In bytes */
};

/*******************************************************************************
 *
 * NFIT - NVDIMM Interface Table (ACPI 6.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_nfit {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 reserved;		/* Reserved, must be zero */
};

/* Subtable header for NFIT */

struct acpi_nfit_header {
	u16 type;
	u16 length;
};

/* Values for subtable type in struct acpi_nfit_header */

enum acpi_nfit_type {
	ACPI_NFIT_TYPE_SYSTEM_ADDRESS = 0,
	ACPI_NFIT_TYPE_MEMORY_MAP = 1,
	ACPI_NFIT_TYPE_INTERLEAVE = 2,
	ACPI_NFIT_TYPE_SMBIOS = 3,
	ACPI_NFIT_TYPE_CONTROL_REGION = 4,
	ACPI_NFIT_TYPE_DATA_REGION = 5,
	ACPI_NFIT_TYPE_FLUSH_ADDRESS = 6,
	ACPI_NFIT_TYPE_RESERVED = 7	/* 7 and greater are reserved */
};

/*
 * NFIT Subtables
 */

/* 0: System Physical Address Range Structure */

struct acpi_nfit_system_address {
	struct acpi_nfit_header header;
	u16 range_index;
	u16 flags;
	u32 reserved;		/* Reseved, must be zero */
	u32 proximity_domain;
	u8 range_guid[16];
	u64 address;
	u64 length;
	u64 memory_mapping;
};

/* Flags */

#define ACPI_NFIT_ADD_ONLINE_ONLY       (1)	/* 00: Add/Online Operation Only */
#define ACPI_NFIT_PROXIMITY_VALID       (1<<1)	/* 01: Proximity Domain Valid */

/* Range Type GUIDs appear in the include/acuuid.h file */

/* 1: Memory Device to System Address Range Map Structure */

struct acpi_nfit_memory_map {
	struct acpi_nfit_header header;
	u32 device_handle;
	u16 physical_id;
	u16 region_id;
	u16 range_index;
	u16 region_index;
	u64 region_size;
	u64 region_offset;
	u64 address;
	u16 interleave_index;
	u16 interleave_ways;
	u16 flags;
	u16 reserved;		/* Reserved, must be zero */
};

/* Flags */

#define ACPI_NFIT_MEM_SAVE_FAILED       (1)	/* 00: Last SAVE to Memory Device failed */
#define ACPI_NFIT_MEM_RESTORE_FAILED    (1<<1)	/* 01: Last RESTORE from Memory Device failed */
#define ACPI_NFIT_MEM_FLUSH_FAILED      (1<<2)	/* 02: Platform flush failed */
#define ACPI_NFIT_MEM_NOT_ARMED         (1<<3)	/* 03: Memory Device is not armed */
#define ACPI_NFIT_MEM_HEALTH_OBSERVED   (1<<4)	/* 04: Memory Device observed SMART/health events */
#define ACPI_NFIT_MEM_HEALTH_ENABLED    (1<<5)	/* 05: SMART/health events enabled */

/* 2: Interleave Structure */

struct acpi_nfit_interleave {
	struct acpi_nfit_header header;
	u16 interleave_index;
	u16 reserved;		/* Reserved, must be zero */
	u32 line_count;
	u32 line_size;
	u32 line_offset[1];	/* Variable length */
};

/* 3: SMBIOS Management Information Structure */

struct acpi_nfit_smbios {
	struct acpi_nfit_header header;
	u32 reserved;		/* Reserved, must be zero */
	u8 data[1];		/* Variable length */
};

/* 4: NVDIMM Control Region Structure */

struct acpi_nfit_control_region {
	struct acpi_nfit_header header;
	u16 region_index;
	u16 vendor_id;
	u16 device_id;
	u16 revision_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_revision_id;
	u8 reserved[6];		/* Reserved, must be zero */
	u32 serial_number;
	u16 code;
	u16 windows;
	u64 window_size;
	u64 command_offset;
	u64 command_size;
	u64 status_offset;
	u64 status_size;
	u16 flags;
	u8 reserved1[6];	/* Reserved, must be zero */
};

/* Flags */

#define ACPI_NFIT_CONTROL_BUFFERED      (1)	/* Block Data Windows implementation is buffered */

/* 5: NVDIMM Block Data Window Region Structure */

struct acpi_nfit_data_region {
	struct acpi_nfit_header header;
	u16 region_index;
	u16 windows;
	u64 offset;
	u64 size;
	u64 capacity;
	u64 start_address;
};

/* 6: Flush Hint Address Structure */

struct acpi_nfit_flush_address {
	struct acpi_nfit_header header;
	u32 device_handle;
	u16 hint_count;
	u8 reserved[6];		/* Reserved, must be zero */
	u64 hint_address[1];	/* Variable length */
};

/*******************************************************************************
 *
 * SBST - Smart Battery Specification Table
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_sbst {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 warning_level;
	u32 low_level;
	u32 critical_level;
};

/*******************************************************************************
 *
 * SLIT - System Locality Distance Information Table
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_slit {
	struct acpi_table_header header;	/* Common ACPI table header */
	u64 locality_count;
	u8 entry[1];		/* Real size = localities^2 */
};

/*******************************************************************************
 *
 * SPCR - Serial Port Console Redirection table
 *
 ******************************************************************************/

struct acpi_table_spcr {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 interface_type;	/* 0=full 16550, 1=subset of 16550 */
	u8 reserved[3];
	struct acpi_generic_address serial_port;
	u8 interrupt_type;
	u8 pc_interrupt;
	u32 interrupt;
	u8 baud_rate;
	u8 parity;
	u8 stop_bits;
	u8 flow_control;
	u8 terminal_type;
	u8 reserved1;
	u16 pci_device_id;
	u16 pci_vendor_id;
	u8 pci_bus;
	u8 pci_device;
	u8 pci_function;
	u32 pci_flags;
	u8 pci_segment;
	u32 reserved2;
};

/*******************************************************************************
 *
 * SPMI - Server Platform Management Interface table
 *
 ******************************************************************************/

struct acpi_table_spmi {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 reserved;
	u8 interface_type;
	u16 spec_revision;	/* Version of IPMI */
	u8 interrupt_type;
	u8 gpe_number;		/* GPE assigned */
	u8 reserved1;
	u8 pci_device_flag;
	u32 interrupt;
	struct acpi_generic_address ipmi_register;
	u8 pci_segment;
	u8 pci_bus;
	u8 pci_device;
	u8 pci_function;
};

/*******************************************************************************
 *
 * SRAT - System Resource Affinity Table
 * SRAT - System Resource Affinity Table
 *        Version 3
 *
 ******************************************************************************/

struct acpi_table_srat {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 table_revision;	/* Must be value '1' */
	u64 reserved;		/* Reserved, must be zero */
};

/* Values for subtable type in struct acpi_subtable_header */

enum acpi_srat_type {
	ACPI_SRAT_TYPE_CPU_AFFINITY = 0,
	ACPI_SRAT_TYPE_MEMORY_AFFINITY = 1,
	ACPI_SRAT_TYPE_RESERVED = 2
};

/* SRAT sub-tables */
	ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY = 2,
	ACPI_SRAT_TYPE_GICC_AFFINITY = 3,
	ACPI_SRAT_TYPE_RESERVED = 4	/* 4 and greater are reserved */
};

/*
 * SRAT Subtables, correspond to Type in struct acpi_subtable_header
 */

/* 0: Processor Local APIC/SAPIC Affinity */

struct acpi_srat_cpu_affinity {
	struct acpi_subtable_header header;
	u8 proximity_domain_lo;
	u8 apic_id;
	u32 flags;
	u8 local_sapic_eid;
	u8 proximity_domain_hi[3];
	u32 reserved;		/* Reserved, must be zero */
	u32 clock_domain;
};

/* Flags */

#define ACPI_SRAT_CPU_ENABLED       (1)	/* 00: Use affinity structure */
#define ACPI_SRAT_CPU_USE_AFFINITY  (1)	/* 00: Use affinity structure */

/* 1: Memory Affinity */

struct acpi_srat_mem_affinity {
	struct acpi_subtable_header header;
	u32 proximity_domain;
	u16 reserved;		/* Reserved, must be zero */
	u64 base_address;
	u64 length;
       u32 reserved1;
	u32 flags;
       u64 reserved2;	       /* Reserved, must be zero */
};

/* Flags */

#define ACPI_SRAT_MEM_ENABLED       (1)	/* 00: Use affinity structure */
#define ACPI_SRAT_MEM_HOT_PLUGGABLE (1<<1)	/* 01: Memory region is hot pluggable */
#define ACPI_SRAT_MEM_NON_VOLATILE  (1<<2)	/* 02: Memory region is non-volatile */

/*******************************************************************************
 *
 * TCPA - Trusted Computing Platform Alliance table
 *
 ******************************************************************************/

struct acpi_table_tcpa {
	struct acpi_table_header header;	/* Common ACPI table header */
	u16 reserved;
	u32 max_log_length;	/* Maximum length for the event log area */
	u64 log_address;	/* Address of the event log area */
};

/*******************************************************************************
 *
 * UEFI - UEFI Boot optimization Table
 *
 ******************************************************************************/

struct acpi_table_uefi {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 identifier[16];	/* UUID identifier */
	u16 data_offset;	/* Offset of remaining data in table */
	u8 data;
};

/*******************************************************************************
 *
 * WDAT - Watchdog Action Table
 *
 ******************************************************************************/

struct acpi_table_wdat {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 header_length;	/* Watchdog Header Length */
	u16 pci_segment;	/* PCI Segment number */
	u8 pci_bus;		/* PCI Bus number */
	u8 pci_device;		/* PCI Device number */
	u8 pci_function;	/* PCI Function number */
	u8 reserved[3];
	u32 timer_period;	/* Period of one timer count (msec) */
	u32 max_count;		/* Maximum counter value supported */
	u32 min_count;		/* Minimum counter value */
	u8 flags;
	u8 reserved2[3];
	u32 entries;		/* Number of watchdog entries that follow */
};

/* WDAT Instruction Entries (actions) */

struct acpi_wdat_entry {
	struct acpi_whea_header whea_header;	/* Common header for WHEA tables */
};

/* Values for Action field above */

enum acpi_wdat_actions {
	ACPI_WDAT_RESET = 1,
	ACPI_WDAT_GET_CURRENT_COUNTDOWN = 4,
	ACPI_WDAT_GET_COUNTDOWN = 5,
	ACPI_WDAT_SET_COUNTDOWN = 6,
	ACPI_WDAT_GET_RUNNING_STATE = 8,
	ACPI_WDAT_SET_RUNNING_STATE = 9,
	ACPI_WDAT_GET_STOPPED_STATE = 10,
	ACPI_WDAT_SET_STOPPED_STATE = 11,
	ACPI_WDAT_GET_REBOOT = 16,
	ACPI_WDAT_SET_REBOOT = 17,
	ACPI_WDAT_GET_SHUTDOWN = 18,
	ACPI_WDAT_SET_SHUTDOWN = 19,
	ACPI_WDAT_GET_STATUS = 32,
	ACPI_WDAT_SET_STATUS = 33,
	ACPI_WDAT_ACTION_RESERVED = 34	/* 34 and greater are reserved */
};

/* Values for Instruction field above */

enum acpi_wdat_instructions {
	ACPI_WDAT_READ_VALUE = 0,
	ACPI_WDAT_READ_COUNTDOWN = 1,
	ACPI_WDAT_WRITE_VALUE = 2,
	ACPI_WDAT_WRITE_COUNTDOWN = 3,
	ACPI_WDAT_INSTRUCTION_RESERVED = 4,	/* 4 and greater are reserved */
	ACPI_WDAT_PRESERVE_REGISTER = 0x80	/* Except for this value */
};

/*******************************************************************************
 *
 * WDRT - Watchdog Resource Table
 *
 ******************************************************************************/

struct acpi_table_wdrt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 header_length;	/* Watchdog Header Length */
	u8 pci_segment;		/* PCI Segment number */
	u8 pci_bus;		/* PCI Bus number */
	u8 pci_device;		/* PCI Device number */
	u8 pci_function;	/* PCI Function number */
	u32 timer_period;	/* Period of one timer count (msec) */
	u32 max_count;		/* Maximum counter value supported */
	u32 min_count;		/* Minimum counter value */
	u8 flags;
	u8 reserved[3];
	u32 entries;		/* Number of watchdog entries that follow */
};

/* Flags */

#define ACPI_WDRT_TIMER_ENABLED     (1)	/* 00: Timer enabled */
/* 2: Processor Local X2_APIC Affinity (ACPI 4.0) */

struct acpi_srat_x2apic_cpu_affinity {
	struct acpi_subtable_header header;
	u16 reserved;		/* Reserved, must be zero */
	u32 proximity_domain;
	u32 apic_id;
	u32 flags;
	u32 clock_domain;
	u32 reserved2;
};

/* Flags for struct acpi_srat_cpu_affinity and struct acpi_srat_x2apic_cpu_affinity */

#define ACPI_SRAT_CPU_ENABLED       (1)	/* 00: Use affinity structure */

/* 3: GICC Affinity (ACPI 5.1) */

struct acpi_srat_gicc_affinity {
	struct acpi_subtable_header header;
	u32 proximity_domain;
	u32 acpi_processor_uid;
	u32 flags;
	u32 clock_domain;
};

/* Flags for struct acpi_srat_gicc_affinity */

#define ACPI_SRAT_GICC_ENABLED     (1)	/* 00: Use affinity structure */

/* Reset to default packing */

#pragma pack()

#endif				/* __ACTBL1_H__ */
