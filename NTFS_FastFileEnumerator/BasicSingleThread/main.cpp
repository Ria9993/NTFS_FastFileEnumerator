#include <iostream>
#include <cassert>
#include <memory>
#include <string>
#include <locale>
#include <codecvt>
#include <fstream>
#include <intrin.h>
#include <io.h>
#include <fcntl.h>

#include "NTFS_Structure.h"

[[noreturn]] void ExitErrorWinApi(std::string msg);

LONGLONG ExtractLowerBytesSigned(LONGLONG val, BYTE nBytesExtract);

DWORD WINAPI IterateFileRecords(LPVOID lpParm);


int wmain(int argc, wchar_t** argv)
{
    const std::locale utf8_locale = std::locale(std::locale(), new std::codecvt_utf8<wchar_t>());
    std::wofstream f(L"output.list");
    f.imbue(utf8_locale);


    WORD  gBytesPerSector = 0;
    BYTE  gSectorsPerCluster = 0;
    ULONG gBytesPerFileRecord = 1024; // Typically NTFS's MFT size is 1KB.


    // Open logical disk
    // TODO: Test performance of FILE_FLAG_OVERLAPPED, FILE_FLAG_NO_BUFFERING
    WCHAR diskPath[] = L"\\\\.\\C:";
    const HANDLE hDrive = CreateFileW(diskPath, GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDrive == INVALID_HANDLE_VALUE) {
        ExitErrorWinApi("Need administrator permission.");
    }
    std::wcout << "Success to open logical disk. HANDLE: " << hDrive << std::endl;


    // Read NTFS boot sector
    NTFS_BootSector bootSector{};
    {
        DWORD nBytesRead;
        if (!ReadFile(hDrive, &bootSector, sizeof(NTFS_BootSector), &nBytesRead, NULL)) {
            ExitErrorWinApi("Failed to read boot sector.");
        }
    }
    gBytesPerSector = bootSector.BPB.BytesPerSector;
    gSectorsPerCluster = bootSector.BPB.SectorsPerClustor;


    // Read $MFT's file record header to get entry size
    const QWORD  loc_$MFT_FileRecord = (QWORD)bootSector.BPB.LCN_$MFT * gBytesPerSector * gSectorsPerCluster;
    const LONG   loc_$MFT_FileRecord_high = (loc_$MFT_FileRecord >> 32) & 0xFFFFFFFF;
    const LONG   loc_$MFT_FileRecord_low = loc_$MFT_FileRecord & 0xFFFFFFFF;

    // Read $MFT's file record content
    std::unique_ptr<char[]> $MFT_FileRecord_Raw = std::make_unique<char[]>(gBytesPerFileRecord);
    {
        OVERLAPPED hOverlapped;
        ZeroMemory(&hOverlapped, sizeof(hOverlapped));
        hOverlapped.Offset = (DWORD)(loc_$MFT_FileRecord & 0xFFFFFFFF);
        hOverlapped.OffsetHigh = (DWORD)((loc_$MFT_FileRecord >> 32) & 0xFFFFFFFF);
        DWORD nBytesRead;
        if (!ReadFile(hDrive, $MFT_FileRecord_Raw.get(), gBytesPerFileRecord, &nBytesRead, &hOverlapped)) {
            ExitErrorWinApi("Failed to read $MFT's MFT entry.");
        }
    }
    NTFS_FileRecordHeader* $MFT_FileRecord_Header = (NTFS_FileRecordHeader*)$MFT_FileRecord_Raw.get();
    assert($MFT_FileRecord_Header->BytesPerFileRecord == gBytesPerFileRecord);
    assert($MFT_FileRecord_Header->Flags == eNTFS_FileRecordHeader_flags::FILE_RECORD_SEGMENT_IN_USE);


    // Find $DATA attribute from $MFT Attribute chain
    NTFS_FileRecord_AttrHeader* $MFT_FileRecord_DataAttr = NULL;
    {
        NTFS_FileRecord_AttrHeader* loc_$MFT_FileRecord_FirstAttribute = (NTFS_FileRecord_AttrHeader*)((SIZE_T)$MFT_FileRecord_Header + $MFT_FileRecord_Header->FirstAttributeOffset);
        NTFS_FileRecord_AttrHeader* loc_$MFT_FileRecord_end = (NTFS_FileRecord_AttrHeader*)((SIZE_T)$MFT_FileRecord_Header + $MFT_FileRecord_Header->BytesUsedFileRecord);
        NTFS_FileRecord_AttrHeader* pAttr = loc_$MFT_FileRecord_FirstAttribute;
        while ((LPBYTE)pAttr <= (LPBYTE)loc_$MFT_FileRecord_end - sizeof(NTFS_FileRecord_AttrHeader))
        {
            if (pAttr->TypeCode == eNTFS_FileRecord_AttrHeader_TypeCode::$DATA) {
                assert($MFT_FileRecord_DataAttr == NULL); //< No handling of multiple DATA attribute.
                $MFT_FileRecord_DataAttr = pAttr;
            }

            // Advance to next attribute
            pAttr = (NTFS_FileRecord_AttrHeader*)((SIZE_T)pAttr + pAttr->RecordLength);
        }
    }
    assert($MFT_FileRecord_DataAttr);


    // Read content of $DATA attribute
    assert($MFT_FileRecord_DataAttr->Flags == 0 || $MFT_FileRecord_DataAttr->Flags == ATTRIBUTE_FLAG_SPARSE);
    if ($MFT_FileRecord_DataAttr->FormCode == eNTFS_FileRecord_AttrHeader_FormCode::RESIDENT_FORM)
    {
        // TODO: 
        assert(0);

        LPBYTE loc_$MFT_FileRecord_DataAttr_Content = (LPBYTE)$MFT_FileRecord_DataAttr + $MFT_FileRecord_DataAttr->Form.Resident.ValueOffset;

    }
    else if ($MFT_FileRecord_DataAttr->FormCode == eNTFS_FileRecord_AttrHeader_FormCode::NONRESIDENT_FORM)
    {
        assert($MFT_FileRecord_DataAttr->Form.Nonresident.CompressionUnitSize == 0); //< Only handle the uncompressed data.

        // Search runlists(mapping pairs)
        QWORD  currentLCN = 0;
        LPBYTE loc_$MFT_FileRecord_DataAttr_Runlists = (LPBYTE)$MFT_FileRecord_DataAttr + $MFT_FileRecord_DataAttr->Form.Nonresident.MappingPairsOffset;
        LPBYTE loc_$MFT_FileRecord_DataAttr_End = (LPBYTE)$MFT_FileRecord_DataAttr + $MFT_FileRecord_DataAttr->RecordLength;
        LPBYTE pRunlist = loc_$MFT_FileRecord_DataAttr_Runlists;
        while (pRunlist < loc_$MFT_FileRecord_DataAttr_End)
        {
            // Read run header
            const BYTE runHeader = *((BYTE*)pRunlist);
            const BYTE nBytesRunLength = runHeader & 0xF;
            const BYTE nBytesRunOffset = (runHeader >> 4) & 0xF;
            pRunlist += 1;
            if (runHeader == 0) {
                continue;
            }
            const LONGLONG runLength = ExtractLowerBytesSigned(*((LONGLONG*)(pRunlist)), nBytesRunLength);
            const LONGLONG runOffset = ExtractLowerBytesSigned(*((LONGLONG*)(pRunlist + nBytesRunLength)), nBytesRunOffset); //< Can be negative
            assert(runLength >= 0); //< Idk it can be negative
            pRunlist += (SIZE_T)nBytesRunLength + nBytesRunOffset;

            // Read file records from run content clusters
            currentLCN += runOffset;
            
            {
                const QWORD loc_currentLogicalCluster = currentLCN * gBytesPerSector * gSectorsPerCluster;
                const QWORD nBytesRunContent = runLength * gBytesPerSector * gSectorsPerCluster;
                assert(loc_currentLogicalCluster >= 0);
                assert(nBytesRunContent >= 0);

                // ReadFile allows only DWORD range.
                assert(nBytesRunContent <= UINT_MAX);

                std::unique_ptr<BYTE[]> runContent = std::make_unique<BYTE[]>(nBytesRunContent);

                OVERLAPPED hOverlapped;
                ZeroMemory(&hOverlapped, sizeof(hOverlapped));
                hOverlapped.Offset = (DWORD)(loc_currentLogicalCluster & 0xFFFFFFFF);
                hOverlapped.OffsetHigh = (DWORD)((loc_currentLogicalCluster >> 32) & 0xFFFFFFFF);
                DWORD nBytesRead;
                if (!ReadFile(hDrive, runContent.get(), nBytesRunContent, &nBytesRead, &hOverlapped)) {
                    ExitErrorWinApi("Failed to read $MFT's MFT data run list.");
                }


                // Iterate file records
                LPBYTE pCurrentRecord = (LPBYTE)runContent.get();
                LPBYTE loc_RunContent_End = (LPBYTE)runContent.get() + nBytesRunContent;
                for (; pCurrentRecord < loc_RunContent_End; pCurrentRecord += gBytesPerFileRecord)
                {
                    NTFS_FileRecordHeader* currentRecord_Header = (NTFS_FileRecordHeader*)pCurrentRecord;
                    if (strncmp((LPSTR)currentRecord_Header->Signature, "FILE", sizeof(currentRecord_Header->Signature)) != 0) {
                        if (strncmp((LPSTR)currentRecord_Header->Signature, "BAAD", sizeof(currentRecord_Header->Signature)) != 0) {
                            std::wcout << "[LOG] Detected BAAD MFT entry." << std::endl;
                            continue;
                        }
                        break;
                    }
                    assert(currentRecord_Header->BytesPerFileRecord == gBytesPerFileRecord);

                    // Find $FILE_NAME attribute from attribute chain
                    NTFS_FileRecord_AttrHeader* loc_FirstAttribute = (NTFS_FileRecord_AttrHeader*)(pCurrentRecord + currentRecord_Header->FirstAttributeOffset);
                    NTFS_FileRecord_AttrHeader* loc_Entry_End = (NTFS_FileRecord_AttrHeader*)(pCurrentRecord + currentRecord_Header->BytesUsedFileRecord);
                    NTFS_FileRecord_AttrHeader* pAttr = loc_FirstAttribute;
                    NTFS_FileRecord_AttrHeader* pFileNameAttr = NULL;
                    while ((LPBYTE)pAttr <= (LPBYTE)loc_Entry_End - sizeof(NTFS_FileRecord_AttrHeader))
                    {
                        if (pAttr->TypeCode == eNTFS_FileRecord_AttrHeader_TypeCode::$FILE_NAME) {
                            pFileNameAttr = pAttr;
                        }

                        // Advance to next attribute
                        pAttr = (NTFS_FileRecord_AttrHeader*)((SIZE_T)pAttr + pAttr->RecordLength);
                    }

                    // Read $FILE_NAME content
                    if (pFileNameAttr == NULL) {
                        //std::wcout << "[No Name]" << std::endl;
                        continue;
                    }

                    // Print file name 
                    assert(pFileNameAttr->FormCode == eNTFS_FileRecord_AttrHeader_FormCode::RESIDENT_FORM);
                    NTFS_FileRecord_Attr_Filename* attrFileName = (NTFS_FileRecord_Attr_Filename*)((LPBYTE)pFileNameAttr + pFileNameAttr->Form.Resident.ValueOffset);
                    switch (attrFileName->FileNameType) {
                    case FILENAME_NAMETYPE_WIN32_7_DOS: {
                        f << attrFileName->FileName << std::endl;
                        break;
                    }
                    case FILENAME_NAMETYPE_DOS: {
                        f << attrFileName->FileName << std::endl;
                        break;
                    }
                    case FILENAME_NAMETYPE_POSIX: {
                        // TODO: Need to handle posix unicode encoding. For now, it seems weird.
                        // wprintf(L"%ls\n", attrFileName->FileName);
                        f << attrFileName->FileName << std::endl;
                        break;
                    }
                    case FILENAME_NAMETYPE_WIN32: {
                        f << attrFileName->FileName << std::endl;
                        break;
                    }
                    default: {
                        __assume(0);
                    }
                    }
                }
            }

            static int countRunList = 0;
            std::wcout << "[LOG]runlist : " << countRunList++ << std::endl;
        }
    }


    $MFT_FileRecord_Raw.reset();
    std::wcout << "[LOG]Reached to end of main." << std::endl;

    return 0;
}

void ExitErrorWinApi(std::string msg)
{
    const DWORD lastError = GetLastError();
    std::wcout << msg.c_str() << ": Code[" << lastError << "]" << std::endl;

    char* szMessageBuffer = NULL;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER
        | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&szMessageBuffer, 0, NULL);
    std::wcout << szMessageBuffer << std::endl;

    exit(1);
}
LONGLONG ExtractLowerBytesSigned(LONGLONG val, BYTE nBytesExtract)
{
    assert(nBytesExtract <= 8);

    const DWORD bitsExtractIndex = nBytesExtract * 8;
    const QWORD sign = (val & ((QWORD)1 << (bitsExtractIndex - 1))) << (63 - (bitsExtractIndex - 1));
    const QWORD extracted = _bzhi_u64(val, bitsExtractIndex);

    return extracted | ((LONGLONG)sign >> (64 - bitsExtractIndex));
}

DWORD WINAPI IterateFileRecords(LPVOID lpParm)
{
    return 0;
}
