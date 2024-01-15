#include <windows.h>
#include <devioctl.h>
#include <ntdddisk.h>
#include <ntddscsi.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <strsafe.h>
#include <intsafe.h>
#define _NTSCSI_USER_MODE_
#include <scsi.h>
#include "spti.h"

#define NAME_COUNT  25

#define BOOLEAN_TO_STRING(_b_) \
( (_b_) ? "True" : "False" )

#if defined(_X86_)
#define PAGE_SIZE  0x1000
#define PAGE_SHIFT 12L
#elif defined(_AMD64_)
#define PAGE_SIZE  0x1000
#define PAGE_SHIFT 12L
#elif defined(_IA64_)
#define PAGE_SIZE 0x2000
#define PAGE_SHIFT 13L
#else
// undefined platform?
#define PAGE_SIZE  0x1000
#define PAGE_SHIFT 12L
#endif

LPCSTR BusTypeStrings[] = {
    "Unknown",
    "Scsi",
    "Atapi",
    "Ata",
    "1394",
    "Ssa",
    "Fibre",
    "Usb",
    "RAID",
    "Not Defined",
};
#define NUMBER_OF_BUS_TYPE_STRINGS (sizeof(BusTypeStrings)/sizeof(BusTypeStrings[0]))

int toHex(char* d) {  //convert input
    int data = 0;
    for (int i = 0; i < 2; ++i) {
        char c = toupper(d[i]); 
        if (isdigit(c)) {
            data = (data * 16) + (c - '0'); 
        }
        else if (c >= 'A' && c <= 'F') {
            data = (data * 16) + (c - 'A' + 10); 
        }
        else {
            return 0;
        }
    }
    return data;
}

typedef struct {    //input data format
    int diskNumber;
    int startingLBA;
    int sectorCount;
    int readOperation;
    char* dataPattern;
    int data;
} SCSIArguments;

SCSIArguments parseArguments(int argc, char* argv[]) {  //analysis input data
    SCSIArguments arguments = { -1, -1, -1, 0, NULL };
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--disk") == 0 && i + 1 < argc) {
            arguments.diskNumber = atoi(argv[i + 1]);
        }
        else if (strcmp(argv[i], "--read") == 0) {
            arguments.readOperation = 1;
        }
        else if (strcmp(argv[i], "--write") == 0) {
            arguments.readOperation = 0;
        }
        else if (strcmp(argv[i], "--lba") == 0 && i + 1 < argc) {
            arguments.startingLBA = atoi(argv[i + 1]);
        }
        else if (strcmp(argv[i], "--sector_cnt") == 0 && i + 1 < argc) {
            arguments.sectorCount = atoi(argv[i + 1]);
        }
        else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            arguments.dataPattern = argv[i + 1];
            arguments.data = toHex(argv[i + 1]);
        }
    }
    return arguments;
}

void PrintData(PUCHAR DataBuffer, ULONG endLBA)     //show data 
{  
    printf("\ndata : \n--------------------------------------------------\n");
    for (int i = 0; i < endLBA; i++)
    {
        if (i % 16 == 0)
        {
            printf("\n");
        }
        printf("%02X ", DataBuffer[i]);
    }
    printf("\n");
}

