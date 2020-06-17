#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#define block_size 512

typedef struct {
    int nextFreeInode;
    int id;
    uint64_t fileSize;
    char fileType;
    uint16_t uid;
    uint16_t gid;
    char permissions[4];
    int16_t reserved;
    int dataBlocks[10];
    int doubleReferenceDataBlock;
    int trippleReferenceDataBlock;
    time_t modificationTime;
    uint16_t checksum;
} Inode;

typedef struct {
    size_t data_block_size;
    size_t inodeSize;
    uint16_t inodesPerDatablock;
    size_t size;
    size_t filesystemSize;
    uint32_t numberOfInodes;
    uint32_t numberOfDataBlocks;
    uint32_t numberOfFreeInodes;
    uint32_t numberOfFreeDataBlocks;
    uint32_t firstFreeInode;
    uint32_t firstFreeDataBlock;
    uint64_t inodeSpace;
    uint64_t dataBlockSpace;
    uint16_t checksum;
} Superblock;

typedef struct {
    uint32_t inodeId;
    char fileName[28];
} DirTableRow;

Superblock superblockConstr(int fileSize, uint16_t data_block_size, int numberOfInodes,
                            uint64_t inodeSpace, int numberOfDataBlocks, uint64_t dataBlockSpace) {
    Superblock sb;
    sb.filesystemSize = fileSize;
    sb.data_block_size = data_block_size;
    sb.inodeSize = sizeof(Inode); 
    sb.numberOfFreeInodes = numberOfInodes;
    sb.numberOfFreeDataBlocks = numberOfDataBlocks;
    sb.firstFreeDataBlock = 0; 
    sb.firstFreeInode = 0;
    sb.inodeSpace = inodeSpace;
    sb.dataBlockSpace = dataBlockSpace;
    sb.inodesPerDatablock = block_size / sb.inodeSize;
    sb.size = sizeof(Superblock);
    sb.checksum = 0;
    sb.numberOfInodes = numberOfInodes;
    sb.numberOfDataBlocks = numberOfDataBlocks;

    return sb;
}

Inode inodeConstr(char fileType, uint16_t uid, uint16_t gid, const char permissions[3]) {
    Inode i;
    i.id = -1;
    i.uid = uid;
    i.gid = gid;
    i.fileType = fileType;
    strcpy(i.permissions, permissions);
    i.modificationTime = time(NULL);
    i.nextFreeInode = -1;
    i.doubleReferenceDataBlock = -1;
    i.trippleReferenceDataBlock = -1;
    i.fileSize = 0;
    i.checksum = 0;
    i.reserved = 0;

    for (int j = 0; j < 10; ++j) {
        i.dataBlocks[j] = -1;
    }

    return i;
}

DirTableRow dirTableRowConstr(uint32_t inodeId, const char* name) {
    DirTableRow newDirTableRow;
    newDirTableRow.inodeId = inodeId;
    strcpy(newDirTableRow.fileName, name);

    return newDirTableRow;
}

void errCheck(int fd, int commandReturnValue, int errCode, const char* errMsg) {
    if(commandReturnValue < 0) {
        int temp = errno;
        close(fd);
        errno = temp;
        err(errCode, "%s", errMsg);
    }
}

void print(int fd, const char * msg) {
    int numWrittenBytes = write(fd, msg, strlen(msg));
    errCheck(fd, numWrittenBytes, 7, "Failed to print message"); 
}

void printWhitespaces(int num) {
    for (int i = 0; i < num; ++i) {
        print(1, " ");
    }
}

int intLenght(long n) {
    if(n == 0) {
        return 1;
    }

    int i = 0;

    while(n > 0) {
        i++;
        n /= 10;
    }
    
    return i;
}

char * intToString(long n) {
    if(n == 0) {
        return "0";
    }

    int numberCount = intLenght(n);
    int i = numberCount - 1;
    char * res = malloc(numberCount + 1);

    while(n > 0) {
        res[i] = n % 10 + '0';
        n /= 10;
        i--;
    }
    
    res[numberCount] = '\0';

    return res;
}

int printNumber(const char * msg, long n) {
    char * numArr = intToString(n);
    print(1, msg);
    print(1, numArr);
    int numLen = strlen(numArr);
    free(numArr);
    return numLen + strlen(msg);
}

int myOpen(const char * fileName, int mode) {
    int fd = open(fileName, mode);

    if(fd < 0) {
        err(2, "Invalid or nonexistent fileName");    
    }

    return fd;
}


uint64_t mySeek(int fd, uint64_t offset, int mode, const char * errMsg) {
    off_t pos;
    pos = lseek(fd, offset, mode);
    errCheck(fd, pos, 9, errMsg);

    return pos;
}

void seekInodeById(int fd, Superblock sb, int inodeId) {
    long offsetPerNInodes = sb.data_block_size - sb.inodesPerDatablock * sb.inodeSize;
    long ofssetAmmount = (inodeId / sb.inodesPerDatablock) * offsetPerNInodes;
    long inodeOffset = sb.data_block_size + inodeId * sb.inodeSize + ofssetAmmount;

    mySeek(fd, inodeOffset, SEEK_SET, 
           "Couldn't seek inode during InodeCount validation");
}

void seekDatablockById(int fd, Superblock sb, int dbId) {
    uint32_t startOffset = sb.data_block_size + sb.inodeSpace;
    mySeek(fd, startOffset + dbId * sb.data_block_size, SEEK_SET, 
            "Failed to seek datablock in getNextFreeDatablock");
}

