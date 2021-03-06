#include <Kernel/IDT.h>
#include <Kernel/IO.h>
#include <Kernel/Registers32.h>

/* 16 hardware interrupts + 1 software interrupt
 */
#define IDT_INTERRUPT_HANDLERS_COUNT 17

/* Syscalls are element 16 of the InterruptHandlers array.
 */
#define IDT_SYSCALL_INTERRUPT_ID 16

/* There's a possible 256 interrupts on x86.
 */
#define IDT_ENTRY_COUNT 256

/* IDT_Entry
 * A standard structure for Interrupt Descriptors.
 */
struct IDT_Entry
{
	unsigned short OffsetLow;
	unsigned short Selector;
	unsigned char Zero;
	unsigned char TypeAttributes;
	unsigned short OffsetHigh;
} __attribute__((packed));

/* IDT_Pointer
 * A standard structure that is required for the lidt instruction.
 */
struct IDT_Pointer
{
	unsigned short Limit;
	void *Ptr;
} __attribute__((packed));

/* Allocate space for IDT_ENTRY_COUNT (256) IDT entries, this is the maxium
 * number of possible interrupts. In the future this could be done at run time
 * using some form of memory management.
 */
static struct IDT_Entry IDT[IDT_ENTRY_COUNT];

/* IDT_Pointer IDTPtr
 * A constant used by the LIDT instruction called in SetIDT.
 */
static const struct IDT_Pointer IDTPtr = 
{
	.Limit = (sizeof(struct IDT_Entry) * IDT_ENTRY_COUNT) - 1,
	.Ptr = IDT
};

/* InterruptHandlers
 * An array of handlers which can be set. These handlers are used in
 * InterruptHandlersStub. They are set using the SetInterruptHandler method.
 */
void (*InterruptHandlers[IDT_INTERRUPT_HANDLERS_COUNT])(void);

/* Ensure that we are aware of the Int_# functions which exist within IDTA.asm
*/
extern void Int_0(void),
	   Int_1(void),
	   Int_2(void),
	   Int_3(void),
	   Int_4(void),
	   Int_5(void),
	   Int_6(void),
	   Int_7(void),
	   Int_8(void),
	   Int_9(void),
	   Int_10(void),
	   Int_11(void),
	   Int_12(void),
	   Int_13(void),
	   Int_14(void),
	   Int_15(void),
	   IntSyscallHandler(void);

/* We create an array of the interrupt functions as these are used within our
 * IDT and mapped to our interrupts. The use of an array makes the assignment
 * much easier.
 */
const void (*Interrupts[16])(void) = 
{
	Int_0,
	Int_1,
	Int_2,
	Int_3,
	Int_4,
	Int_5,
	Int_6,
	Int_7,
	Int_8,
	Int_9,
	Int_10,
	Int_11,
	Int_12,
	Int_13,
	Int_14,
	Int_15
};

/* SetIDT
 * SetIDT is used once the 
 */
extern void SetIDT(const struct IDT_Pointer*);

/* IDT_InitializePIC
 * Remaps the PIC mappings as we do not want to use the BIOS defaults
 */
static void IDT_InitializePIC(void)
{
	outb(0x20, 0x11);
	outb(0xA0, 0x11);
	outb(0x21, 0x20);
	outb(0xA1, 0x28);
	outb(0x21, 0x04);
	outb(0xA1, 0x02);
	outb(0x21, 0x01);
	outb(0xA1, 0x01);
	outb(0x21, 0x00);
	outb(0xA1, 0x00);
}

/* IDT_SetInterruptEntry
 * Used to setup Interrupt Entries
 * unsigned int EID: The ID of the entry to modify, must be >= 32
 * void (*Func)(void): The function to be used by the interrupt.
 * Returns non zero on success. Value is the address of the entry filled.
 */
