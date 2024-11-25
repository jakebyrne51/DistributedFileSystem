#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "net.h"

static uint8_t isMounted = 0;
static uint8_t canWrite = 0;

// Mounts the linear device. Only can mount if unmounted
int mdadm_mount(void)
{
  // shifts bit 12 to the left so we can pass in the command to be executed by JBOD
  int bitShift = JBOD_MOUNT << 12;
  int mountOperation = jbod_client_operation(bitShift, NULL);
  if (mountOperation == 0)
  {
    isMounted = 1;
    return 1;
  }
  else
  {
    return -1;
  }
}

// Unmounts the linear device. Only can unmount if mounted
int mdadm_unmount(void)
{
  // shifts bit 12 to the left so we can pass in the command to be executed by JBOD
  int bitShift = JBOD_UNMOUNT << 12;
  int unmountOperation = jbod_client_operation(bitShift, NULL);
  if (unmountOperation == 0)
  {
    isMounted = 0;
    return 1;
  }
  else
  {
    return -1;
  }
}

// Gives write permission
int mdadm_write_permission(void)
{
  int bitShift = JBOD_WRITE_PERMISSION << 12;
  int giveWritePermission = jbod_client_operation(bitShift, NULL);
  if (giveWritePermission == 0)
  {
    canWrite = 1;
    return 1;
  }
  return -1;
}

// Revokes write permission
int mdadm_revoke_write_permission(void)
{
  int bitShift = JBOD_REVOKE_WRITE_PERMISSION << 12;
  int revokeWritePermission = jbod_client_operation(bitShift, NULL);
  if (revokeWritePermission == 0)
  {
    canWrite = 0;
    return 1;
  }
  return -1;
}

// HELPER FUNCTION TO DETERMINE IF ITS TIME TO MOVE TO THE NEXT DISK
int nextDisk(uint32_t currentBlock, uint32_t previousBlock, uint32_t currentDisk, uint32_t previousDisk)
{
  if (currentBlock == 0 && (currentBlock != previousBlock) && currentDisk != previousDisk)
  {
    return 1;
  }
  return 0;
}

// HELPER FUNCTION THAT SEEKS TO THE DISK
int seekToDisk(uint32_t disk)
{
  uint32_t seekDiskShift = (JBOD_SEEK_TO_DISK << 12) | (disk);
  int seekDisk = jbod_client_operation(seekDiskShift, NULL);
  if (seekDisk != 0)
  {
    return -1;
  }
  return 1;
}

// HELPER FUNCTION THAT SEEKS TO THE BLOCK
int seekToBlock(uint32_t block)
{
  uint32_t seekBlockShift = (JBOD_SEEK_TO_BLOCK << 12) | (block << 4);
  int seekBlock = jbod_client_operation(seekBlockShift, NULL);
  if (seekBlock != 0)
  {
    return -1;
  }
  return 1;
}

// HELPER FUNCTION THAT READS THE BLOCK
int readToBlock(uint8_t *tempBuff)
{
  uint32_t readBlockShift = (JBOD_READ_BLOCK << 12);
  int readBlock = jbod_client_operation(readBlockShift, tempBuff);
  if (readBlock != 0)
  {
    return -1;
  }
  return 1;
}