Inode readInodeById(int fd, Superblock sb, int inodeId) {
    Inode readInode;
    seekInodeById(fd, sb, inodeId);

    int readValue = read(fd, &readInode, sizeof(Inode));
    errCheck(fd, readValue, 6, "Inode could not be read");

    return readInode;
}

void readSuperblock(int fd, Superblock * sb) {
    mySeek(fd, 0, SEEK_SET, "Error seeking to start of file ine readSuperblock");
    int readValue = read(fd, sb, sizeof(Superblock));

    errCheck(fd, readValue, 6, "Superblock could not be read");
}

//reads Inode. This bit of code was often repeated so I added this function
//It is used when reading inodes sequentialy incide a for. The i is used to determine
//if offset is required.
void readNextInode(int fd, Inode * inode, int i, Superblock sb) {

    int inodeOffset = sb.data_block_size % sb.inodeSize;
    if(i != 0 && i % sb.inodesPerDatablock == 0) {
        //on every n inodes it adds the offsets
        mySeek(fd, inodeOffset, SEEK_CUR,
               "lseek failed in skipping offset in inode validation");  
    }

    int readValue = read(fd, inode, sizeof(Inode));
    errCheck(fd, readValue, 6, "Read of inode failed");
}

void initDataBlocks(int fd, Superblock sb) {
    int nextFreeDataBlockIndex;

    long currentOffset = mySeek(fd, 0, SEEK_CUR, "lseek error in initDataBlocks");
    if((currentOffset % sb.data_block_size) != 0) {
        errx(12, "Invalid offset at start of data block initialization");
    }

    for (int i = 0; i < sb.numberOfDataBlocks; ++i) {
        nextFreeDataBlockIndex = i + 1;

        if(i == sb.numberOfFreeDataBlocks - 1) {
            nextFreeDataBlockIndex = -1;
        }

        int writtenBytes = write(fd, &nextFreeDataBlockIndex, sizeof(int));
        errCheck(fd, writtenBytes, 7,
                "Error while writing index of next data block in initDataBlocks");

        mySeek(fd, sb.data_block_size - sizeof(int), SEEK_CUR,
               "Error lseeking to next next data block i initDataBlocks");
    }
}

uint16_t Fletcher16Checksum(uint8_t * data, int size) {
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;

    for (int i = 0; i < size; ++i) {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255; //assures that the order of the numbers matters
    }

    return (sum2 << 8) | sum1;
}

void printInodeForTesting(Inode i) {
    printf("INODE\n");
    printf("%d\n", i.checksum);
    for (int j = 0; j < 10; ++j) {
        printf("%d\n", i.dataBlocks[j]);
    }
    printf("%d\n", i.doubleReferenceDataBlock);
    printf("%ld\n", i.fileSize);
    printf("%c\n", i.fileType);
    printf("%d\n", i.gid);
    printf("%d\n", i.id);
    printf("%ld\n", i.modificationTime);
    printf("%d\n", i.nextFreeInode);
    printf("%s\n", i.permissions);
    printf("%d\n", i.reserved);
    printf("%d\n", i.trippleReferenceDataBlock);
    printf("%d\n", i.uid);

    printf("---------------------\n");
}

void printPermissions(const char perms[4]) {
    int currNum;
    char letters[4];
    letters[3] = '\0';

    for (int i = 0; i < 3; ++i) {
        letters[0] = '-';
        letters[1] = '-';
        letters[2] = '-';
        currNum = perms[i] - '0';

        if(currNum >= 4) {
            letters[0] = 'r';
            currNum -= 4;
        } 

        if(currNum >= 2) {
            letters[1] = 'w';
            currNum -= 2;
        }

        if(currNum == 1) {
            letters[2] = 'x';
        }

        print(1, letters);
    }

    print(1, " ");
}

void printUserAndGroupNames(int uid, int gid) {
    print(1, getpwuid(uid)->pw_name);  
    print(1, " ");
    print(1, getgrgid(gid)->gr_name);  
    print(1, " ");
}

void printModificationTime(time_t unixTime) {
    char buff[40];
    struct tm * info;
    info = localtime(&unixTime);

    strftime(buff, 40, "%Y-%m-%dT%H-%M-%S", info);
    print(1, buff);
    print(1, " ");
}

void printInode(Inode i, char * fileName) {
    if(i.fileType == 'd') {
        print(1, "d");
    } else {
        print(1, "-");
    }

    printPermissions(i.permissions);
    printUserAndGroupNames(i.uid, i.gid);   
    print(1, intToString(i.fileSize));
    print(1, " ");
    printModificationTime(i.modificationTime);
    if(strlen(fileName) != 0) {
        print(1, fileName);
    } else {
        print(1, "+/");
    }
    print(1, "\n");
}