VOID
__cdecl
main(_In_ int argc, _In_z_ char* argv[])
{
    DWORD accessMode = 0, shareMode = 0;
    HANDLE fileHandle;
    SCSI_PASS_THROUGH_WITH_BUFFERS sptwb;
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER sptdwb;
    TCHAR string[NAME_COUNT];
    LPCTSTR path = TEXT("\\\\.\\PHYSICALDRIVE%d");     //set up device id
    size_t strSize = NAME_COUNT * sizeof(string);
    int status = 0;
    DWORD bytesReturn;
    BYTE dataBuffer[32 * 1024 + 10];

    ULONG length = 0,
        errorCode = 0,
        returned = 0,
        sectorSize = 512;

    if (argc < 2) {
        printf("Usage: scsi.exe --disk <disk_number> --read/--write --lba <starting_lba> --sector_cnt <number_of_sectors> --data <data_pattern>\n");
    }

    SCSIArguments parsedArgs = parseArguments(argc, argv);

    int sector_cnt = sectorSize * parsedArgs.sectorCount;
    int lba_start = parsedArgs.startingLBA;

    StringCbPrintf(string, strSize, path, parsedArgs.diskNumber);

    if (parsedArgs.readOperation) {
        shareMode = FILE_SHARE_READ;
    }
    else  shareMode = FILE_SHARE_WRITE;


    printf("Disk Number: %d\n", parsedArgs.diskNumber);
    printf("Operation: %s\n", parsedArgs.readOperation ? "Read" : "Write");
    printf("Starting LBA: %d\n", parsedArgs.startingLBA);
    printf("Sector Count: %d\n", parsedArgs.sectorCount);
    printf("Data Pattern: %s\n", parsedArgs.dataPattern);

    fileHandle = CreateFile(string,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (fileHandle == INVALID_HANDLE_VALUE) {
        printf("Connect Fail\n");
        return;
    }

    ZeroMemory(&sptdwb, sizeof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER));
    sptdwb.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    sptdwb.sptd.PathId = 0;
    sptdwb.sptd.TargetId = 1;
    sptdwb.sptd.Lun = 0;
    sptdwb.sptd.CdbLength = 10;
    sptdwb.sptd.DataIn = SCSI_IOCTL_DATA_IN;
    sptdwb.sptd.SenseInfoLength = 24;
    sptdwb.sptd.DataTransferLength = 8;
    sptdwb.sptd.TimeOutValue = 2;
    sptdwb.sptd.DataBuffer = dataBuffer; //store data
    sptdwb.sptd.SenseInfoOffset =
        offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, ucSenseBuf);
    sptdwb.sptd.Cdb[0] = 0x25;   //recive capacity command
    length = sizeof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER);
    status = DeviceIoControl(fileHandle,
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        &sptdwb,
        length,
        &sptdwb,
        length,
        &bytesReturn,
        NULL);
    if (0 == status)
    {
        printf("Fail to recive Disk's capacity\n");
        return;
    }

    if (shareMode == 1)
    {
        int readSectors = parsedArgs.sectorCount; //read length
        ZeroMemory(&sptdwb, sizeof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER));
        sptdwb.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
        sptdwb.sptd.PathId = 0;
        sptdwb.sptd.TargetId = 1;
        sptdwb.sptd.Lun = 0;
        sptdwb.sptd.CdbLength = CDB16GENERIC_LENGTH; //SCSI command length
        sptdwb.sptd.DataIn = SCSI_IOCTL_DATA_IN;
        sptdwb.sptd.SenseInfoLength = 24;
        sptdwb.sptd.DataTransferLength = sector_cnt; //read data
        sptdwb.sptd.TimeOutValue = 2;
        sptdwb.sptd.DataBuffer = dataBuffer;
        sptdwb.sptd.SenseInfoOffset =
            offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, ucSenseBuf);
        sptdwb.sptd.Cdb[0] = SCSIOP_READ16;          //read16 : 0x88
        sptdwb.sptd.Cdb[2] = (lba_start >> 24) & 0xff; //start from lba_start
        sptdwb.sptd.Cdb[3] = (lba_start >> 16) & 0xff;
        sptdwb.sptd.Cdb[8] = (lba_start >> 8) & 0xff;
        sptdwb.sptd.Cdb[9] = lba_start & 0xff;
        sptdwb.sptd.Cdb[10] = (readSectors >> 8) & 0xff;
        sptdwb.sptd.Cdb[13] = readSectors & 0xff;
        length = sizeof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER);
        status = DeviceIoControl(fileHandle,
            IOCTL_SCSI_PASS_THROUGH_DIRECT,
            &sptdwb,
            length,
            &sptdwb,
            length,
            &bytesReturn,
            FALSE);
        if (0 == status)
        {
            printf("Read Disk data Fail\n");
            return;
        }
        PrintData(dataBuffer, readSectors * sectorSize);
    }

    if (shareMode == 2)
    {
        int writeSectors = parsedArgs.sectorCount; //write length
        ZeroMemory(&sptdwb, sizeof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER));
        sptdwb.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
        sptdwb.sptd.PathId = 0;
        sptdwb.sptd.TargetId = 1;
        sptdwb.sptd.Lun = 0;
        sptdwb.sptd.CdbLength = CDB16GENERIC_LENGTH; //SCSI command length
        sptdwb.sptd.DataIn = SCSI_IOCTL_DATA_OUT;
        sptdwb.sptd.SenseInfoLength = 0;
        sptdwb.sptd.DataTransferLength = sector_cnt;
        sptdwb.sptd.TimeOutValue = 2;
        sptdwb.sptd.DataBuffer = (BYTE*)malloc(sector_cnt * sizeof(BYTE));
        FillMemory(sptdwb.sptd.DataBuffer, sector_cnt, parsedArgs.data);
        sptdwb.sptd.Cdb[0] = SCSIOP_WRITE16;          //write16 : 0x8A
        sptdwb.sptd.Cdb[2] = (lba_start >> 24) & 0xff; //start from lba_start
        sptdwb.sptd.Cdb[3] = (lba_start >> 16) & 0xff;
        sptdwb.sptd.Cdb[8] = (lba_start >> 8) & 0xff;
        sptdwb.sptd.Cdb[9] = lba_start & 0xff;
        sptdwb.sptd.Cdb[10] = (writeSectors >> 8) & 0xff;
        sptdwb.sptd.Cdb[13] = writeSectors & 0xff;
        length = sizeof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER);
        status = DeviceIoControl(fileHandle,
            IOCTL_SCSI_PASS_THROUGH_DIRECT,
            &sptdwb,
            length,
            &sptdwb,
            length,
            &bytesReturn,
            FALSE);
        if (0 == status)
        {
            printf("Write Disk data Fail\n");
            return;
        }
        printf("Done\n");
        free(sptdwb.sptd.DataBuffer);
    }
    CloseHandle(fileHandle);
}