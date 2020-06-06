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
    char fileType;
    uint16_t uid;
    uint16_t gid;
    uint16_t permissions;
    int16_t reserved;
    int dataBlocks[10];
    int doubleReferenceDataBlock;
    int trippleReferenceDataBlock;
    time_t modificationTime;
} Inode;

typedef struct {
    uint16_t data_block_size;
    uint16_t inodeSize;
    uint16_t inodesPerDatablock;
    uint32_t size;
    uint32_t filesystemSize;
    uint32_t numberOfFreeInodes;
    uint32_t numberOfFreeDataBlocks;
    uint32_t firstFreeInode;
    uint32_t firstFreeDataBlock;
    uint64_t inodeSpace;
    uint64_t dataBlockSpace;
} Superblock;

Superblock superblockConstr(int fileSize, uint16_t data_block_size, int numberOfInodes,
                            int inodeSpace, int numberOfDataBlocks, int dataBlockSpace, int inode_size) {
    Superblock sb;
    sb.filesystemSize = fileSize;
    sb.data_block_size = data_block_size;
    sb.inodeSize = inode_size; 
    sb.numberOfFreeInodes = numberOfInodes;
    sb.numberOfFreeDataBlocks = numberOfDataBlocks;
    sb.firstFreeDataBlock = 0; 
    sb.firstFreeInode = 0;
    sb.inodesPerDatablock = block_size / inode_size;
    sb.size = sizeof(Superblock);

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

    for (int j = 0; j < 10; ++j) {
        i.dataBlocks[j] = -1;
    }

    return i;
}

void initDataBlocks(int * fd, Superblock sb) {
    int nextFreeDataBlockIndex;
    long currentOffset = lseek(*fd, 0, SEEK_CUR);

    if((currentOffset % sb.data_block_size) != 0) {
        errx(12, "Invalid offset at start of data block initialization");
    }

    for (int i = 0; i < sb.numberOfFreeDataBlocks; ++i) {
        nextFreeDataBlockIndex = i + 1;

        if(i == sb.numberOfFreeDataBlocks - 1) {
            nextFreeDataBlockIndex = -1;
        }

        if(write(*fd, &nextFreeDataBlockIndex, sizeof(int)) < 0) {
            int temp = errno;
            close(*fd);
            errno = temp;
            err(7, "Error while writing index of next data block in initDataBlocks");
        }

        if(lseek(*fd, sb.data_block_size - sizeof(int), SEEK_CUR) < 0) {
            int temp = errno;
            close(*fd);
            errno = temp;
            err(9, "Error lseeking to next next data block i initDataBlocks");
        }
    }
}

void initInodes(int * fd, Superblock sb) {
    Inode inode;
    int testOffset;
    if(lseek(*fd, sb.data_block_size, SEEK_SET) < 0) {
        int temp = errno;
        close(*fd);
        errno = temp;
        err(9, "lseek failed in during start of inode initialization");
    }

    int inodeOffset = sb.data_block_size % sb.inodeSize;

    for (int i = 0; i < sb.numberOfFreeInodes; ++i) {
        if(i != 0 && i % sb.inodesPerDatablock == 0) {
            //on every n inodes it adds the offsets
            if((testOffset = lseek(*fd, inodeOffset, SEEK_CUR)) < 0 ) {
                int temp = errno;
                close(*fd);
                errno = temp;
                err(9, "lseek failed in skipping offset in inode initialization");
            }
            
            if(testOffset % sb.data_block_size != 0) {
                errx(11, "Issue with inode initialization");
            }
        }

        inode.id = i;
        inode.nextFreeInode = i + 1;
        
        if(i == sb.numberOfFreeInodes - 1) {
            inode.nextFreeInode = -1;
        }

        if(write(*fd, &inode, sizeof(inode)) < 0) {
            int temp = errno;
            close(*fd);
            errno = temp;
            err(7, "Write of inode initialization");
        }
    }
    
    if(lseek(*fd, inodeOffset, SEEK_CUR) < 0) {
        int temp = errno;
        close(*fd);
        errno = temp;
        err(9, "Final lseek for offset failed");
    }
}