void initInodes(int fd, Superblock sb) {
    int testOffset;
    Inode inode = inodeConstr('d', 0, 0, "755");
    mySeek(fd, sb.data_block_size, SEEK_SET, "lseek failed in during start of inode initialization");

    int inodeOffset = sb.data_block_size % sb.inodeSize;

    for (int i = 0; i < sb.numberOfFreeInodes; ++i) {
        if(i != 0 && i % sb.inodesPerDatablock == 0) {
            //on every n inodes it adds the offsets
            testOffset = mySeek(fd, inodeOffset, SEEK_CUR,
                                "lseek failed in skipping offset in inode initialization");  
            
            if(testOffset % sb.data_block_size != 0) {
                errx(11, "Issue with inode initialization");
            }
        }

        inode.id = i;
        inode.nextFreeInode = i + 1;
        
        if(i == sb.numberOfFreeInodes - 1) {
            inode.nextFreeInode = -1;
        }

        inode.checksum = 0;
        inode.checksum = Fletcher16Checksum((uint8_t*) &inode, sizeof(Inode));

        int writtenBytes = write(fd, &inode, sizeof(Inode));
        errCheck(fd ,writtenBytes, 7, "Write of inode initialization");
    }
    
    mySeek(fd, inodeOffset, SEEK_CUR, "Final lseek for offset failed in inodeInit");
}

void delegateInode(int fd, Superblock * sb, Inode * i) {
    if(sb->numberOfFreeInodes == 0) {
        errx(8, "No inodes are left. File cannot be created");
    }

    //Reads the currently written inode and gets the index of the next
    //one that is free and we write that to the superblock. After that we go back
    //to the id of the inode we want to "create"
    Inode buff = readInodeById(fd, *sb, sb->firstFreeInode);
    sb->firstFreeInode = buff.nextFreeInode;
    mySeek(fd, -sizeof(Inode), SEEK_CUR, 
          "Error seeking back to the position of an inode in delegateInode");
    
    //Changes the id of the node to be that of the free inode.
    i->id = buff.id;
    i->checksum = Fletcher16Checksum((uint8_t *) i, sizeof(Inode));

    int writtenBytes = write(fd, i, sizeof(Inode));
    errCheck(fd, writtenBytes, 7, "Error delegating Inode");

    sb->numberOfFreeInodes--;
}

void writeInode(int fd, Superblock sb, Inode i) {
    seekInodeById(fd, sb, i.id);
    i.checksum = 0;
    i.checksum = Fletcher16Checksum((uint8_t *) &i, sizeof(Inode));
    int writtenBytes = write(fd, &i, sizeof(Inode));
    errCheck(fd , writtenBytes, 7, "Error writing inode"); 
}

void writeSuperblock(int fd, Superblock sb) {
    mySeek(fd, 0, SEEK_SET, 
           "Failed to seek to start of file while writing Superblock");

    sb.checksum = 0;
    sb.checksum = Fletcher16Checksum((uint8_t *) &sb, sizeof(Superblock));
    int writtenBytes = write(fd, &sb, sizeof(Superblock));
    errCheck(fd , writtenBytes, 7, "Error writing Superblock"); 
}

int getFileSize(const char * fileName) {
    int p[2];

    if(pipe(p) < 0) {
        err(3, "Opening pipe in mkfs failed");
    }

    pid_t childP = fork();

    if(childP == 0) {
        close(p[0]); 
               
        if(dup2(p[1], 1) < 0) {
            err(4, "Problem with dup in child process of mkfs");
        }
        
        if(execlp("stat", "stat", fileName, "-c %s", (char*)0) < 0) {
            err(5, "Exec failed to execute stat and get fileSize"); 
        }
    }

    close(p[1]);

    int result = 0;
    char buff = 0;
    int readErrCheck;

    while((readErrCheck = read(p[0], &buff, 1)) > 0 ) {
        //Output of pipe(stat) starts with ' ' and ends with '\n'. 
        //I do not want theese characters
        if(buff != ' ' && buff != '\n') { 
            result = result * 10 + (buff - '0');
        }
    }

    if(readErrCheck < 0) {
        err(6, "Error reading output from pipe");
    }

    close(p[0]);

    return result;
}

void validateInodes(int fd, Superblock sb) {
    Inode inode;
    int inodeOffset = sb.data_block_size % sb.inodeSize;

    mySeek(fd, sb.data_block_size, SEEK_SET, "lseek failed in during start of inode validation");
    
    for (int i = 0; i < sb.numberOfFreeInodes; ++i) {

        readNextInode(fd, &inode, i, sb);

        uint16_t readChecksum = inode.checksum;
        inode.checksum = 0;
        if(readChecksum != Fletcher16Checksum((uint8_t*) &inode, sizeof(Inode))) {
            close(fd);
            errx(11, "Inode initialization failed! Inode Corrupt");
        };

        if(inode.id != i) {
            close(fd);
            errx(11, "Inode initialization failed! Wrong inode id!");
        }

        if(i != sb.numberOfFreeInodes - 1 && inode.nextFreeInode != i + 1) {
            close(fd);
            errx(11, "Inode initialization failed! Wrong nextFreeInode");
        }

        if( i == sb.numberOfFreeInodes - 1 && inode.nextFreeInode != -1) {
            close(fd);
            errx(11, "Initialization of last inode failed. Wrong nextFreeInode");
        }
    }

    mySeek(fd, inodeOffset, SEEK_CUR, "Final lseek for offset failed in inodeValidation");
    write(1, "Successful inode initialization!\n", 33);
}

