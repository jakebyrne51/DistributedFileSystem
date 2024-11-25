#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
bool nread(int fd, int len, uint8_t *buf)
{
    int bytesRead = 0;
    while (bytesRead < len)
    {
        // read sys call
        int readCall = read(fd, buf + bytesRead, len - bytesRead);
        if (readCall < 0)
        {
            return false;
        }
        else
        {
            bytesRead += readCall;
        }
    }
    if (bytesRead > len || bytesRead < len)
    {
        return false;
    }
    return true;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
bool nwrite(int fd, int len, uint8_t *buf)
{
    int bytesWritten = 0;
    while (bytesWritten < len)
    {
        // write sys call
        int writeCall = write(fd, buf + bytesWritten, len - bytesWritten);
        if (writeCall < 0)
        {
            return false;
        }
        else
        {
            bytesWritten += writeCall;
        }
    }
    if (bytesWritten > len || bytesWritten < len)
    {
        return false;
    }
    return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
bool recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block)
{
    // Space for packet
    uint8_t packetSpace[HEADER_LEN];
    // Read data into our header
    if (nread(fd, HEADER_LEN, packetSpace) == false)
    {
        perror("false 2");
        return false;
    }
    // Copy what we need into the packet
    memcpy(op, packetSpace, sizeof(uint32_t));
    memcpy(ret, packetSpace + 4, sizeof(uint8_t));

    // Conversion back to host byte order from network
    *op = ntohl(*op);

    // ret of second to last bit for the block check we need
    if (!(*ret & 2))
    {
        return true;
    }
    // nread for block, this should work if we get to this point and if not we return false
    if (nread(fd, 256, block))
    {
        return true;
    }
    else
    {
        return false;
    }
    return true;
}

// Helper fuction to determine if its a write command.Main purpose is smoother readability
bool isWriteCommand(uint32_t op)
{
    uint8_t action = ((op >> 12) & 0x3f);
    if (action == JBOD_WRITE_BLOCK)
    {
        return true;
    }
    return false;
}

/* attempts to send a packet to sd; returns true on success and false on
 * failure */
bool send_packet(int sd, uint32_t op, uint8_t *block)
{
    // byte representing whether data block exists or not
    uint16_t dataCode;
    // converts data to network byte order
    uint32_t netOp = htonl(op);
    uint32_t writeSize;

    // Check to see if we are writing or not writing AKA see if we need block
    dataCode = isWriteCommand(op) == true ? 2 : 0;
    uint8_t packetSpace[HEADER_LEN + 256];
    // Copies operation into header
    memcpy(packetSpace, &netOp, sizeof(uint32_t));
    // Sets info code for header
    packetSpace[4] = dataCode;
    // Get size of write that we need to do
    writeSize = isWriteCommand(op) ? HEADER_LEN + JBOD_BLOCK_SIZE : HEADER_LEN;
    if (isWriteCommand(op) == true)
    {
        // memcpy for copying in a block for write commands
        memcpy(packetSpace + HEADER_LEN, block, JBOD_BLOCK_SIZE);
    }
    // Write should be successful at this point but if not, return false
    if (nwrite(sd, writeSize, packetSpace))
    {
        return true;
    }
    else
    {
        return false;
    }
}
// operations send and recieve a packet and returns the return code
int jbod_client_operation(uint32_t op, uint8_t *block)
{
    uint32_t opFromSendPack = 0;
    uint8_t returnCode;
    if (cli_sd == -1)
    {
        return -1;
    }
    if (send_packet(cli_sd, op, block) == false)
    {
        return -1;
    }
    if (recv_packet(cli_sd, &opFromSendPack, &returnCode, block) == false)
    {
        return -1;
    }
    if ((returnCode == (uint8_t)1))
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

//Moved jbod server stuff down here for organizational purposes. Made sense to me to section off the actual network implementation from the jbod server setup.
//AKA for readability

/* connect to server and set the global client variable to the socket */
bool jbod_connect(const char *ip, uint16_t port)
{
    if (cli_sd != -1)
    {
        jbod_disconnect();
    }
    struct sockaddr_in sockadd;
    cli_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (cli_sd == -1)
    {
        perror("socket");
        printf("Error on socket creation [%s]\n", strerror(errno));
        return false;
    }
    sockadd.sin_family = AF_INET;
    sockadd.sin_port = htons(port);
    if (inet_aton(ip, &sockadd.sin_addr) == 0)
    {
        perror("inet_pton");
        return false;
    }

    if (connect(cli_sd, (const struct sockaddr *)&sockadd, sizeof(sockadd)) == -1)
    {
        perror("connect");
        return false;
    }

    return true;
}

// disconnects from jbod server
void jbod_disconnect(void)
{
    // closes socket
    close(cli_sd);
    // resets client socket descriptor
    cli_sd = -1;
}