int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)
{

  int addressTracker = start_addr;
  uint8_t tempBuff[JBOD_BLOCK_SIZE];

  // Checks for error cases such as too large of a read, too much data, unmounted, and no buffer to read into.
  if (read_len > 1024)
  {
    return -1;
  }
  else if (start_addr + read_len > JBOD_NUM_DISKS * JBOD_DISK_SIZE)
  {
    return -1;
  }
  else if (mdadm_mount() == 1)
  {
    return -1;
  }
  else if (read_len > 0 && read_buf == NULL)
  {
    return -1;
  }
  else if (read_buf == 0 && read_len != 0)
  {
    return read_len;
  }

  uint32_t startDisk = (start_addr / (JBOD_DISK_SIZE));
  uint32_t startBlock = (start_addr % (JBOD_BLOCK_SIZE * JBOD_NUM_BLOCKS_PER_DISK) / JBOD_BLOCK_SIZE);
  seekToDisk(startDisk);
  seekToBlock(startBlock);

  // While theres data to read
  while (addressTracker < start_addr + read_len)
  {
    // Tracks current disk, current block, previous block, and the block difference(for offsets)
    uint32_t currDisk = (addressTracker / (JBOD_DISK_SIZE));
    uint32_t currBlock = (addressTracker % ((JBOD_BLOCK_SIZE * JBOD_NUM_BLOCKS_PER_DISK) / JBOD_BLOCK_SIZE));
    int32_t prevBlock = ((addressTracker - 1) % ((JBOD_BLOCK_SIZE * JBOD_NUM_BLOCKS_PER_DISK) / JBOD_BLOCK_SIZE));
    uint32_t blockDiff;
    if (nextDisk(currBlock, prevBlock, currDisk, startDisk) == 1)
    {
      // Changes our disk
      seekToDisk(currDisk);
      seekToBlock(currBlock);
    }

    // Reads data into the temp buffer
    readToBlock(tempBuff);

    blockDiff = (addressTracker % JBOD_BLOCK_SIZE);
    if (blockDiff >= JBOD_BLOCK_SIZE || blockDiff < 0)
    {
      return -1;
    }

    // Below finds bytes to be copied based on length of read and space left in block

    uint32_t bytesToBeRead = (start_addr + read_len) - addressTracker;
    uint32_t bytesInBlock = JBOD_BLOCK_SIZE - blockDiff;
    uint32_t copyBytes;

    if (bytesInBlock > bytesToBeRead)
    {
      copyBytes = bytesToBeRead;
    }
    else if (bytesToBeRead >= bytesInBlock)
    {
      copyBytes = bytesInBlock;
    }

    if (addressTracker < start_addr || addressTracker < 0 || addressTracker > start_addr + read_len)
    {
      return -1;
    }
    // Copies data into the read buffer
    // memcpy(read_buf + (addressTracker - start_addr), tempBuff + blockDiff, copyBytes);
    memcpy(read_buf + (addressTracker - start_addr), tempBuff + blockDiff, copyBytes);
    //  Increments address by amount of bytes we copied
    addressTracker += copyBytes;
  }
  return read_len;
}

// HELPER FUNCTION THAT WRITES TO THE BLOCK
int writeToBlock(uint8_t *tempBuff)
{
  uint32_t writeBlockShift = (JBOD_WRITE_BLOCK << 12);
  int writeBlock = jbod_client_operation(writeBlockShift, tempBuff);
  if (writeBlock != 0)
  {
    return -1;
  }
  return 1;
}

int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf)
{
  int addressTracker = start_addr;
  uint8_t tempBuff[JBOD_BLOCK_SIZE];

  // Checks for error cases such as too large of a read, too much data, unmounted, and no buffer to read into.
  if (write_len > 1024)
  {
    return -1;
  }
  else if (start_addr + write_len > JBOD_NUM_DISKS * JBOD_DISK_SIZE)
  {
    return -1;
  }
  else if (isMounted == 0)
  {
    return -1;
  }
  else if (write_len > 0 && write_buf == NULL)
  {
    return -1;
  }
  else if (canWrite == 0)
  {
    return -1;
  }

  uint32_t startDisk = (start_addr / (JBOD_DISK_SIZE));
  uint32_t startBlock = (start_addr % (JBOD_BLOCK_SIZE * JBOD_NUM_BLOCKS_PER_DISK) / JBOD_BLOCK_SIZE);
  seekToDisk(startDisk);
  seekToBlock(startBlock);

  // While theres data to read
  while (addressTracker < start_addr + write_len)
  {
    uint32_t currDisk = (addressTracker / (JBOD_DISK_SIZE));
    uint32_t currBlock = addressTracker % ((JBOD_BLOCK_SIZE * JBOD_NUM_BLOCKS_PER_DISK)) / JBOD_BLOCK_SIZE;
    seekToDisk(currDisk);
    seekToBlock(currBlock);
    readToBlock(tempBuff);
    seekToDisk(currDisk);
    seekToBlock(currBlock);

    // Tracks current disk, current block, previous block, and the block difference(for offsets)
    uint32_t blockDiff;

    blockDiff = (addressTracker % JBOD_BLOCK_SIZE);

    // Below finds bytes to be copied based on length of read and space left in block

    int bytesToWriteTo = (start_addr + write_len) - addressTracker;
    int bytesInBlock = JBOD_BLOCK_SIZE - blockDiff;
    int writeBytes;

    if (bytesInBlock >= bytesToWriteTo)
    {
      writeBytes = bytesToWriteTo;
    }
    else if (bytesToWriteTo > bytesInBlock)
    {
      writeBytes = bytesInBlock;
    }

    // Copies what we want to write into the temp buffer
    memcpy(tempBuff + blockDiff, write_buf + (addressTracker - start_addr), writeBytes);
    writeToBlock(tempBuff);

    // Increments address by amount of bytes we copied
    addressTracker += writeBytes;
  }
  return write_len;
}