void validateDataBlocks(int fd, Superblock sb) {
    int nextFreeDataBlockIndex;
    for (int i = 0; i < sb.numberOfFreeDataBlocks; ++i) {
        nextFreeDataBlockIndex = i + 1;

        if(i == sb.numberOfFreeDataBlocks - 1) {
            nextFreeDataBlockIndex = -1;
        }

        int readBytes = read(fd, &nextFreeDataBlockIndex, sizeof(int));
        errCheck(fd, readBytes, 7, "Error while reading index of next data block in initDataBlocks");

        if(nextFreeDataBlockIndex != i + 1 && i < sb.numberOfFreeDataBlocks - 1) {
            close(fd);
            errx(12, "Datablock initilization failed");
        }

        if(nextFreeDataBlockIndex != -1 && i == sb.numberOfFreeDataBlocks - 1) {
            close(fd);
            errx(12, "Datablock initilization failed lastIndex");
        }

        mySeek(fd, sb.data_block_size - sizeof(int), SEEK_CUR,
              "Error lseeking to next next data block i initDataBlocks");
    }

    write(1, "Successful data block initialization!\n", 38);
}



void printSuperblock(Superblock * sb) {
    printf("%ld\n", sb->dataBlockSpace);
    printf("%ld\n", sb->data_block_size);
    printf("%ld\n", sb->inodeSize);
    printf("%d\n", sb->inodesPerDatablock);
    printf("%d\n", sb->checksum);
    printf("%ld\n", sb->inodeSpace);
    printf("%d\n", sb->numberOfFreeInodes);
    printf("%d\n", sb->numberOfFreeDataBlocks);
    printf("freeData: %d\n", sb->firstFreeDataBlock);
    printf("freeInode: %d\n", sb->firstFreeInode);
    printf("fsSize: %ld\n", sb->filesystemSize);
    printf("sbSize: %ld\n", sb->size);
}

bool validateSuperblockInodeCount(int fd, Superblock sb) {
    uint32_t inodeCounter = 0;
    uint32_t currInodeId = sb.firstFreeInode;   
    Inode inode;

    while(currInodeId != -1) {
        inode = readInodeById(fd, sb, currInodeId);
        currInodeId = inode.nextFreeInode;
        inodeCounter++;
    }

    return inodeCounter != sb.numberOfFreeInodes;
}

bool validateSuperblockDataBlockCount(int fd, Superblock sb) {
    uint32_t dbCounter = 0;
    int currDbId = sb.firstFreeDataBlock;   
    
    seekDatablockById(fd, sb, sb.firstFreeDataBlock);

    while(currDbId != -1) {
        int readBytes = read(fd, &currDbId, sizeof(int));
        errCheck(fd, readBytes, 6, "Could not read datablock during datablockCount validation");

        mySeek(fd, sb.data_block_size - sizeof(int), SEEK_CUR, "Error seeking during dataBock validation");
        dbCounter++;
    }

    return dbCounter != sb.numberOfFreeDataBlocks;
}

void validateSuperbock(int fd, Superblock sb) {
    uint16_t readChecksum = sb.checksum;
    sb.checksum = 0;

    if(Fletcher16Checksum((uint8_t*)& sb, sizeof(Superblock)) != readChecksum) {
        close(fd);
        errx(10, "Corrupted Superblock");
    }

    if(validateSuperblockInodeCount(fd, sb)) {
        close(fd);
        errx(10, "Corrupted Superblock! Invalid free inode count");
    }

    if(validateSuperblockDataBlockCount(fd, sb)) {
        close(fd);
        errx(10, "Corrupted Superblock! Invalid free data block count");
    }
}

void validateInodeChecksums(int fd, Superblock sb) {
    Inode inode;
    mySeek(fd, sb.data_block_size, SEEK_SET, 
           "Error seeking at start of Inode checksum validation");

    for (int i = 0; i < sb.numberOfInodes; ++i) {
        readNextInode(fd, &inode, i, sb);
        uint16_t readChecksum = inode.checksum;
        inode.checksum = 0;

        if(readChecksum != Fletcher16Checksum((uint8_t *) & inode, sizeof(Inode))) {
            close(fd);
            errx(11, "Corrupted Inode");   
        }
    }
}

void fsck(const char * fileName) {
    int fd = myOpen(fileName, O_RDONLY);
    Superblock sb;
    readSuperblock(fd, &sb);
    validateSuperbock(fd, sb);
    validateInodeChecksums(fd, sb);

    print(1, "File system is operational!\n");
}



void debug(const char * fileName) {
    int fd = myOpen(fileName, O_RDWR);
    //I have a starting offset and from it I remove the size of the text
    //I printed in the last command
    int lastPrintOffset;
    int startingOffset = 30;
    Superblock sb; 
    readSuperblock(fd, &sb);
    
    printWhitespaces(20);
    print(1, "This is the structure of BDSM_FS\n");
    print(1, "\n");

    //The starting whitespaces are to even out the :
    printWhitespaces(2);
    printNumber("File system size: ", sb.filesystemSize);
    print(1, "\n");

    printWhitespaces(2);
    lastPrintOffset = printNumber("Superblock  size: ", sb.size);
    //-2 is to take into account the above whitespaces. 
    //+8 to even out with the hardcoded message. Same below
    printWhitespaces(startingOffset - lastPrintOffset - 2 + 8);
    printNumber("Data block size: ", sb.data_block_size);
    printWhitespaces(6);
    printNumber("Inode size: ", sb.inodeSize);
    print(1, "\n");

    printWhitespaces(2);
    lastPrintOffset = printNumber("Number of inodes: ", sb.numberOfInodes);
    printWhitespaces(startingOffset - lastPrintOffset - 2 + 2);
    printNumber("Number of data blocks: ", sb.numberOfDataBlocks);
    print(1, "\n");

    printWhitespaces(7);
    lastPrintOffset = printNumber("Free inodes: ", sb.numberOfFreeInodes);
    printWhitespaces(startingOffset - lastPrintOffset - 7 + 7);
    printNumber("Free data blocks: ", sb.numberOfFreeDataBlocks);
    print(1, "\n");

    lastPrintOffset = printNumber("Inode space(bytes): ", sb.inodeSpace); 
    printWhitespaces(startingOffset - lastPrintOffset);
    printNumber("Data block space(bytes): ", sb.dataBlockSpace); 
    print(1, "\n");
}

