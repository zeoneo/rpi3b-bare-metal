#include<device/usb-mem.h>
#include<plibc/stdio.h>
#include <stdarg.h>




#define HEAP_END ((void*)0xFFFFFFFF)

struct HeapAllocation {
	uint32_t Length;
	void* Address;
	struct HeapAllocation *Next;
};

uint8_t Heap[0x4000] __attribute__((aligned(8))); // Support a maximum of 16KiB of allocations
struct HeapAllocation Allocations[0x100]; // Support 256 allocations
struct HeapAllocation *FirstAllocation = HEAP_END, *FirstFreeAllocation = NULL;
uint32_t allocated = 0;

void* MemoryReserve(uint32_t length, void* physicalAddress) {
	printf("\n Allocating %d", length);
	return physicalAddress;
}


void* MemoryAllocate(uint32_t size) {
	struct HeapAllocation *Current, *Next;
	if (FirstFreeAllocation == NULL) {
		printf("Platform: First memory allocation, reserving 16KiB of heap, 256 entries.\n");
		MemoryReserve(sizeof(Heap), &Heap);
		MemoryReserve(sizeof(Allocations), &Allocations);

		FirstFreeAllocation = &Allocations[0];
	}

	size += (8 - (size & 7)) & 7; // Align to 8

	if (allocated + size > sizeof(Heap)) {
		printf("Platform: Out of memory! We should've had more heap space in platform.c.\n");
		return NULL;
	}
	
	if (FirstFreeAllocation == HEAP_END) {
		printf("Platform: Out of memory! We should've had more allocations in platform.c.\n");
		return NULL;
	}
	Current = FirstAllocation;

	while (Current != HEAP_END) {
		if (Current->Next != HEAP_END) {
			if ((uint32_t)Current->Next->Address - (uint32_t)Current->Address - Current->Length >= size) {
				FirstFreeAllocation->Address = (void*)((uint8_t*)Current->Address + Current->Length);
				FirstFreeAllocation->Length = size;
				Next = FirstFreeAllocation;
				if (Next->Next == NULL)
					if ((uint32_t)(FirstFreeAllocation + 1) < (uint32_t)((uint8_t*)Allocations + sizeof(Allocations)))
						FirstFreeAllocation = FirstFreeAllocation + 1;
					else
						FirstFreeAllocation = HEAP_END;
				else
					FirstFreeAllocation = Next->Next;
				Next->Next = Current->Next;
				Current->Next = Next;
				allocated += size;
				printf("Platform: malloc(%#x) = %#x. (%d/%d)\n", size, Next->Address, allocated, sizeof(Heap));
				return Next->Address;
			}
			else
				Current = Current->Next;
		} else {
			if ((uint32_t)&Heap[sizeof(Heap)] - (uint32_t)Current->Next - Current->Length >= size) {
				FirstFreeAllocation->Address = (void*)((uint8_t*)Current->Address + Current->Length);
				FirstFreeAllocation->Length = size;
				Next = FirstFreeAllocation;
				if (Next->Next == NULL)
					if ((uint32_t)(FirstFreeAllocation + 1) < (uint32_t)((uint8_t*)Allocations + sizeof(Allocations)))
						FirstFreeAllocation = FirstFreeAllocation + 1;
					else
						FirstFreeAllocation = HEAP_END;
				else
					FirstFreeAllocation = Next->Next;
				Next->Next = Current->Next;
				Current->Next = Next;
				allocated += size;
				printf("Platform: malloc(%#x) = %#x. (%d/%d)\n", size, Next->Address, allocated, sizeof(Heap));
				return Next->Address;
			}
			else {
				printf("Platform: Out of memory! We should've had more heap space in platform.c.\n");
				printf("Platform: malloc(%#x) = %#x. (%d/%d)\n", size, NULL, allocated, sizeof(Heap));
				return NULL;
			}
		}
	}
	
	Next = FirstFreeAllocation->Next;
	FirstAllocation = FirstFreeAllocation;
	FirstAllocation->Next = HEAP_END;
	FirstAllocation->Length = size;
	FirstAllocation->Address = &Heap;
	if (Next == NULL)
		if ((uint32_t)(FirstFreeAllocation + 1) < (uint32_t)((uint8_t*)Allocations + sizeof(Allocations)))
			FirstFreeAllocation = FirstFreeAllocation + 1;
		else
			FirstFreeAllocation = HEAP_END;
	else
		FirstFreeAllocation = Next;
	allocated += size;
	printf("Platform: malloc(%#x) = %#x. (%d/%d)\n", size, FirstAllocation->Address, allocated, sizeof(Heap));
	return FirstAllocation->Address;
}

void MemoryDeallocate(void* address) {
	struct HeapAllocation *Current, **CurrentAddress;

	CurrentAddress = &FirstAllocation;
	Current = FirstAllocation;

	while (Current != HEAP_END) {
		if (Current->Address == address) {
			allocated -= Current->Length;
			*CurrentAddress = Current->Next;
			Current->Next = FirstFreeAllocation;
			FirstFreeAllocation = Current;
			printf("Platform: free(%#x) (%d/%d)\n", address, allocated, sizeof(Heap));
			return;
		}
		else {
			Current = Current->Next;
			CurrentAddress = &((*CurrentAddress)->Next);
		}
	}
	
	printf("Platform: free(%#x) (%d/%d)\n", address, allocated, sizeof(Heap));
	printf("Platform: Deallocated memory that was never allocated. Ignored, but you should look into it.\n");
}

void MemoryCopy(void* destination, void* source, uint32_t length) {
	uint8_t *d, *s;
	
	if (length == 0) return;

	d = (uint8_t*)destination;
	s = (uint8_t*)source;

	if ((uint32_t)s < (uint32_t)d)
		while (length-- > 0)
			*d++ = *s++;
	else {
		d += length;
		s += length;
		while (length-- > 0)
			*--d = *--s;
	}
}

void PlatformLoad()
{
	FirstAllocation = HEAP_END;
	FirstFreeAllocation = NULL;
}