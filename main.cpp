#include <iostream>
#include <cassert>
#include <memory>
#include <string>
#include <locale>
#include <codecvt>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <intrin.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>

#include "NTFS_Structure.h"


[[noreturn]] void ExitErrorWinApi(std::string msg);

LONGLONG ExtractLowerBytesSigned(LONGLONG val, BYTE nBytesExtract);

/** Iterate and print file records
 *
 *  @param lpParm   [in] Pointer of IterateFileRecordsParam
*/
unsigned __stdcall IterateFileRecords(LPVOID lpParm);

struct IterateFileRecordsParam {
    BYTE*                FileRecords_Raw;
    QWORD                ClustorNumber;
    QWORD                RecordCount;
    std::vector<BYTE>*   OutStream;
};


WORD  gBytesPerSector = 0;
BYTE  gSectorsPerCluster = 0;
QWORD gBytesPerCluster = 0;
ULONG gBytesPerFileRecord = 1024; // Typically NTFS's MFT size is 1KB.

int wmain(int argc, wchar_t** argv)
{
    const std::locale utf16_locale = std::locale(std::locale(), new std::codecvt_utf16<wchar_t>());
    //std::wofstream f(L"output.list");
    //f.imbue(utf16_locale);

    // Open logical disk
    // TODO: Test performance of FILE_FLAG_OVERLAPPED, FILE_FLAG_NO_BUFFERING
    WCHAR diskPath[] = L"\\\\.\\C:";
    HANDLE hDrive = CreateFileW(diskPath, GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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
    gBytesPerCluster = gBytesPerSector * gSectorsPerCluster;


    // Read $MFT's file record header to get entry size
    const QWORD  loc_$MFT_FileRecord = (QWORD)bootSector.BPB.LCN_$MFT * gBytesPerCluster;
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
    CloseHandle(hDrive);
    hDrive = NULL;


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
        std::vector<IterateFileRecordsParam> runIterThreads_RunParamList;
        QWORD  currentLCN = 0;
        QWORD  nBytesTotalRunContent = 0;
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
            currentLCN += runOffset;
            nBytesTotalRunContent += runLength * gBytesPerCluster;

            // Push run information parameter
            IterateFileRecordsParam param;
            param.FileRecords_Raw = NULL;
            param.OutStream = NULL;
            param.ClustorNumber = currentLCN;
            param.RecordCount = runLength * ((gBytesPerCluster) / gBytesPerFileRecord);
            runIterThreads_RunParamList.push_back(param);
        }

        // Read all file records (as FILE_FLAG_OVERLAPPED)
        hDrive = CreateFileW(diskPath, GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        if (hDrive == INVALID_HANDLE_VALUE) {
            ExitErrorWinApi("Need administrator permission.");
        }
        std::unique_ptr<BYTE[]> fileRecords_Raw = std::make_unique<BYTE[]>(nBytesTotalRunContent);
        {
            std::vector<HANDLE>     fileReadEvents;
            std::vector<DWORD>      fileReadLengthList;
            QWORD  nBytesReadOffset = 0;
            for (IterateFileRecordsParam& paramIt : runIterThreads_RunParamList)
            {
                HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                if (hEvent == NULL) {
                    ExitErrorWinApi("Failed CreateEvent().");
                }
                fileReadEvents.push_back(hEvent);
                fileReadLengthList.push_back((DWORD)0);

                OVERLAPPED overlapped;
                ZeroMemory(&overlapped, sizeof(OVERLAPPED));
                overlapped.hEvent = hEvent;
                overlapped.Offset     = (DWORD)((paramIt.ClustorNumber * gBytesPerCluster) & 0xFFFFFFFF);
                overlapped.OffsetHigh = (DWORD)(((paramIt.ClustorNumber * gBytesPerCluster) >> 32) & 0xFFFFFFFF);
                assert((QWORD)paramIt.RecordCount * gBytesPerFileRecord <= MAXDWORD);
                if (!ReadFile(hDrive, fileRecords_Raw.get() + nBytesReadOffset, (DWORD)paramIt.RecordCount * gBytesPerFileRecord, &fileReadLengthList.back(), &overlapped)) {
                    if (GetLastError() != ERROR_IO_PENDING) {
                        ExitErrorWinApi("Failed ReadFile()");
                    }
                }

                nBytesReadOffset += paramIt.RecordCount * gBytesPerFileRecord;
            }
            assert(fileReadEvents.size() <= MAXDWORD);
            if (WaitForMultipleObjects((DWORD)fileReadEvents.size(), fileReadEvents.data(), TRUE, INFINITE) == WAIT_FAILED) {
                ExitErrorWinApi("Failed to wait for all file records read.");
            }
        }
        CloseHandle(hDrive);
        hDrive = NULL;

        // Iterate all of file records
        std::vector<HANDLE>                   runIterThreadList;
        std::vector<IterateFileRecordsParam*> runIterThreads_ParamList;
        std::vector< std::vector< BYTE >* >   runIterThreads_OutStreamList;
        {
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            const int numCpuCore  = sysinfo.dwNumberOfProcessors;
            const QWORD numThread = numCpuCore; //< or x2

            const QWORD totalRecordClusterCount = nBytesTotalRunContent / gBytesPerCluster;
            const QWORD numRecordClusterPerThread = totalRecordClusterCount / numThread;
            const QWORD numRecordClusterPerThread_Remain = totalRecordClusterCount % numThread;

            for (size_t i = 0; i < numThread; i++)
            {
                IterateFileRecordsParam* param = new IterateFileRecordsParam();
                param->FileRecords_Raw = fileRecords_Raw.get();
                param->ClustorNumber = i * numRecordClusterPerThread;
                param->RecordCount = numRecordClusterPerThread * (gBytesPerCluster / gBytesPerFileRecord);
                if (i == numThread - 1) {
                    param->RecordCount += numRecordClusterPerThread_Remain;
                }
                runIterThreads_OutStreamList.push_back(new std::vector<BYTE>());
                param->OutStream = runIterThreads_OutStreamList.back();

                runIterThreads_ParamList.push_back(param);
                HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, IterateFileRecords, param, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
                if (hThread == (HANDLE)0 || hThread == (HANDLE)-1L) {
                    ExitErrorWinApi("_beginthreadex failed.");
                }

                runIterThreadList.push_back(hThread);

                std::wcout << "[LOG]Begin runlist entry thread: " << i << std::endl;
            }
            if (WaitForMultipleObjects(numThread, runIterThreadList.data(), TRUE, INFINITE) == WAIT_FAILED) {
                ExitErrorWinApi("Failed to wait for runlist entry threads.");
            }

            // Write result to file
            std::ofstream fsResult("output.list", std::ios::out | std::ios::binary);
            //fsResult.imbue(utf16_locale);
            DWORD UTF16_BOM = 0xFEFF;
            fsResult.write((LPSTR)&UTF16_BOM, sizeof(DWORD));
            for (std::vector<BYTE>* it : runIterThreads_OutStreamList) {
                fsResult.write((LPSTR)it->data(), it->size());
                delete(it);
            }
            fsResult.close();
            runIterThreads_OutStreamList.clear();

            for (IterateFileRecordsParam* it : runIterThreads_ParamList) {
				delete(it);
			}
        }
    }


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

unsigned __stdcall IterateFileRecords(LPVOID lpParm)
{
    IterateFileRecordsParam* Param = (IterateFileRecordsParam*)lpParm;
    std::vector<BYTE>* outStream = Param->OutStream;

    // Iterate file records
    LPBYTE pCurrentRecord            = (LPBYTE)Param->FileRecords_Raw + (Param->ClustorNumber * gBytesPerCluster);
    LPBYTE loc_RecordIteration_End   = (LPBYTE)pCurrentRecord + (Param->RecordCount * gBytesPerFileRecord);
    for (; pCurrentRecord < loc_RecordIteration_End; pCurrentRecord += gBytesPerFileRecord)
    {
        NTFS_FileRecordHeader* currentRecord_Header = (NTFS_FileRecordHeader*)pCurrentRecord;
        if (strncmp((LPSTR)currentRecord_Header->Signature, "FILE", sizeof(currentRecord_Header->Signature)) != 0) {
            // std::wcout << "[LOG] NowRecord = " << pCurrentRecord << std::endl;
            if (strncmp((LPSTR)currentRecord_Header->Signature, "BAAD", sizeof(currentRecord_Header->Signature)) != 0) {
                std::wcout << "::[LOG] Detected BAAD MFT entry." << std::endl;
                continue;
            }
            break;
        }
        assert(currentRecord_Header->BytesPerFileRecord == gBytesPerFileRecord);

        // Find $FILE_NAME attribute from attribute chain
        NTFS_FileRecord_AttrHeader* pFileNameAttr = NULL;
        {
            NTFS_FileRecord_AttrHeader* loc_FirstAttribute = (NTFS_FileRecord_AttrHeader*)(pCurrentRecord + currentRecord_Header->FirstAttributeOffset);
            NTFS_FileRecord_AttrHeader* loc_Entry_End = (NTFS_FileRecord_AttrHeader*)(pCurrentRecord + currentRecord_Header->BytesUsedFileRecord);
            NTFS_FileRecord_AttrHeader* pAttr = loc_FirstAttribute;
            while ((LPBYTE)pAttr <= (LPBYTE)loc_Entry_End - sizeof(NTFS_FileRecord_AttrHeader))
            {
                if (pAttr->TypeCode == eNTFS_FileRecord_AttrHeader_TypeCode::$FILE_NAME) {
                    // @note. There can exist multiple $FILE_NAME.
                    pFileNameAttr = pAttr;
                }

                // Advance to next attribute
                pAttr = (NTFS_FileRecord_AttrHeader*)((SIZE_T)pAttr + pAttr->RecordLength);
            }
        }
        if (pFileNameAttr == NULL) {
            //std::wcout << "[No Name]" << std::endl;
            continue;
        }

        // Get file name 
        assert(pFileNameAttr->FormCode == eNTFS_FileRecord_AttrHeader_FormCode::RESIDENT_FORM);
        NTFS_FileRecord_Attr_Filename* attrFileName = (NTFS_FileRecord_Attr_Filename*)((LPBYTE)pFileNameAttr + pFileNameAttr->Form.Resident.ValueOffset);
        switch (attrFileName->FileNameType)
        {
        case FILENAME_NAMETYPE_WIN32_7_DOS:
        case FILENAME_NAMETYPE_DOS:
        case FILENAME_NAMETYPE_POSIX:
        case FILENAME_NAMETYPE_WIN32: {
            static_assert(sizeof(WCHAR) == 2, "wchar_t is not 2byte.");
            static_assert(sizeof(attrFileName->FileNameLength) == 1, "Maximum of FileNameLength is not 256.");

            std::wstring fileNameResult = std::wstring(attrFileName->FileName, (size_t)attrFileName->FileNameLength);

            // [DEBUG] Dereference parent directory to get path
            QWORD currParentDirectoryReference = attrFileName->ParentDirectory;
            while (true)
            {
                const QWORD parentDirectoryRecordIndex = (currParentDirectoryReference & 0x0000FFFFFFFFFFFF);
                const QWORD parentDirectorySequenceNum = (currParentDirectoryReference & 0xFFFF000000000000) >> 60;

                NTFS_FileRecordHeader* parentDirectory_RecordHeader = (NTFS_FileRecordHeader*)(Param->FileRecords_Raw + (parentDirectoryRecordIndex * gBytesPerFileRecord));

                // Find $FILE_NAME attribute from parent's attribute chain
                NTFS_FileRecord_AttrHeader* pFileNameAttr = NULL;
                {
                    NTFS_FileRecord_AttrHeader* loc_FirstAttribute = (NTFS_FileRecord_AttrHeader*)((LPBYTE)parentDirectory_RecordHeader + parentDirectory_RecordHeader->FirstAttributeOffset);
                    NTFS_FileRecord_AttrHeader* loc_Entry_End      = (NTFS_FileRecord_AttrHeader*)((LPBYTE)parentDirectory_RecordHeader + parentDirectory_RecordHeader->BytesUsedFileRecord);
                    NTFS_FileRecord_AttrHeader* pAttr = loc_FirstAttribute;
                    while ((LPBYTE)pAttr <= (LPBYTE)loc_Entry_End - sizeof(NTFS_FileRecord_AttrHeader))
                    {
                        if (pAttr->TypeCode == eNTFS_FileRecord_AttrHeader_TypeCode::$TYPECODE_END) {
                            break;
                        }

                        if (pAttr->TypeCode == eNTFS_FileRecord_AttrHeader_TypeCode::$FILE_NAME) {
                            // @note. There can exist multiple $FILE_NAME.
                            pFileNameAttr = pAttr;
                        }

                        // Advance to next attribute
                        pAttr = (NTFS_FileRecord_AttrHeader*)((SIZE_T)pAttr + pAttr->RecordLength);
                    }
                }
                if (pFileNameAttr == NULL) {
                    //std::wcout << "[No Name]" << std::endl;
                    break;
                }

                NTFS_FileRecord_Attr_Filename* parentDirecoryName = (NTFS_FileRecord_Attr_Filename*)((LPBYTE)pFileNameAttr + pFileNameAttr->Form.Resident.ValueOffset);
                fileNameResult += L"<-" + std::wstring(parentDirecoryName->FileName, parentDirecoryName->FileNameLength);
                if (parentDirecoryName->FileName == std::wstring(L".")) {
                    break;
                }

                currParentDirectoryReference = parentDirecoryName->ParentDirectory;
            }

            const size_t originalSize = outStream->size();
            const size_t adjustSize = originalSize + ((fileNameResult.length() * 2) + (std::wstring(L"\r\n").length() * 2));
            outStream->resize(adjustSize);

            memcpy(outStream->data() + originalSize, fileNameResult.data(), fileNameResult.length() * 2);
            memcpy(outStream->data() + originalSize + (fileNameResult.length() * 2), std::wstring(L"\r\n").data(), std::wstring(L"\r\n").length() * 2);

            break;
        }
        default: {
            _assume(0);
        }
        }
    }

    return 0;
}