void writeInode(Inode i, Superblock* sb, int fd) {
    if(sb->numberOfFreeInodes == 0) {
        errx(8, "No inodes are left. File cannot be created");
    }

    int inodeOffset = sb->firstFreeInode * sb->inodeSize;
    lseek(fd, inodeOffset + sb->size, SEEK_SET);

    //Reads the currently written inode and gets the index of the next
    //one that is free and we write that to the superblock. After that we go back
    //To "create" the inode
    Inode buff;
    read(fd, &buff, sizeof(Inode));
    sb->firstFreeInode = buff.nextFreeInode;
    lseek(fd, -sizeof(Inode), SEEK_CUR);

    if(write(fd, &i, 64) < 0) {
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

void validateInodes(int * fd, Superblock sb) {
    Inode inode;
    int inodeOffset = sb.data_block_size % sb.inodeSize;

    if(lseek(*fd, sb.data_block_size, SEEK_SET) < 0) {
        int temp = errno;
        close(*fd);
        errno = temp;
        err(9, "lseek failed in during check of inode validation");
    }
    
    for (int i = 0; i < sb.numberOfFreeInodes; ++i) {
        if(i != 0 && i % sb.inodesPerDatablock == 0) {
            //on every n inodes it adds the offsets
            if(lseek(*fd, inodeOffset, SEEK_CUR) < 0 ) {
                int temp = errno;
                close(*fd);
                errno = temp;
                err(9, "lseek failed in skipping offset in inode validation");
            }
        }
        
        if(i == sb.numberOfFreeInodes - 1) {
            inode.nextFreeInode = -1;
        }

        if(read(*fd, &inode, sizeof(Inode)) < 0) {
            int temp = errno;
            close(*fd);
            errno = temp;
            err(7, "Write of inode initialization");
        }

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

    if(lseek(*fd, inodeOffset, SEEK_CUR) < 0) {
        int temp = errno;
        close(*fd);
        errno = temp;
        err(9, "Final lseek for offset failed during validation\n");
    }

    write(1, "Successful inode initialization!\n", 33);
}

void validateDataBlocks(int * fd, Superblock sb) {
    int nextFreeDataBlockIndex;
    for (int i = 0; i < sb.numberOfFreeDataBlocks; ++i) {
        nextFreeDataBlockIndex = i + 1;

        if(i == sb.numberOfFreeDataBlocks - 1) {
            nextFreeDataBlockIndex = -1;
        }

        if(read(*fd, &nextFreeDataBlockIndex, sizeof(int)) < 0) {
            int temp = errno;
            close(*fd);
            errno = temp;
            err(7, "Error while reading index of next data block in initDataBlocks");
        }

        if(nextFreeDataBlockIndex != i + 1 && i < sb.numberOfFreeDataBlocks - 1) {
            errx(11, "Datablock initilization failed");
        }

        if(nextFreeDataBlockIndex != -1 && i == sb.numberOfFreeDataBlocks - 1) {
            errx(11, "Datablock initilization failed lastIndex");
        }

        if(lseek(*fd, sb.data_block_size - sizeof(int), SEEK_CUR) < 0) {
            int temp = errno;
            close(*fd);
            errno = temp;
            err(9, "Error lseeking to next next data block i initDataBlocks");
        }
    }
    write(1, "Successful data block initialization!\n", 38);
}

void mkfs(const char * fileName) {
    int fileSize = getFileSize(fileName);
    int fd = open(fileName, O_RDWR);

    if(fd < 0) {
        err(2, "Invalid or nonexistent fileName");    
    }
    
    int inode_size = sizeof(Inode);
    int numberOfInodes = fileSize / 2000;
    int inodesPerDatablock = block_size / inode_size;
    //make numberOfInodes a multiple of inodesPerDatablock so when we calculate the number of
    //data_blocks needed to fit the inodes we don't have leftover space and the numberOfInodes fit in neatly
    numberOfInodes += inodesPerDatablock - numberOfInodes % inodesPerDatablock;
    int data_blocks_needed = numberOfInodes / inodesPerDatablock;
    int inodeSpace = data_blocks_needed * block_size;
    int dataBlocksSpace = fileSize - inodeSpace - 46; 
    int numberOfDataBlocks = dataBlocksSpace / block_size;
    
    Superblock sb = superblockConstr(fileSize, block_size, numberOfInodes, inodeSpace,
                                     numberOfDataBlocks, dataBlocksSpace, inode_size);
    
    if(write(fd, &sb, sb.size) < 0) {
        int errNum = errno;
        close(fd);
        errno = errNum;
        err(7, "Error writing Superblock while writingInode");
    }

    initInodes(&fd, sb);
    initDataBlocks(&fd, sb);
    validateInodes(&fd, sb);
    validateDataBlocks(&fd, sb);
    Inode i = inodeConstr(0, 'd', 0, 0, 755, time(NULL));
    writeInode(i, &sb, fd);
    write(1, "FileSystem successfuly created!\n",34);
}

int main(int argc, char * argv[]) {

    if(argc < 2) {
        errx(1, "Not enough arguments");
    }

    const char * fileName = getenv("BDSM_FS");

    if(strcmp(argv[1], "mkfs") == 0) {
        mkfs(fileName);   
    }

	return 42;
}
