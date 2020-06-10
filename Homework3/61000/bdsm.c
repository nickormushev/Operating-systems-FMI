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
#define block_size 512

typedef struct {
    int nextFreeInode;
    uint32_t id;
    uint64_t fileSize;
    char fileType;
    uint16_t uid;
    uint16_t gid;
    uint16_t permissions;
    int16_t reserved;
    int dataBlocks[10];
    int doubleReferenceDataBlock;
    int trippleReferenceDataBlock;
    time_t modificationTime;
    uint16_t checksum;
} Inode;

typedef struct {
    uint16_t data_block_size;
    uint16_t inodeSize;
    uint16_t inodesPerDatablock;
    uint32_t size;
    uint32_t filesystemSize;
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

Inode inodeConstr(int inodeId, char fileType, uint16_t uid, uint16_t gid,
                  uint16_t permissions, time_t modificationTime) {
    Inode i;
    i.id = inodeId;
    i.uid = uid;
    i.gid = gid;
    i.fileType = fileType;
    i.permissions = permissions;
    i.modificationTime = modificationTime;
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

void print(int fd, const char * msg) {
    write(fd, msg, strlen(msg));
}

int myOpen(const char * fileName, int mode) {
    int fd = open(fileName, mode);

    if(fd < 0) {
        err(2, "Invalid or nonexistent fileName");    
    }

    return fd;
}

uint64_t mySeek(int fd, uint64_t offset, int mode, const char * errMsg) {
    int pos;
    if((pos = lseek(fd, offset, mode)) < 0) {
        int temp = errno;
        close(fd);
        errno = temp;
        err(9, "%s", errMsg);    
    }

    return pos;
}

Inode readInodeById(int fd, Superblock sb, int inodeId) {
    Inode readInode;
    int offsetPerNInodes = sb.data_block_size - sb.inodesPerDatablock * sb.inodeSize;
    long ofssetAmmount = (inodeId / sb.inodesPerDatablock) * offsetPerNInodes;
    long inodeOffset = sb.data_block_size + inodeId * sb.inodeSize + ofssetAmmount;

    mySeek(fd, inodeOffset, SEEK_SET, 
           "Couldn't seek inode during InodeCount validation");

    if(read(fd, &readInode, sizeof(Inode)) < 0) {
        int temp = errno;
        close(fd);
        errno = temp;
        err(7, "%s", "Inode could not be read");  
    }

    return readInode;
}

void readSuperblock(int fd, Superblock * sb, const char * errMsg) {
    mySeek(fd, 0, SEEK_SET, "Error seeking to start of file ine readSuperblock");
    if(read(fd, sb, sizeof(Superblock)) < 0) {
        int temp = errno;
        close(fd);
        errno = temp;
        err(10, "%s", errMsg);    
    }
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

    if(read(fd, inode, sizeof(Inode)) < 0) {
        int temp = errno;
        close(fd);
        errno = temp;
        err(7, "Read of inode failed");
    }
}

void initDataBlocks(int fd, Superblock sb) {
    int nextFreeDataBlockIndex;

    long currentOffset = mySeek(fd, 0, SEEK_CUR, "lseek error in initDataBlocks");
    if((currentOffset % sb.data_block_size) != 0) {
        errx(12, "Invalid offset at start of data block initialization");
    }

    for (int i = 0; i < sb.numberOfFreeDataBlocks; ++i) {
        nextFreeDataBlockIndex = i + 1;

        if(i == sb.numberOfFreeDataBlocks - 1) {
            nextFreeDataBlockIndex = -1;
        }

        if(write(fd, &nextFreeDataBlockIndex, sizeof(int)) < 0) {
            int temp = errno;
            close(fd);
            errno = temp;
            err(7, "Error while writing index of next data block in initDataBlocks");
        }

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

void printInode(Inode i) {
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
    printf("%d\n", i.permissions);
    printf("%d\n", i.reserved);
    printf("%d\n", i.trippleReferenceDataBlock);
    printf("%d\n", i.uid);

    printf("---------------------\n");
}

void initInodes(int fd, Superblock sb) {
    int testOffset;
    Inode inode = inodeConstr(-1, 'd', 0, 0, 755, time(NULL));
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

        if(write(fd, &inode, sizeof(Inode)) < 0) {
            int temp = errno;
            close(fd);
            errno = temp;
            err(7, "Write of inode initialization");
        }
    }
    
    mySeek(fd, inodeOffset, SEEK_CUR, "Final lseek for offset failed in inodeInit");
}

void writeInode(Inode i, Superblock * sb, int fd) {
    if(sb->numberOfFreeInodes == 0) {
        errx(8, "No inodes are left. File cannot be created");
    }

    //int inodeOffset = sb->firstFreeInode * sb->inodeSize;
    //mySeek(fd, inodeOffset + sb->data_block_size, SEEK_SET, 
    //       "Error seeking the position of an inode in writeInode");

    //Reads the currently written inode and gets the index of the next
    //one that is free and we write that to the superblock. After that we go back
    //To "create" the inode
    Inode buff = readInodeById(fd, *sb, sb->firstFreeInode);
    sb->firstFreeInode = buff.nextFreeInode;

    mySeek(fd, -sizeof(Inode), SEEK_CUR, 
          "Error seeking back to the position of an inode in writeInode");
    
    i.checksum = Fletcher16Checksum((uint8_t *) &i, sizeof(Inode));

    if(write(fd, &i, sizeof(Inode)) < 0) {
        err(7, "Error writing Inode while writingInode");
    }

    sb->numberOfFreeInodes--;
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
            errx(10, "Inode initialization failed! Inode Corrupt");
        };

        if(inode.id != i) {
            errx(10, "Inode initialization failed! Wrong inode id!");
        }

        if(i != sb.numberOfFreeInodes - 1 && inode.nextFreeInode != i + 1) {
            errx(10, "Inode initialization failed! Wrong nextFreeInode");
        }

        if( i == sb.numberOfFreeInodes - 1 && inode.nextFreeInode != -1) {
            errx(10, "Initialization of last inode failed. Wrong nextFreeInode");
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

        if(read(fd, &nextFreeDataBlockIndex, sizeof(int)) < 0) {
            int temp = errno;
            close(fd);
            errno = temp;
            err(7, "Error while reading index of next data block in initDataBlocks");
        }

        if(nextFreeDataBlockIndex != i + 1 && i < sb.numberOfFreeDataBlocks - 1) {
            errx(11, "Datablock initilization failed");
        }

        if(nextFreeDataBlockIndex != -1 && i == sb.numberOfFreeDataBlocks - 1) {
            errx(11, "Datablock initilization failed lastIndex");
        }

        mySeek(fd, sb.data_block_size - sizeof(int), SEEK_CUR,
              "Error lseeking to next next data block i initDataBlocks");
    }

    write(1, "Successful data block initialization!\n", 38);
}

void printDataBlock(Superblock * sb) {
    printf("%ld\n", sb->dataBlockSpace);
    printf("%d\n", sb->data_block_size);
    printf("%d\n", sb->inodeSize);
    printf("%d\n", sb->inodesPerDatablock);
    printf("%d\n", sb->checksum);
    printf("%ld\n", sb->inodeSpace);
    printf("%d\n", sb->numberOfFreeInodes);
    printf("%d\n", sb->numberOfFreeDataBlocks);
    printf("freeData: %d\n", sb->firstFreeDataBlock);
    printf("freeInode: %d\n", sb->firstFreeInode);
    printf("fsSize: %d\n", sb->filesystemSize);
    printf("sbSize: %d\n", sb->size);
}

bool validateSuperblockInodeCount(int fd, Superblock * sb) {
    int inodeCounter = 0;
    int currInodeId = sb->firstFreeInode;   
    Inode inode;

    while(currInodeId != -1) {

        inodeCounter++;
    }

    return inodeCounter == sb->numberOfFreeInodes;
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

void validateSuperbock(Superblock sb) {
    uint16_t readChecksum = sb.checksum;
    sb.checksum = 0;

    if(Fletcher16Checksum((uint8_t*)& sb, sizeof(Superblock)) != readChecksum) {
        errx(10, "Corrupted Superblock");
    }
}

void fsck(const char * fileName) {
    int fd = myOpen(fileName, O_RDONLY);
    Superblock sb;
    readSuperblock(fd, &sb, "Superblock could not be read during fsck");
    validateSuperbock(sb);
    validateInodeChecksums(fd, sb);


    print(1, "File system is operational!\n");
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
    Inode i = inodeConstr(0, 'd', 0, 0, 755, time(NULL));
    writeInode(i, &sb, fd);
    sb.checksum = Fletcher16Checksum((uint8_t*)& sb, sizeof(Superblock));

    mySeek(fd, 0, SEEK_SET, "Error seeking to start of file in mkfs");
    if(write(fd, &sb, sizeof(Superblock)) < 0) {
        int errNum = errno;
        close(fd);
        errno = errNum;
        err(7, "Error writing Superblock while writingInode");
    }

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

    }

	return 0;
}