char * readWordFromPath(const char * path) {
    //This can be optimized to use less memory
    //But this seemed simpler. The directory name
    //can't be longer than the path
    char * word = malloc(strlen(path) + 1);

    int i = 0;
    while(path[0] != '/' && path[0] != '\0') {
        word[i] = path[0]; 
        i++;
        path++;
    }
    
    word[i] = '\0';
    
    return word;
}


uint32_t getNextFreeDatablock(int fd, Superblock sb) {
    int nextFreeDataBlockIndex;
    printf("Error her %d", sb.firstFreeDataBlock);
    seekDatablockById(fd, sb, sb.firstFreeDataBlock);
    int readBytes = read(fd, &nextFreeDataBlockIndex, sizeof(int));
    errCheck(fd, readBytes, 6, "Error reading nextFreeDataBlockIndex");

    return nextFreeDataBlockIndex;
}

void updateFreeDatablocks(int fd, Superblock * sb, int * inodeDb, int * requiredDataBlocks) {
    if(sb->firstFreeDataBlock == -1) {
        close(fd);
        errx(14, "Not enough space or data blocks");
    } 

    printf("HEEERER\n");
    printf("Delegating db: %d\n", sb->firstFreeDataBlock);
    printf("Free dbs: %d\n", sb->numberOfFreeDataBlocks);
    *inodeDb = sb->firstFreeDataBlock;
    int currOffset = mySeek(fd, 0, SEEK_CUR, 
            "Error getting currentOffset in updateFreeDatablocks");
    sb->firstFreeDataBlock = getNextFreeDatablock(fd, *sb);
    mySeek(fd, currOffset, SEEK_SET, 
            "Error reseeking to old offset in updateFreeDatablocks");
    sb->numberOfFreeDataBlocks--;
    (*requiredDataBlocks)--;
}


int calculateNumberOfDatablocks(Inode inode, Superblock sb) {
    //Removes the ten datablocks from the start
    long size = inode.fileSize - 10 * sb.data_block_size;
    int counter = 0;
    while(size > 0) {
        size -= sb.data_block_size;
        counter++;
    }

    return counter;
}

//Initializes all datablock id's in a reference datablock to -1
//I used this because with triple reference datablocks it seemed really hard or a lot of math
//to find out how many datablocks I have written and in which datablock I need to write.
//Probably is less effective though
void initReferenceDataBlocks(int fd, Superblock sb, uint32_t datablockId) {
    seekDatablockById(fd, sb, datablockId);

    int currWrittenBytes;
    int writtenBytesSum = 0;
    const int value = -1;

    while(writtenBytesSum < sb.data_block_size && writtenBytesSum + sizeof(int) < sb.data_block_size) {
        currWrittenBytes = write(fd, &value, sizeof(int));
        errCheck(fd, currWrittenBytes, 7, 
                "Error writing to a newly delegated reference data block");
        writtenBytesSum += currWrittenBytes;
    }
}

//seeks to the first datablock with an id different from -1
int readUntilEndOfDatablocks(int fd, Superblock sb, int referenceDataBlock) {
    int readBytesSum = 0;
    int readBytes;
    int currInodeId = 0;

    seekDatablockById(fd, sb, referenceDataBlock); 
    while(readBytesSum + sizeof(int) < sb.data_block_size && currInodeId != -1) {
        printf("Next free db: %d\n", sb.firstFreeDataBlock);
        printf("readAndWrittenBytesSum: %d\n", readBytesSum);
        readBytes = read(fd, &currInodeId, sizeof(int));
        errCheck(fd, readBytes, 6, "Error reading from doubleReferenceDataBlock");
        readBytesSum += readBytes;
    }

    if(currInodeId == -1) { 
        mySeek(fd, -sizeof(int), SEEK_CUR, "Error seeking backwards in doubleReferenceDataBlock");
    }

    return readBytesSum;
}

void delegateToDoubleReferenceDataBlock(int fd, Superblock * sb, Inode * inode, int * requiredDataBlocks, int firstDataBlockToDelegate) {
    if(inode->doubleReferenceDataBlock == -1) {
        updateFreeDatablocks(fd, sb, &inode->doubleReferenceDataBlock, requiredDataBlocks);
        (*requiredDataBlocks)++;//to offset the reduction from updateFreeDatablocks
        printf("doubleReferenceDataBlock delegated value %d\n", inode->doubleReferenceDataBlock);
    }

    int newlyDelegatedDb = 0;
    int writtenBytesSum = 0;
    int writtenBytes;

    seekDatablockById(fd, *sb, inode->doubleReferenceDataBlock);
    int pos = lseek(fd, 0, SEEK_CUR);
    printf("pos is %d\n", pos);
    pos = mySeek(fd, firstDataBlockToDelegate * sizeof(int), SEEK_CUR, "Error seeking to firstDataBlockToDelegate");
    printf("pos is pos 2 %d\n", pos);

    while(*requiredDataBlocks > 0 && writtenBytesSum + sizeof(int) < sb->data_block_size) {
        updateFreeDatablocks(fd, sb, &newlyDelegatedDb, requiredDataBlocks);
        printf("\nnewlyDelegatedDb delegated value %d\n", newlyDelegatedDb);
        pos = lseek(fd, 0, SEEK_CUR);
        printf("pos is pos 2.5 before %d\n", pos);
        writtenBytes = write(fd, &newlyDelegatedDb, sizeof(int));
        pos = lseek(fd, 0, SEEK_CUR);
        printf("pos is pos 2.6 after %d\n", pos);
        errCheck(fd, writtenBytes , 7, "Error writing a new datablockId to doubleReferenceDataBlock");
        writtenBytesSum += writtenBytes;
    }
}

