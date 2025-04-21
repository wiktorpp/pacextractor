#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>

typedef struct {
    int16_t someField[24];
    int32_t someInt;
    int16_t productName[256];
    int16_t firmwareName[256];
    int32_t partitionCount;
    int32_t partitionsListStart;
    int32_t someIntFields1[5];
    int16_t productName2[50];
    int16_t someIntFields2[6];
    int16_t someIntFields3[2];
} PacHeader;

typedef struct {
    uint32_t length;
    int16_t partitionName[256];
    int16_t fileName[512];
    uint32_t partitionSize;
    int32_t someFileds1[2];
    uint32_t partitionAddrInPac;
    int32_t someFileds2[3];
    int32_t dataArray[];
} PartitionHeader;

void getString(int16_t* source, char* destination) {
    if (*source == 0) {
        *destination = 0;
        return;
    }
    int length = 0;
    while (*source > 0 && length < 256) {
        *destination = 0xFF & *source;
        destination++;
        source++;
        length++;
    }
    *destination = 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage:\n  pacextractor <firmware_file>.pac [output_directory]\n");
        printf("Extracts partitions from a Spreadtrum/Unisoc firmware file.\n");
        exit(EXIT_FAILURE);
    }

    const char* outputDirectory = (argc >= 3) ? argv[2] : ".";
    struct stat directoryStat;
    if (stat(outputDirectory, &directoryStat) == -1 || !S_ISDIR(directoryStat.st_mode)) {
        printf("Error: The specified output directory '%s' does not exist or is invalid.\n", outputDirectory);
        exit(EXIT_FAILURE);
    }

    int firmwareFile = open(argv[1], O_RDONLY);
    if (firmwareFile == -1) {
        perror("Error: Unable to open the firmware file");
        exit(EXIT_FAILURE);
    }

    struct stat firmwareStat;
    if (fstat(firmwareFile, &firmwareStat) == -1) {
        perror("Error: Unable to retrieve firmware file information");
        close(firmwareFile);
        exit(EXIT_FAILURE);
    }

    if (firmwareStat.st_size < sizeof(PacHeader)) {
        printf("Error: The firmware file '%s' is too small to be valid. Please verify the file.\n", argv[1]);
        close(firmwareFile);
        exit(EXIT_FAILURE);
    }

    PacHeader firmwareHeader;
    if (read(firmwareFile, &firmwareHeader, sizeof(PacHeader)) <= 0) {
        perror("Error: Failed to read the firmware header");
        close(firmwareFile);
        exit(EXIT_FAILURE);
    }

    char firmwareName[256], partitionFileName[256];
    getString(firmwareHeader.firmwareName, firmwareName);
    printf("Firmware Name: %s\n", firmwareName);

    uint32_t partitionOffset = firmwareHeader.partitionsListStart;
    PartitionHeader** partitionHeaders = malloc(sizeof(PartitionHeader*) * firmwareHeader.partitionCount);
    if (!partitionHeaders) {
        perror("Error: Memory allocation failed for partition headers");
        close(firmwareFile);
        exit(EXIT_FAILURE);
    }

    for (int partitionIndex = 0; partitionIndex < firmwareHeader.partitionCount; partitionIndex++) {
        lseek(firmwareFile, partitionOffset, SEEK_SET);
        uint32_t partitionHeaderLength;
        if (read(firmwareFile, &partitionHeaderLength, sizeof(uint32_t)) <= 0) {
            perror("Error: Failed to read the partition header length");
            free(partitionHeaders);
            close(firmwareFile);
            exit(EXIT_FAILURE);
        }

        partitionHeaders[partitionIndex] = malloc(partitionHeaderLength);
        if (!partitionHeaders[partitionIndex]) {
            perror("Error: Memory allocation failed for a partition header");
            free(partitionHeaders);
            close(firmwareFile);
            exit(EXIT_FAILURE);
        }

        lseek(firmwareFile, partitionOffset, SEEK_SET);
        partitionOffset += partitionHeaderLength;
        if (read(firmwareFile, partitionHeaders[partitionIndex], partitionHeaderLength) <= 0) {
            perror("Error: Failed to read the partition header");
            free(partitionHeaders[partitionIndex]);
            free(partitionHeaders);
            close(firmwareFile);
            exit(EXIT_FAILURE);
        }

        getString(partitionHeaders[partitionIndex]->partitionName, firmwareName);
        getString(partitionHeaders[partitionIndex]->fileName, partitionFileName);
        printf("Partition: %s\n\tFile Name: %s\n\tSize: %u bytes\n", firmwareName, partitionFileName, partitionHeaders[partitionIndex]->partitionSize);
    }

    for (int partitionIndex = 0; partitionIndex < firmwareHeader.partitionCount; partitionIndex++) {
        if (partitionHeaders[partitionIndex]->partitionSize == 0) {
            free(partitionHeaders[partitionIndex]);
            continue;
        }

        lseek(firmwareFile, partitionHeaders[partitionIndex]->partitionAddrInPac, SEEK_SET);
        getString(partitionHeaders[partitionIndex]->fileName, partitionFileName);

        char outputFilePath[512];
        snprintf(outputFilePath, sizeof(outputFilePath), "%s/%s", outputDirectory, partitionFileName);

        if (access(outputFilePath, F_OK) == 0) {
            printf("Error: The file '%s' already exists. Extraction aborted.\n", outputFilePath);
            free(partitionHeaders[partitionIndex]);
            free(partitionHeaders);
            close(firmwareFile);
            exit(EXIT_FAILURE);
        }

        int outputFile = open(outputFilePath, O_WRONLY | O_CREAT, 0666);
        if (outputFile == -1) {
            perror("Error: Failed to create the output file");
            free(partitionHeaders[partitionIndex]);
            free(partitionHeaders);
            close(firmwareFile);
            exit(EXIT_FAILURE);
        }

        printf("Extracting: %s\n", outputFilePath);
        uint32_t remainingDataSize = partitionHeaders[partitionIndex]->partitionSize;
        while (remainingDataSize > 0) {
            uint32_t chunkSize = (remainingDataSize > 256) ? 256 : remainingDataSize;
            remainingDataSize -= chunkSize;
            if (read(firmwareFile, firmwareName, chunkSize) != chunkSize) {
                perror("Error: Failed to read partition data");
                close(outputFile);
                free(partitionHeaders[partitionIndex]);
                free(partitionHeaders);
                close(firmwareFile);
                exit(EXIT_FAILURE);
            }
            if (write(outputFile, firmwareName, chunkSize) != chunkSize) {
                perror("Error: Failed to write partition data");
                close(outputFile);
                free(partitionHeaders[partitionIndex]);
                free(partitionHeaders);
                close(firmwareFile);
                exit(EXIT_FAILURE);
            }
            printf("\r\tProgress: %02lu%%", (uint64_t)100 - (uint64_t)100 * remainingDataSize / partitionHeaders[partitionIndex]->partitionSize);
        }
        close(outputFile);
        free(partitionHeaders[partitionIndex]);
    }

    free(partitionHeaders);
    close(firmwareFile);
    printf("All partitions extracted successfully.\n");
    return EXIT_SUCCESS;
}
