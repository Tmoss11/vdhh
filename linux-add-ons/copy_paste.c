/*
 * Copyright (C) 2016 Veertu Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/io.h>


typedef uint64_t UINT64;
typedef uint32_t UINT32;
typedef uint8_t  UINT8;
typedef uint32_t DWORD;
typedef int      BOOL;

char gbuffer[4096 * 1024];

void SetPortVal(uint32_t port, uint32_t val, int size)
{
    outl(val, port);
}

void GetPortVal(uint32_t port, uint32_t *val, int size)
{
   *val = (uint32_t)inl(port);
}

BOOL SyncVMXPageNoSet(UINT64 page_addr, DWORD command)
{
    DWORD val;
    SetPortVal(0x1854, command, 4);
    GetPortVal(0x1854, &val, 4);
    if (val)
        return 0;
    return 1;
}

BOOL SyncVMXPage(UINT64 page_addr, DWORD command)
{
    DWORD val;
    memset((void *)page_addr, 0, 4096);
    SetPortVal(0x1854, command, 4);
    GetPortVal(0x1854, &val, 4);
    if (val)
	return 0;
    return 1;
}

BOOL SyncVMXPageVal(UINT64 page_addr, DWORD command, DWORD nval)
{
    DWORD val;
    memset((void *)page_addr, 0, 4096);
    *(UINT32*)page_addr = nval;
    SetPortVal(0x1854, command, 4);
    GetPortVal(0x1854, &val, 4);
    if (val)
        return 0;
    return 1;
}

static UINT32 VMX_MIN(UINT32 a, UINT32 b)
{
    if (a > b)
        return b;
    return a;
}

int grab_sync = 0;
int ungrab_sync = 0;

int need_to_copy = 0;
int copy_index = 0;
int copyed = 0;

int CopyFromVmxFirst(UINT64 gva)
{
    int size;
    UINT32 *ptr;

    SyncVMXPage(gva, 4);
    ptr = (UINT32 *)gva;

    grab_sync = ptr[1];
    ungrab_sync = ptr[2];

    if (grab_sync) {
        size = (int)ptr[3];
        need_to_copy = size - VMX_MIN(size, 4096 - 1024);
        copyed = copy_index = VMX_MIN(size, 4096 - 1024);
    } else {
        need_to_copy = copyed = copy_index = 0;
    }

    return copyed;
}

int CopyFromVmxSeconed(UINT64 gva)
{
    UINT32 *ptr;

    SyncVMXPageVal(gva, 5, copy_index);
    ptr = (UINT32 *)gva;

    copyed = VMX_MIN(need_to_copy, 4096);
    need_to_copy -= copyed;
    copy_index += copyed;

    return copyed;
}

int CopyToVmxFirst(UINT64 gva, UINT8 *buffer, int size)
{
    UINT32 *ptr = (UINT32 *)gva;
    ptr[0] = size;

    copyed = 0;
    memcpy((void *)(gva + 1024), buffer, VMX_MIN(size, 4096 - 1024));
    if (SyncVMXPageNoSet(gva, 6)) {
        copyed = copy_index = VMX_MIN(size, 4096 - 1024);
        need_to_copy = size - copyed;
    } 

    return copyed;
}

int CopyToVmxSeconed(UINT64 gva, UINT8 *buffer)
{
    copyed = 0;
    memcpy((void *)gva, buffer + copy_index, VMX_MIN(need_to_copy, 4096));
    if (SyncVMXPageNoSet(gva, 7)) {
        copyed = VMX_MIN(need_to_copy, 4096);
        copy_index += copyed;
	need_to_copy -= copyed;
    }

    return copyed;
}

void SyncVM(UINT64 gva)
{
    UINT32 *ptr = (UINT32 *)gva;
    while (1)
    {
        CopyFromVmxFirst(gva);
        if (grab_sync) {
            char *cpy_ptr;
            char *hMem =  malloc(ptr[3] + 1024);

            strcpy(hMem, "echo \"");
            cpy_ptr = hMem + strlen(hMem);
            memcpy(cpy_ptr, (char *)gva + 1024, copyed);
            if (need_to_copy) {
                cpy_ptr = (char *)(((UINT8 *)cpy_ptr) + copyed);
                while (need_to_copy) {
                    if (CopyFromVmxSeconed(gva)) {
                        memcpy(cpy_ptr, (char *)gva, copyed);
                        cpy_ptr = (char *)(((UINT8 *)cpy_ptr) + copyed);
                    }
                }
            }
            strcat(hMem ,"\" | xclip -selection clipboard");
            system(hMem);
            } else if (ungrab_sync) {
                if (1) {
                    int x = 0;
                    FILE *file = popen("xclip -out", "r");

                    if (gbuffer) {
                        while (!feof(file) && x < (4096 * 1024 - 1))
                            gbuffer[x++] = fgetc(file);
                            gbuffer[x-1] = '\0';
                            if (CopyToVmxFirst(gva, (UINT8 *)gbuffer, strlen(gbuffer) + 1)) {
                                while (need_to_copy) {
                                    CopyToVmxSeconed(gva, (UINT8 *)gbuffer);
                                }
                            }
                    }
                    pclose(file);
               }
               ungrab_sync = 0;
           }
           usleep(50000);
    }
}

UINT64 AllocRegisterVMXPage()
{
    void *ptr;
    UINT64 val;

    ptr = aligned_alloc(4096, 4096);
    if (!ptr)
        exit(1);

    val = (UINT64)ptr;
    SetPortVal(0x1854, 2, 4);
    SetPortVal(0x1854, (UINT32)val, 4);
    SetPortVal(0x1854, (UINT32)(val >> 32), 4);

    return (UINT64)ptr;
}

int main()
{
    uint64_t gva;

    setuid(0);  

    if (ioperm(0x1854, 4, 1)) {perror("ioperm"); exit(1);}

    gva = AllocRegisterVMXPage();
    SyncVM(gva);

   /* We don't need the ports anymore */
   if (ioperm(0x1854, 4, 0)) {perror("ioperm"); exit(1);}

   exit(0);
}