void delegateToTripleReferenceDataBlock(int fd, Superblock * sb, Inode * inode, int * requiredDataBlocks) { 

    //TODO: add implementation for files and double and triple pointers
    if(*requiredDataBlocks > 0 && inode->trippleReferenceDataBlock == -1) {
        updateFreeDatablocks(fd, sb, &inode->trippleReferenceDataBlock, requiredDataBlocks);
        initReferenceDataBlocks(fd, *sb, inode->trippleReferenceDataBlock);
    }

    while(*requiredDataBlocks > 0) {
        
    }   
}

void delegateDataBlocksToInode(int fd, Superblock * sb, Inode * inode, uint64_t requiredSpace, int startDatablockToWrite) {
    int requiredDataBlocks = requiredSpace / sb->data_block_size; 
    int dbIdsPerDataBlock = sb->data_block_size / sizeof(int);

    if(requiredSpace % sb->data_block_size != 0) {
        requiredDataBlocks++;
    }

    int index = startDatablockToWrite;
    //Index is less than 10 for the base data blocks
    while(index < 10 && requiredDataBlocks > 0) {
        updateFreeDatablocks(fd, sb, &inode->dataBlocks[index++], &requiredDataBlocks);
    }

    if(requiredDataBlocks > 0 && index - 10 < dbIdsPerDataBlock) {
        delegateToDoubleReferenceDataBlock(fd, sb, inode, &requiredDataBlocks, index - 10);
    }

    if(inode->doubleReferenceDataBlock != -1) {
        int dbCount = calculateNumberOfDatablocks(*inode, *sb);
        seekDatablockById(fd, *sb, inode->doubleReferenceDataBlock);
        mySeek(fd, dbCount * sizeof(DirTableRow), SEEK_CUR,
                "Failed to seek end of doubleReferenceDataBlock");
    }

    //delegateToDoubleReferenceDataBlock(fd, sb, inode, &requiredDataBlocks);
    printf("Dbs Delegated: %d\n", sb->firstFreeDataBlock);

    if(requiredDataBlocks > 0) {
        errx(18, "You are at the file size limit");
    }
}

Inode findInodeInFile(int fd, Superblock sb, Inode parentInode, const char * tableRowFileName) {
    uint64_t readSum = 0;
    DirTableRow dirTableRow;
    Inode resInode;
    resInode.id = -1;
    int inodeIndex = 1;
    bool addOffset = false;
    int numberOfIntsInDb = sb.data_block_size / sizeof(int);

    seekDatablockById(fd, sb, parentInode.dataBlocks[0]);

    while(readSum < parentInode.fileSize && resInode.id == -1) {
        addOffset = readSum % sb.data_block_size + sizeof(DirTableRow) > sb.data_block_size;
        if(inodeIndex < 10 && addOffset) {
            seekDatablockById(fd, sb, parentInode.dataBlocks[inodeIndex++]);
        } else if(numberOfIntsInDb > inodeIndex - 10 && addOffset) {
            seekDatablockById(fd, sb, parentInode.doubleReferenceDataBlock);
            mySeek(fd, (inodeIndex - 10) * sizeof(int), SEEK_CUR, 
                    "Error seeking in doubleReferenceDataBlock findInodeInFile");

            int dbId;
            int readBytes = read(fd, &dbId, sizeof(int));
            errCheck(fd, readBytes, 6, "Error reading doubleReferenceDataBlock in findInodeInFile");
            seekDatablockById(fd, sb, dbId);
        }

        int readBytes = read(fd, &dirTableRow, sizeof(DirTableRow));
        errCheck(fd, readBytes, 6, "Error searching for file");
        readSum += readBytes;

        if(strcmp(dirTableRow.fileName, tableRowFileName) == 0) {
            return readInodeById(fd, sb, dirTableRow.inodeId);
        }
    }

    return resInode;
}

