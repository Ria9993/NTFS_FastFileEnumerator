#pragma once

#define WIN32_LEAN_AND_MEAN 
#include "windows.h"
#include <winDNS.h>

typedef struct _NTFS_BPB					NTFS_BPB;
typedef struct _NTFS_BootSector				NTFS_BootSector;
typedef struct _NTFS_FileRecordHeader				NTFS_FileRecordHeader;
typedef struct _NTFS_FileRecord_AttrHeader			NTFS_FileRecord_AttrHeader;
typedef struct _NTFS_FileRecord_Attr_Filename		NTFS_FileRecord_Attr_Filename;

enum eNTFS_FileRecord_Attr_Filename_Flags {
	FILENAME_FLAG_READONLY = 0x0001,
	FILENAME_FLAG_HIDDEN = 0x0002,
	FILENAME_FLAG_SYSTEM = 0x0004,
	FILENAME_FLAG_ARCHIVE = 0x0020,
	FILENAME_FLAG_DEVICE = 0x0040,
	FILENAME_FLAG_NORMAL = 0x0080,
	FILENAME_FLAG_TEMPORARY = 0x0100,
	FILENAME_FLAG_SPARSE_FILE = 0x0200,
	FILENAME_FLAG_REPARSE_POINT = 0x0400,
	FILENAME_FLAG_COMPRESSED = 0x0800,
	FILENAME_FLAG_OFFLINE = 0x1000,
	FILENAME_FLAG_NOT_INDEXED = 0x2000,
	FILENAME_FLAG_ENCRYPTED = 0x4000,
};

enum eNTFS_FileRecord_Attr_Filename_NameType {
	FILENAME_NAMETYPE_POSIX = 0,
	FILENAME_NAMETYPE_WIN32 = 1,
	FILENAME_NAMETYPE_DOS = 2,
	FILENAME_NAMETYPE_WIN32_7_DOS = 3
};

#pragma pack(push)
#pragma pack(1)
struct _NTFS_FileRecord_Attr_Filename {
	QWORD		   ParentDirectory;
	QWORD		   DateCreated;
	QWORD		   DateModified;
	QWORD		   DateMFTModified;
	QWORD		   DateAccessed;
	QWORD		   LogicalFileSize;
	QWORD		   SizeOnDisk;
	DWORD		   Flags; //< See eNTFS_FileRecord_Attr_Filename_Flags
	DWORD		   ReparseValue;
	UCHAR          FileNameLength;
	UCHAR          FileNameType; //< See eNTFS_FileRecord_Attr_Filename_NameType
	WCHAR          FileName[1]; //< Maybe [sizeof(FileNameType) * FileNameLength]
};
#pragma pack(pop)

enum eNTFS_FileRecord_AttrHeader_Flags {
	ATTRIBUTE_FLAG_COMPRESSED = 0x0001,
	ATTRIBUTE_FLAG_COMPRESSION_MASK = 0x00FF,
	ATTRIBUTE_FLAG_ENCRYPTED = 0x4000,
	ATTRIBUTE_FLAG_SPARSE = 0x8000,
};

enum eNTFS_FileRecord_AttrHeader_TypeCode {
	$STANDARD_INFORMATION = 0x10,
	$ATTRIBUTE_LIST = 0x20,
	$FILE_NAME = 0x30,
	$OBJECT_ID = 0x40,
	$VOLUME_NAME = 0x60,
	$VOLUME_INFORMATION = 0x70,
	$DATA = 0x80,
	$INDEX_ROOT = 0x90,
	$INDEX_ALLOCATION = 0xA0,
	$BITMAP = 0xB0,
	$REPARSE_POINT = 0xC0,
	$TYPECODE_END = 0xFFFF
};

enum eNTFS_FileRecord_AttrHeader_FormCode {
	RESIDENT_FORM = 0x00,
	NONRESIDENT_FORM = 0x01
};

#pragma pack(push)
#pragma pack(1)
struct _NTFS_FileRecord_AttrHeader {
	ULONG	TypeCode; //< See eNTFS_FileRecord_AttrHeader_TypeCode
	ULONG   RecordLength;
	UCHAR   FormCode; //< See eNTFS_FileRecord_AttrHeader_FormCode
	UCHAR   NameLength;
	USHORT  NameOffset;
	USHORT  Flags; //< See eNTFS_FileRecord_AttrHeader_Flags
	USHORT  Instance;
	union {
		struct {
			ULONG  ValueLength;
			USHORT ValueOffset;
			UCHAR  Reserved[2];
		} Resident;
		struct {
			ULONGLONG	LowestVcn;
			ULONGLONG   HighestVcn;
			USHORT		MappingPairsOffset;
			USHORT		CompressionUnitSize;
			UCHAR		Reserved[4];
			LONGLONG	AllocatedLength;
			LONGLONG	FileSize;
			LONGLONG	ValidDataLength;
			LONGLONG	TotalAllocated;
		} Nonresident;
	} Form;
};
#pragma pack(pop)

/** _NTFS_FileRecordHeader::Flags */
enum eNTFS_FileRecordHeader_flags {
	FILE_RECORD_SEGMENT_IN_USE = 0x0001,
	MFT_RECORD_IN_USE = 0x0001,
	FILE_NAME_INDEX_PRESENT = 0x0002,
	MFT_RECORD_IS_DIRECTORY = 0x0002,
	MFT_RECORD_IN_EXTEND = 0x0004,
	MFT_RECORD_IS_VIEW_INDEX = 0x0008
};

#pragma pack(push)
#pragma pack(1)
struct _NTFS_FileRecordHeader {
	UCHAR			Signature[4]; //< "FILE" or "BAAD"
	USHORT			UpdateSequenceArrayOffset;
	USHORT			UpdateSequenceArraySize;
	ULONGLONG		Reserved1;
	USHORT			SequenceNumber;
	USHORT			Reserved2;
	USHORT			FirstAttributeOffset;
	USHORT			Flags; //< See eNTFS_FileRecordHeader_flags
	ULONG			BytesUsedFileRecord;
	ULONG			BytesPerFileRecord;
	ULONGLONG		BaseFileRecordSegment;
	USHORT			Reserved4;
	USHORT			Reserved5;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack(1)
/** Include BPB & extended BPB */
struct _NTFS_BPB {
	WORD	BytesPerSector;
	BYTE	SectorsPerClustor;
	WORD	ReservedSectors;
	BYTE	FailedChecker1[3];
	BYTE	FailedChecker2[2];
	BYTE	MediaDescriptor;
	BYTE	FailedChecker3[2];
	BYTE	Reserved1[2];
	BYTE	Reserved2[2];
	BYTE	Reserved3[4];
	BYTE	FailedChecker4[4];
	BYTE	Reserved4[4];
	QWORD	TotalSectors;
	QWORD	LCN_$MFT;
	QWORD	LCN_$MFTMirr;
	BYTE	ClustersPerMftRecord;
	BYTE	Reserved5[3];
	BYTE	ClustersPerIndexBuffer;
	BYTE	Reserved6[3];
	QWORD	VolumeSerialNumber;
	BYTE	Reserved7[4];
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack(1)
struct _NTFS_BootSector {
	BYTE		jumpInst[3];
	BYTE		OEM[8];
	NTFS_BPB	BPB;
	BYTE		BootStrap[426];
	BYTE		EndMaker[2];
};
#pragma pack(pop)