static unsigned int IDT_SetInterruptEntry(unsigned int EID, void (*Func)(void))
{
	/* We don't want to be setting entries for CPU Exceptions.
	*/
	if (EID < 32) return 0;

	struct IDT_Entry *Entry = &(IDT[EID]);
	Entry->OffsetLow  = 
		(unsigned short)((unsigned long)Func & 0x0000FFFF);
	Entry->OffsetHigh = 
		(unsigned short)((unsigned long)Func & 0xFFFF0000) >> 16;
	Entry->Zero = 0;
	Entry->TypeAttributes = 0x8E;
	Entry->Selector = 0x08;
	return (unsigned int)Entry;
}

/* IDT_Setup
 * Does everything necessary to setup the IDT so that the system remains stable
 * and supports interrupts.
 */
void IDT_Setup(void)
{
	unsigned int i = 0;
	DisableInterrupts();
	IDT_InitializePIC();

	/* Ensure that the InterruptHandlers pointers are all null to avoid
	 * accidentally calling somewhere random in memory which could cause
	 * problems.
	 */
	for (; i < IDT_INTERRUPT_HANDLERS_COUNT; i++)
	{
		InterruptHandlers[i] = 0;
	}

	/* Null out the first 32 entries, these are used by the processor for events
	 * such as a page fault. (System exceptions)
	 */
	for (i = 0; i < 32; i++)
	{
		IDT[i].OffsetLow 		= 0;
		IDT[i].OffsetHigh 		= 0;
		IDT[i].Zero 			= 0;
		IDT[i].Selector 		= 0;
		IDT[i].TypeAttributes 	= 0;
	}

	/* The next 16 interrupts are hardware interrupts which we care about. These
	 * are events such as keyboard input so we have created a function in
	 * assembly to handle them, this then calls into the InterruptHandlerStub
	 * which can call a regular (Standard C) function from the IntruptHandlers
	 * array.
	 */
	for (; i < 48; i++)
	{
		IDT_SetInterruptEntry(i, Interrupts[i-32]);
	}
	
	/* Finally we null out the remaining IDT entries.
	*/
	for (; i < IDT_ENTRY_COUNT; i++)
	{
		IDT[i].OffsetLow		= 0;
		IDT[i].OffsetHigh 		= 0;
		IDT[i].Zero 			= 0;
		IDT[i].Selector 		= 0;
		IDT[i].TypeAttributes 	= 0;
	}
	
	/* Register our syscall (software interrupt) handler. 
	 */
	IDT_SetInterruptEntry(HANDLER_SYSCALLS, IntSyscallHandler);
	
	/* Call our assembly function which is wrapping the lidt instruction for us.
	*/
	SetIDT(&IDTPtr);
}

void SetInterruptHandler(unsigned char ID, void (*Func)(void))
{
	if (ID < IDT_INTERRUPT_HANDLERS_COUNT-1)
	{
		InterruptHandlers[ID] = Func;
	}
	
	if (ID == 0x80)
	{
		InterruptHandlers[IDT_SYSCALL_INTERRUPT_ID] = Func;
	}
}

/* InterruptHandlerStub
 * The InterruptHandlerStub is a method of allowing the interrupt handlers to be
 * changed without having to reload the IDT every time, this also allows for the
 * use of regular function as generated by the compiler.
 * ID: The ID number with an offset of 32 to skip the system defined interrupts
 *     32+ID
 */
void InterruptHandlerStub(unsigned char ID, struct Registers32 Regs) 
{
	/* Handle hardware interrupts
	*/
	if (ID < 16)
	{
		void (*Func)(void) = (void (*)(void))InterruptHandlers[ID];
		if (Func != 0)
		{
			Func();
		}
	}

	/* Handle software interrupts (system calls)
	*/
	else if (ID == 0x80)
	{
		void (*Func)(struct Registers32) = 
			(void (*)(struct Registers32))
			InterruptHandlers[IDT_SYSCALL_INTERRUPT_ID];

		if (Func != 0)
		{
			Func(Regs);
		}
	}

	/* All interrupts from 8+ are members of the slave pic.
	 * Remembering that this is referring to the first 8 hardware interrupts
	 * that have been remapped due to the CPU exceptions using the first 32
	 * entries.
	 */
	if (ID >= 8)
	{
		outb(0xA0, 0x20);
	}
	outb(0x20, 0x20);
}

#undef IDT_ENTRY_COUNT