void addDirToDirTable(int fd, Superblock * sb, Inode * parentInode, const char * newDirName) {
    //Inode validationInode = findInodeInFile(fd, *sb, *parentInode, newDirName);
    //if(validationInode.id != -1) {
    //    close(fd);
    //    errx(17, "Name %s taken", newDirName);
    //}

    Inode newDirInode = inodeConstr('d', 0, 0, "755");
    delegateInode(fd, sb, &newDirInode);
    DirTableRow newRow = dirTableRowConstr(newDirInode.id, newDirName);
    int datablockToWrite = parentInode->fileSize / sb->data_block_size;
    int dbOffset = parentInode->fileSize % sb->data_block_size;
    bool noDbInode = (parentInode->dataBlocks[0] == -1);
    bool parentInodeDivisible = parentInode->fileSize % sb->data_block_size == 0;

    if( noDbInode || parentInodeDivisible || 
            (parentInode->fileSize % sb->data_block_size + sizeof(newRow)) > sb->data_block_size) {

        //printf("Here %d\n", parentInode->dataBlocks[datablockToWrite]);
        //This is used to calculate the number of datablocks in the doubleReference datablock that are used
        //Also makes sure the fileSize takes into account the offsets
        if(!noDbInode && !parentInodeDivisible) {
            parentInode->fileSize += sb->data_block_size - (parentInode->fileSize % sb->data_block_size);
            datablockToWrite++;
        }
        //Offsets the datablock if we delegate a new one
        //Some edge cases are when the parentInode is divisible by 512(the data block size).
        //Then the calculation was correct from the beggining so no offseting the datablock is needed
        delegateDataBlocksToInode(fd, sb, parentInode, sb->data_block_size, datablockToWrite);
        dbOffset = 0;
    }

    //TODO: fix this your choice of datablock is invalid  
    int pos = 0; 
    if(datablockToWrite < 10) { 
        printf("DbToWrite %d\n", datablockToWrite);
        printf("DbIndex %d\n", parentInode->dataBlocks[datablockToWrite]);
        seekDatablockById(fd, *sb, parentInode->dataBlocks[datablockToWrite]);
    } else {
        printf("datablock 12: %s\n", newRow.fileName);
        seekDatablockById(fd, *sb, parentInode->doubleReferenceDataBlock);
        printf("%d\n", sb->firstFreeDataBlock);
        pos = mySeek(fd, (datablockToWrite - 10) * sizeof(int), SEEK_CUR, 
                "Error seeking to back to delegated datablock in addDirToDirTable");
        printf("pos pos 3: %d\n", pos);
        int datablockId;
        int readBytes = read(fd, &datablockId, sizeof(int));
        errCheck(fd, readBytes, 6, "Error reading datablock id from doubleReferenceDataBlock");
        printf("pos pos 3 data: %d\n", datablockId);
        seekDatablockById(fd, *sb, datablockId);
    }
    
    pos = mySeek(fd, dbOffset, SEEK_CUR,
            "Failed to seek when adding directory to parent file");

    printf("Position of write: %d\n", pos);
    int writtenBytes = write(fd, &newRow, sizeof(DirTableRow));
    errCheck(fd, writtenBytes, 7, "Error writing new file to directory table");
    parentInode->fileSize += writtenBytes;
    
    printf("Inode file size: %ld\n", parentInode->fileSize);
    printf("Inode file size 2: %d\n", parentInode->dataBlocks[0]);
    writeInode(fd, *sb, *parentInode);
    writeSuperblock(fd, *sb);
}


//Does not work the same way the actual cd does because it does not
//cd into the last directory/file. Instead it returns it's name and
//I use that for lsobj and mkdir
char * cd(int fd, const char * path, Inode * inode, Superblock sb) {
    if(path[0] != '+' || path[1] != '/') {
        errx(13, "Invalid path. Please enter the full path! Root directory is missing");
    }
    
    path += 2;
    char * word;
    word = readWordFromPath(path);
    while(path[0] != '\0') {
        word = readWordFromPath(path);
        path += strlen(word);
        
        if(path[0] != '\0') {
            *inode = findInodeInFile(fd, sb, *inode, word);
            free(word);
            path++;
        } 
    }

    return word;
}

void myMkdir(const char * fileName, const char * path) {
    int fd = myOpen(fileName, O_RDWR);
    Superblock sb;
    readSuperblock(fd, &sb);

    Inode inode = readInodeById(fd, sb, 0);
    char * word = cd(fd, path, &inode, sb);
    addDirToDirTable(fd, &sb, &inode, word); 

    free(word);
}

void lsobj(const char * fileName, const char * path) {
    int fd = myOpen(fileName, O_RDWR);
    Superblock sb;
    readSuperblock(fd, &sb);
    Inode currDirInode = readInodeById(fd, sb, 0);
    Inode resInode;
    char * searchedFileName;

    if(strcmp(path, "+/") != 0) {
        searchedFileName = cd(fd, path, &currDirInode, sb);
        resInode = findInodeInFile(fd, sb, currDirInode, searchedFileName);
    } else {
        searchedFileName = malloc(strlen(path));
        strcpy(searchedFileName, path);
        resInode = currDirInode;
    }

    if(resInode.id != -1) {
        printInode(resInode, searchedFileName);
        free(searchedFileName);
    } else {
        close(fd);
        free(searchedFileName);
        errx(15, "File not found");
    }
}

DirTableRow * getAllFilesInodeIds(int fd, Superblock sb, int * childCounter, Inode resInode) {
    int readBytes = 0;
    DirTableRow * inodeIds = malloc(resInode.fileSize);
    int currDbId = 0;
    int numberOfIntsInDb = sb.data_block_size / sizeof(int);
    seekDatablockById(fd, sb, resInode.dataBlocks[currDbId++]);

    while(readBytes < resInode.fileSize) {
        bool addOffset = (readBytes % sb.data_block_size + sizeof(DirTableRow) > sb.data_block_size 
                        || (readBytes % sb.data_block_size == 0 && readBytes != 0));

        if(addOffset && currDbId < 10) {
            seekDatablockById(fd, sb, resInode.dataBlocks[currDbId++]);
        } else if(addOffset && numberOfIntsInDb > currDbId - 10) {
            seekDatablockById(fd, sb, resInode.doubleReferenceDataBlock);
            mySeek(fd, (currDbId - 10) * sizeof(int), SEEK_CUR, 
                    "Error seeking in doubleReferenceDataBlock findInodeInFile");

            int dbId;
            int curReadBytes = read(fd, &dbId, sizeof(int));
            errCheck(fd, curReadBytes, 6, "Error reading doubleReferenceDataBlock in findInodeInFile");
            seekDatablockById(fd, sb, dbId);
            currDbId++;
        }

        int curReadBytes = read(fd, &inodeIds[(*childCounter)++], sizeof(DirTableRow));
        errCheck(fd, curReadBytes, 6, "Error reading file from directory");

        readBytes += curReadBytes;
    }

    return inodeIds;
}

void lsdir(const char * fileName, const char * path) {
    int fd = myOpen(fileName, O_RDWR);
    Superblock sb;
    readSuperblock(fd, &sb);
    Inode currDirInode = readInodeById(fd, sb, 0);
    Inode resInode = currDirInode;

    if(strcmp(path, "+/") != 0) {
        char * searchedFileName = cd(fd, path, &currDirInode, sb);
        resInode = findInodeInFile(fd, sb, currDirInode, searchedFileName);
        free(searchedFileName);
    }

    //TODO: Put the above in a function. lsdir and lsobj are pretty close
    
    if(resInode.fileType != 'd') {
        close(fd);
        errx(16, "You have specified a file and not a directory");
    }
    
    int childCounter = 0;
    DirTableRow * inodeIds = getAllFilesInodeIds(fd, sb, &childCounter, resInode);

    for (int i = 0; i < childCounter; ++i) {
        printInode(readInodeById(fd, sb, inodeIds[i].inodeId), inodeIds[i].fileName);
    }
}

void firstFileTest(const char * fileName) {
    int fd = myOpen(fileName, O_RDWR);
    Superblock sb;
    readSuperblock(fd, &sb);
    Inode inode = readInodeById(fd, sb, 0);
    DirTableRow dirTableRow;
    printf("Reserved db: %d\n", inode.dataBlocks[0]);

    if(inode.dataBlocks[0] != -1) {
        seekDatablockById(fd, sb, inode.dataBlocks[0]);
        int pos = lseek(fd, 0, SEEK_CUR);

        printf("pos: %d\nShould be: %ld\n", pos, 512 + sb.inodeSpace);

        int readBytes = read(fd, &dirTableRow, sizeof(DirTableRow));
        errCheck(fd, readBytes, 6, "Error searching for file");
        printf("%s\n", dirTableRow.fileName);
    }
}

void mkfs(const char * fileName) {
    int fileSize = getFileSize(fileName);
    int fd = myOpen(fileName, O_RDWR);
    
    int inode_size = sizeof(Inode);
    int numberOfInodes = fileSize / 2000;
    int inodesPerDatablock = block_size / inode_size;
    //make numberOfInodes a multiple of inodesPerDatablock so when we calculate the number of
    //data_blocks needed to fit the inodes we don't have leftover space and the numberOfInodes fit in neatly
    numberOfInodes += inodesPerDatablock - numberOfInodes % inodesPerDatablock;
    int data_blocks_needed = numberOfInodes / inodesPerDatablock;
    uint64_t inodeSpace = data_blocks_needed * block_size;
    //-512 because the first dataBlock is for the superblock
    uint64_t dataBlocksSpace = fileSize - inodeSpace - block_size; 
    int numberOfDataBlocks = dataBlocksSpace / block_size;
    
    Superblock sb = superblockConstr(fileSize, block_size, numberOfInodes, inodeSpace,
                                     numberOfDataBlocks, dataBlocksSpace);
    
    initInodes(fd, sb);
    initDataBlocks(fd, sb);

    validateInodes(fd, sb);
    validateDataBlocks(fd, sb);

    Inode i = inodeConstr('d', 0, 0, "755");
    delegateInode(fd, &sb, &i);

    mySeek(fd, 0, SEEK_SET, "Error seeking to start of file in mkfs");
    sb.checksum = Fletcher16Checksum((uint8_t*)& sb, sizeof(Superblock));
    writeSuperblock(fd, sb);

    print(1, "FileSystem successfuly created!\n");
    close(fd);
}

int main(int argc, char * argv[]) {

    if(argc < 2) {
        errx(1, "Not enough arguments");
    }

    const char * fileName = getenv("BDSM_FS");

    if(strcmp(argv[1], "mkfs") == 0) {
        mkfs(fileName);   
    } else if(strcmp(argv[1], "fsck") == 0) {
        fsck(fileName);
    } else if(strcmp(argv[1], "debug") == 0) {
        debug(fileName);
    } else if(strcmp(argv[1], "mkdir") == 0) {
        if(argc < 3) {
            errx(1, "Not enough arguments");
        }

        myMkdir(fileName, argv[2]);
    } else if(strcmp(argv[1], "lsobj") == 0) {
        if(argc < 3) {
            errx(1, "Not enough arguments");
        }
        //firstFileTest(fileName);
        lsobj(fileName, argv[2]);
    } else if(strcmp(argv[1], "lsdir") == 0) {
        if(argc < 3) {
            errx(1, "Not enough arguments");
        } 
        
        lsdir(fileName, argv[2]);
    }

	return 0;
}
